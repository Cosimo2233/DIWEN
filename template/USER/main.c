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
 *    音符变量图标: VP=0x2006~09  SP=0x2100/10/20/30  Y坐标=SP+4
 *    击中返回控件: VP=0x200A~0D
 *    底层转换变量图标: VP=0x2004
 *
 *  界面8 (游戏关2):
 *    音符变量图标: VP=0x2010~13  SP=0x2200/10/20/30  Y坐标=SP+4
 *    击中返回控件: VP=0x2014~17
 *    底层转换变量图标: VP=0x2005
 *
 *  SP起始地址(SPADDRESS): 0x5000 (见 DWprj.hmi)
 *  实际Y坐标地址 = SPADDRESS + SP + 4
 *
 *  音频控制: VP=0x200E (值1=播放音频0, 值0=停止)
 *
 * ==================== 界面跳转流程 (DGUS独立完成) ====================
 *  界面4 → 界面5 + 播放音频0
 *  界面6 → 界面8 + 播放音频1
 *  界面7 → 界面5 + 播放音频0
 *  界面9 → 界面0
 *  界面10 → 界面8 + 播放音频1
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
#define VP_GAME_TIMER       0x2001U   /* 计时器 */
#define VP_SCORE            0x2002U   /* 得分 */
#define VP_JUDGE_RESULT     0x2003U   /* 判定反馈 */
#define VP_CONVERT_5        0x2004U   /* 界面5底层转换变量图标 */
#define VP_CONVERT_8        0x2005U   /* 界面8底层转换变量图标 */
#define VP_AUDIO_CTRL       0x200EU   /* 音频控制 */

/*
 * SP 起始地址 = SPADDRESS (见 DWprj.hmi 配置)
 * 音符Y坐标地址 = SPADDRESS + SP偏移 + 4
 */
#define SP_BASE             0x5000U

/* 界面5 音符 SP 偏移 (每个轨道间隔0x10) */
#define SP5_OFFSET          0x2100U
/* 界面8 音符 SP 偏移 (每个轨道间隔0x10) */
#define SP8_OFFSET          0x2200U

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

/* 获取音符Y坐标写入地址 = SPADDRESS + SP偏移 + 4(Y坐标在SP中的位置) + 轨道*0x10 */
static unsigned int get_note_y_addr(unsigned char track)
{
    unsigned int sp_offs;
    if (g_current_game == PAGE_GAME_5)
    {
        sp_offs = SP5_OFFSET;
    }
    else /* PAGE_GAME_8 */
    {
        sp_offs = SP8_OFFSET;
    }
    /* SP+4 = Y坐标字段在描述指针中的偏移 */
    return SP_BASE + sp_offs + 4U + (unsigned int)track * 0x10U;
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
 * 音频控制
 *===========================================================================*/

static void audio_play(unsigned char audio_id)
{
    /*
     * 写入 VP_AUDIO_CTRL，DGUS 根据值播放对应音频:
     *   值=1 → 音频0 (游戏背景音乐)
     *   值=2 → 音频1 (过渡音效)
     *   值=0 → 停止
     * 如果你 DGUS 的音频映射不同，调整这里的值即可
     */
    vp_write16(VP_AUDIO_CTRL, (unsigned int)audio_id + 1U);
}

static void audio_stop(void)
{
    vp_write16(VP_AUDIO_CTRL, 0x0000U);
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
        /* 写SP+4地址来隐藏音符图标 */
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

/* 更新4条轨道的音符Y坐标 → 写入SP+4地址 → 屏幕上的音符往下掉 */
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

                /* ★ 核心衔接点: 写SP+4地址 → DGUS变量图标Y坐标变化 → 屏幕刷新 */
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

    audio_stop();
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

    /* 播放游戏音乐 (音频0) */
    audio_play(0U);
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
        /* 离开游戏界面 → 清理 */
        if (g_game_status == STATUS_PLAYING)
        {
            g_game_status = STATUS_IDLE;
            audio_stop();
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
