#include "sys.h"

static unsigned int delay_tick = 0;

//���ļĴ�����ʼ��
void sys_cpu_init()
{
	EA = 0;
	RS0 = 0;
	RS1 = 0;

	CKCON = 0x00;
	T2CON = 0x70;
	DPC = 0x00;
	PAGESEL = 0x01;
	D_PAGESEL = 0x02;
	MUX_SEL = 0x00;
	RAMMODE = 0x00;
	PORTDRV = 0x01;
	IEN0 = 0x00;
	IEN1 = 0x00;
	IEN2 = 0x00;
	IP0 = 0x00;
	IP1 = 0x00;

	WDT_OFF();
}

//��ʱ��2��ʼ��,��ʱ����?ms
void sys_timer2_init()
{
	T2CON = 0x70;
	TH2 = 0x00;
	TL2 = 0x00;

	TRL2H = 0xBC;
	TRL2L = 0xCD;

	IEN0 |= 0x20;
	TR2 = 0x01;
	EA = 1;
}

//ϵͳ��ʼ��
void sys_init()
{
	sys_cpu_init();
	sys_timer2_init();
}

//����������ʱ,��λms
void sys_delay_about_ms(unsigned int ms)
{
	unsigned int i, j;
	for (i = 0; i < ms; i++)
		for (j = 0; j < 3000; j++);
}

//����������ʱ,��λus
void sys_delay_about_us(unsigned char us)
{
    unsigned char i, j;
    for (i = 0; i < us; i++)
        for (j = 0; j < 5; j++);
}

//���ö�ʱ��2���о�ȷ��ʱ,��λms
void sys_delay_ms(unsigned int ms)
{
    delay_tick = ms;
    while (delay_tick);
}

//��DGUS�е�VP��������
void sys_read_vp(unsigned int addr, unsigned char* buf, unsigned int len)
{
    unsigned char i;

    i = (unsigned char)(addr & 0x01);
	addr >>= 1;
	ADR_H = 0x00;
	ADR_M = (unsigned char)(addr >> 8);
	ADR_L = (unsigned char)addr;
	ADR_INC = 0x01;
	RAMMODE = 0xAF;
	while (APP_ACK == 0);
	while (len > 0)
	{
		APP_EN = 1;
		while (APP_EN == 1);
		if ((i == 0) && (len > 0))
		{
			*buf++ = DATA3;
			*buf++ = DATA2;
			i = 1;
			len--;
		}
		if ((i == 1) && (len > 0))
		{
			*buf++ = DATA1;
			*buf++ = DATA0;
			i = 0;
			len--;
		}
	}
	RAMMODE = 0x00;
}

//дDGUS�е�VP��������
void sys_write_vp(unsigned int addr, unsigned char* buf, unsigned int len)
{
    unsigned char i;

    i = (unsigned char)(addr & 0x01);
	addr >>= 1;
	ADR_H = 0x00;
	ADR_M = (unsigned char)(addr >> 8);
	ADR_L = (unsigned char)addr;
	ADR_INC = 0x01;
	RAMMODE = 0x8F;
	while (APP_ACK == 0);
	if (i && len > 0)
	{
		RAMMODE = 0x83;
		DATA1 = *buf++;
		DATA0 = *buf++;
		APP_EN = 1;
		len--;
	}
	RAMMODE = 0x8F;
	while (len >= 2)
	{
		DATA3 = *buf++;
		DATA2 = *buf++;
		DATA1 = *buf++;
		DATA0 = *buf++;
		APP_EN = 1;
		len -= 2;
	}
	if (len)
	{
		RAMMODE = 0x8C;
		DATA3 = *buf++;
		DATA2 = *buf++;
		APP_EN = 1;
	}
	RAMMODE = 0x00;
}

//��ʱ��2�жϷ������?
#ifdef __C51__
void sys_timer2_isr()    interrupt 5
#else
void sys_timer2_isr(void)
#endif
{
	TF2 = 0;

	if (delay_tick)
		delay_tick--;
}

// ==================== ��������·ѡ���� ====================
#define DWIN_TEXT_ADDR    0x1000    // ��ʾ��·�ı���ַ
#define DWIN_KEY_ADDR     0x2000    // �������յ�ַ���û�������ȷ��

const unsigned char LINE_1[] = { 0xC1,0xDF,0xCF,0xDF };  // 1����
const unsigned char LINE_2[] = { 0xB6,0xFE,0xCF,0xDF };  // 2����

void DWIN_Set_Line(const unsigned char* line)
{
	sys_write_vp(DWIN_TEXT_ADDR, (unsigned char*)line, 4);
}

void DWIN_Goto_Main(void)
{
	unsigned char buf[1] = { 0x00 };
	sys_write_vp(0x0084, buf, 1);
}

void DWIN_Key_Process(unsigned char key)
{
	if (key == 0x01)
	{
		DWIN_Set_Line(LINE_1);
		DWIN_Goto_Main();
	}
	else if (key == 0x02)
	{
		DWIN_Set_Line(LINE_2);
		DWIN_Goto_Main();
	}
}
