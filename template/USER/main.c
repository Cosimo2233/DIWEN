#include "sys.h"

/* 兼容 VSCode IntelliSense (不识别 Keil C51 的 xdata 关键字) */
#ifdef __C51__
#define XDATA xdata
#else
#define XDATA
#endif

/*===========================================================================
 * 节奏大师 - 迪文屏二次开发 (T5L OS 8051)
 *
 * ==================== DGUS 一次开发配置 (需核对) ====================
 *
 *  界面5 (游戏):
 *    音符变量图标 VP: 0x2006~0x2009 (4条轨道, Y坐标值)
 *    击中返回控件 VP: 0x200A~0x200D
 *    底层转换变量图标 VP: 0x2004 (0=游戏, 1=成功→6, 2=失败→7)
 *
 *  界面8 (游戏关2):
 *    音符变量图标 VP: 0x2010~0x2013 (4条轨道, Y坐标值)
 *    击中返回控件 VP: 0x2014~0x2017
 *    底层转换变量图标 VP: 0x2005 (0=游戏, 1=成功→9, 2=失败→10)
 *
 *  ★ 采用VP地址方式写音符Y坐标 (非SP描述指针方式)
 *    写入 VP(0x2006~0x2009 / 0x2010~0x2013) → DGUS变量图标Y坐标变化
 *
 * ==================== 界面跳转流程 (DGUS独立完成) ====================
 *  界面4 → 界面5 + 播放音频0 (DGUS按钮配置)
 *  界面6 → 界面8 + 播放音频1 (DGUS按钮配置)
 *  界面7 → 界面5 + 播放音频0 (DGUS按钮配置)
 *  界面9 → 界面0
 *  界面10 → 界面8 + 播放音频1
 *
 *  ★ 音频播放由DGUS一次开发配置完成, C代码不主动控制音频
 *
 * ==================== 界面转换 (C代码写底层VP触发) ====================
 *  VP(0x2004/0x2005) = 0 → 游戏界面  (通电默认)
 *  VP(0x2004/0x2005) = 1 → 成功界面6/9
 *  VP(0x2004/0x2005) = 2 → 失败界面7/10
 *===========================================================================*/

/*==================== 界面编号 ====================*/
#define PAGE_GAME_5         5U
#define PAGE_SUCCESS_6      6U
#define PAGE_FAIL_7         7U
#define PAGE_GAME_8         8U
#define PAGE_SUCCESS_9      9U
#define PAGE_FAIL_10        10U

/*==================== DGUS 系统 VP 地址 ====================*/
#define VP_PAGE_ID          0x0014U   /* 当前界面编号 (只读) */
#define VP_PAGE_SWITCH      0x0084U   /* 界面切换命令 (只写) */

/*==================== 全局 VP 地址 (界面5和界面8共用) ====================*/
#define VP_GAME_STATUS      0x2000U   /* 游戏状态 */
#define VP_GAME_TIMER       0x2001U   /* 计时器 (ms) */
#define VP_SCORE            0x2002U   /* 得分 */
#define VP_JUDGE_RESULT     0x2003U   /* 判定反馈 (0=无,1=击中,3=漏击) */
#define VP_CONVERT_5        0x2004U   /* 界面5底层转换变量图标 (0=游戏,1=成功,2=失败) */
#define VP_CONVERT_8        0x2005U   /* 界面8底层转换变量图标 (0=游戏,1=成功,2=失败) */

/*
 * ★ VP地址方式: 直接写变量图标VP地址更新Y坐标
 *   页面5: 4条轨道的Y坐标分别对应 VP=0x2006~0x2009
 *   页面8: 4条轨道的Y坐标分别对应 VP=0x2010~0x2013
 */
#define NOTE5_Y_BASE        0x2006U   /* 页面5 音符Y坐标VP基址 (每条轨道+1) */
#define NOTE8_Y_BASE        0x2010U   /* 页面8 音符Y坐标VP基址 (每条轨道+1) */

/* 界面5 击中按钮 VP */
#define HIT5_BASE           0x200AU
/* 界面8 击中按钮 VP */
#define HIT8_BASE           0x2014U

/*==================== 游戏参数 ====================*/
#define TRACK_COUNT         4U
#define NOTES_PER_TRACK     15U
#define TOTAL_NOTES         (TRACK_COUNT * NOTES_PER_TRACK)
#define SUCCESS_THRESHOLD   60U

#define NOTE_START_Y        20U
#define NOTE_END_Y          220U
#define HIT_Y_MIN           200U
#define HIT_Y_MAX           235U
#define NOTE_HIDDEN_Y       0U
#define FALL_DIV            10U

#define MUSIC_LEN_MS        34000U

/*==================== 状态常量 ====================*/
#define STATUS_IDLE         0U
#define STATUS_PLAYING      1U
#define STATUS_ENDED        2U

#define JUDGE_NONE          0U
#define JUDGE_HIT           1U
#define JUDGE_MISS          3U

#define NOTE_PENDING        0U
#define NOTE_FALLING        1U
#define NOTE_DONE           2U

/*==================== 全局变量 ====================*/
static unsigned int  g_timer;
static unsigned int  g_hit_count;
static unsigned char g_last_page;
static unsigned char g_game_status;
static unsigned char g_current_game;   /* 当前游戏界面: PAGE_GAME_5 或 PAGE_GAME_8 */

/* 大数组放 xdata (T5L 内部DATA仅256B) */
static unsigned int  XDATA g_note_start[TRACK_COUNT][NOTES_PER_TRACK];
static unsigned char XDATA g_note_state[TRACK_COUNT][NOTES_PER_TRACK];
static unsigned int  XDATA g_note_y[TRACK_COUNT];

/*===========================================================================
 * 辅助函数：根据当前游戏页面获取动态地址
 *===========================================================================*/

/*
 * 获取音符Y坐标VP地址
 * 页面5: VP=0x2006 + track (0x2006~0x2009)
 * 页面8: VP=0x2010 + track (0x2010~0x2013)
 * 写入该VP的值即为变量图标的Y坐标
 */
static unsigned int get_note_y_addr(unsigned char track)
{
    unsigned int base;
    if (g_current_game == PAGE_GAME_5)
    {
        base = NOTE5_Y_BASE;
    }
    else /* PAGE_GAME_8 */
    {
        base = NOTE8_Y_BASE;
    }
    return base + (unsigned int)track;
}

/* 获取击中按钮VP地址 */
static unsigned int get_hit_btn_addr(unsigned char track)
{
    unsigned int base;
    if (g_current_game == PAGE_GAME_5)
    {
        base = HIT5_BASE;
    }
    else /* PAGE_GAME_8 */
    {
        base = HIT8_BASE;
    }
    return base + (unsigned int)track;
}

/* 获取底层转换变量VP地址 */
static unsigned int get_convert_addr(void)
{
    return (g_current_game == PAGE_GAME_5) ? VP_CONVERT_5 : VP_CONVERT_8;
}

/*===========================================================================
 * VP 读写封装
 *===========================================================================*/

static void vp_write16(unsigned int addr, unsigned int val)
{
    unsigned char buf[2];
    buf[0] = (unsigned char)(val >> 8);
    buf[1] = (unsigned char)(val & 0xFF);
    sys_write_vp(addr, buf, 2);
}

static unsigned int vp_read16(unsigned int addr)
{
    unsigned char buf[2];
    sys_read_vp(addr, buf, 2);
    return ((unsigned int)buf[0] << 8) | buf[1];
}

static void vp_write8(unsigned int addr, unsigned char val)
{
    sys_write_vp(addr, &val, 1);
}

static void delay_ms(unsigned int ms)
{
    sys_delay_ms(ms);
}

/*===========================================================================
 * 界面切换
 *===========================================================================*/

static void page_switch(unsigned char page_id)
{
    vp_write8(VP_PAGE_SWITCH, page_id);
}

static unsigned char page_get_current(void)
{
    unsigned char buf[1];
    sys_read_vp(VP_PAGE_ID, buf, 1);
    return buf[0];
}

/*===========================================================================
 * 界面转换变量管理
 *===========================================================================*/

static void convert_reset(void)
{
    /* 通电时确保两个底层变量都为0 (显示游戏界面) */
    vp_write16(VP_CONVERT_5, 0U);
    vp_write16(VP_CONVERT_8, 0U);
}

/*===========================================================================
 * 音符管理
 *===========================================================================*/

static void notes_reset(void)
{
    unsigned char track, note;
    for (track = 0; track < TRACK_COUNT; track++)
    {
        g_note_y[track] = NOTE_HIDDEN_Y;
        /* 写VP地址隐藏音符图标 (Y=0) */
        vp_write16(get_note_y_addr(track), NOTE_HIDDEN_Y);
        for (note = 0; note < NOTES_PER_TRACK; note++)
        {
            g_note_state[track][note] = NOTE_PENDING;
            g_note_start[track][note] = 0U;
        }
    }
}

/*
 * 生成不规则音符掉落时间表
 * 每条轨道15个音符，在34000ms音乐结束前全部掉落完毕
 * 使用多种扰动因子确保4条轨道音符不会整齐划一地下落
 */
static void notes_build_schedule(void)
{
    unsigned char track, note;
    unsigned int interval;
    unsigned int start_time;

    interval = MUSIC_LEN_MS / NOTES_PER_TRACK;  /* ~2266ms */

    for (track = 0; track < TRACK_COUNT; track++)
    {
        unsigned int offset = (unsigned int)track * 500U;

        for (note = 0; note < NOTES_PER_TRACK; note++)
        {
            start_time = 800U
                       + (unsigned int)note * interval
                       + offset
                       + (unsigned int)(note % 3U) * 150U
                       + (unsigned int)((note * 37U) % 200U);

            /* 确保最后一个音符在音乐结束前掉落完 */
            if (start_time > MUSIC_LEN_MS - 2500U)
            {
                start_time = MUSIC_LEN_MS - 2500U
                           - (unsigned int)(NOTES_PER_TRACK - note) * 100U;
            }

            /* 避免负数/过小值 */
            if (start_time < 100U)
            {
                start_time = 100U + note * 80U;
            }

            g_note_start[track][note] = start_time;
            g_note_state[track][note] = NOTE_PENDING;
        }
    }
}

/*===========================================================================
 * 击中按钮管理
 *===========================================================================*/

static void hit_buttons_reset(void)
{
    unsigned char track;
    for (track = 0; track < TRACK_COUNT; track++)
    {
        vp_write16(get_hit_btn_addr(track), 0U);
    }
}

/*===========================================================================
 * 核心游戏逻辑 (每1ms)
 *===========================================================================*/

/* 更新4条轨道的音符Y坐标 → 写入VP地址 → 屏幕上的音符往下掉 */
static void game_update_notes(void)
{
    unsigned char track, note;

    for (track = 0; track < TRACK_COUNT; track++)
    {
        unsigned char found_active = 0U;

        for (note = 0; note < NOTES_PER_TRACK; note++)
        {
            /* 激活到达预定时间的音符 */
            if (g_note_state[track][note] == NOTE_PENDING
                && g_timer >= g_note_start[track][note])
            {
                g_note_state[track][note] = NOTE_FALLING;
            }

            /* 处理正在下落的音符 */
            if (g_note_state[track][note] == NOTE_FALLING)
            {
                unsigned int elapsed = g_timer - g_note_start[track][note];
                unsigned int y = NOTE_START_Y + elapsed / FALL_DIV;

                if (y >= NOTE_END_Y)
                {
                    /* 超出底部 → 漏掉 */
                    g_note_state[track][note] = NOTE_DONE;
                    g_note_y[track] = NOTE_HIDDEN_Y;
                }
                else
                {
                    g_note_y[track] = y;
                    found_active = 1U;
                }

                /* ★ 核心衔接点: 写VP地址 → DGUS变量图标Y坐标变化 → 屏幕刷新 */
                vp_write16(get_note_y_addr(track), g_note_y[track]);
                break;  /* 每条轨道同时只有1个活跃音符 */
            }
        }

        if (found_active == 0U)
        {
            vp_write16(get_note_y_addr(track), NOTE_HIDDEN_Y);
        }
    }
}

/* 检测击中: 读底部4个返回控件VP → 判断Y坐标是否在命中区 */
static void game_check_hits(void)
{
    unsigned char track, note;
    unsigned int btn_val;

    for (track = 0; track < TRACK_COUNT; track++)
    {
        btn_val = vp_read16(get_hit_btn_addr(track));

        if (btn_val == 0U)
        {
            continue;
        }

        /* 按钮被按下 */
        {
            unsigned char hit_handled = 0U;

            for (note = 0; note < NOTES_PER_TRACK; note++)
            {
                if (g_note_state[track][note] == NOTE_FALLING)
                {
                    if (g_note_y[track] >= HIT_Y_MIN && g_note_y[track] <= HIT_Y_MAX)
                    {
                        /* ★ 命中! */
                        g_hit_count++;
                        g_note_state[track][note] = NOTE_DONE;
                        g_note_y[track] = NOTE_HIDDEN_Y;
                        vp_write16(get_note_y_addr(track), NOTE_HIDDEN_Y);
                        vp_write16(VP_JUDGE_RESULT, JUDGE_HIT);
                    }
                    else
                    {
                        /* 时机不对 → 失误 */
                        vp_write16(VP_JUDGE_RESULT, JUDGE_MISS);
                    }
                    hit_handled = 1U;
                    break;
                }
            }

            if (hit_handled == 0U)
            {
                /* 无活跃音符时误触 */
                vp_write16(VP_JUDGE_RESULT, JUDGE_MISS);
            }
        }

        /* 清空按钮状态，等待下次按下 */
        vp_write16(get_hit_btn_addr(track), 0U);
    }
}

/* 判断通关: 击中数 / 总数 >= 60% */
static unsigned char game_is_success(void)
{
    unsigned int percent;
    percent = (g_hit_count * 100U) / TOTAL_NOTES;
    return (percent >= SUCCESS_THRESHOLD) ? 1U : 0U;
}

/* 游戏结束: 统计结果 → 写底层变量图标触发界面转换 → 跳转成功/失败页 */
static void game_end(void)
{
    unsigned char success;
    unsigned char target_page;

    g_game_status = STATUS_ENDED;
    vp_write16(VP_GAME_STATUS, STATUS_ENDED);

    success = game_is_success();

    /*
     * ★ 核心衔接点: 修改底层变量图标的VP值
     *   =1 → DGUS自动切换为成功界面(6或9)
     *   =2 → DGUS自动切换为失败界面(7或10)
     */
    vp_write16(get_convert_addr(), success ? 1U : 2U);
    delay_ms(300U);

    /* 同时代码主动跳转，双保险 */
    if (g_current_game == PAGE_GAME_5)
    {
        target_page = success ? PAGE_SUCCESS_6 : PAGE_FAIL_7;
    }
    else
    {
        target_page = success ? PAGE_SUCCESS_9 : PAGE_FAIL_10;
    }
    page_switch(target_page);
}

/*===========================================================================
 * 游戏初始化 (进入界面5或8时调用)
 *===========================================================================*/

static void game_init(unsigned char page)
{
    g_current_game = page;
    g_timer        = 0U;
    g_hit_count    = 0U;
    g_game_status  = STATUS_PLAYING;

    convert_reset();
    notes_reset();
    notes_build_schedule();
    hit_buttons_reset();

    vp_write16(VP_GAME_STATUS,  STATUS_PLAYING);
    vp_write16(VP_GAME_TIMER,   0U);
    vp_write16(VP_SCORE,        0U);
    vp_write16(VP_JUDGE_RESULT, JUDGE_NONE);
}

/*===========================================================================
 * 游戏主循环
 *===========================================================================*/

static void game_loop(void)
{
    if (g_game_status != STATUS_PLAYING)
    {
        return;
    }

    g_timer++;
    vp_write16(VP_GAME_TIMER, g_timer);

    game_update_notes();    /* 更新4轨道音符Y坐标 */
    game_check_hits();      /* 检测击打 */

    if (g_timer >= MUSIC_LEN_MS)
    {
        game_end();
    }
}

/*===========================================================================
 * 界面切换检测
 *===========================================================================*/

static void page_change_handler(void)
{
    unsigned char now_page;

    now_page = page_get_current();

    if (now_page == g_last_page)
    {
        return;
    }

    g_last_page = now_page;

    if (now_page == PAGE_GAME_5 || now_page == PAGE_GAME_8)
    {
        /* 进入游戏界面 → 初始化新游戏 */
        game_init(now_page);
    }
    else
    {
        /* 离开游戏界面 → 清理状态 */
        if (g_game_status == STATUS_PLAYING)
        {
            g_game_status = STATUS_IDLE;
            convert_reset();
        }
    }
}

/*===========================================================================
 * 主函数
 *===========================================================================*/

void main(void)
{
    sys_init();

    g_timer         = 0U;
    g_hit_count     = 0U;
    g_game_status   = STATUS_IDLE;
    g_last_page     = 0xFFU;       /* 强制首次界面检测 */
    g_current_game  = PAGE_GAME_5;

    /* 通电时确保底层变量为0 → 显示游戏界面(非成功/失败) */
    convert_reset();

    delay_ms(50U);

    while (1)
    {
        page_change_handler();  /* 检测界面切换 */
        game_loop();            /* 运行游戏逻辑 */
        delay_ms(1U);           /* 1ms 循环周期 */
    }
}
