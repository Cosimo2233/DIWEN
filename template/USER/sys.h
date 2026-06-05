#ifndef __SYS_H__
#define __SYS_H__
#include "t5los8051.h"

//锟斤拷锟斤拷锟截讹拷锟斤拷
typedef unsigned char   u8;
typedef unsigned int    u16;
typedef unsigned long   u32;
typedef signed char     s8;
typedef signed int     s16;
typedef signed long     s32;

//锟斤拷锟脚癸拷锟疥定锟斤拷
#define	WDT_ON()				MUX_SEL|=0x02		//锟斤拷锟斤拷锟斤拷锟脚癸拷
#define	WDT_OFF()				MUX_SEL&=0xFD		//锟截闭匡拷锟脚癸拷
#define	WDT_RST()				MUX_SEL|=0x01		//喂锟斤拷

//系统锟斤拷频锟斤拷1ms锟斤拷时锟斤拷值锟斤拷锟斤拷
#define FOSC     				206438400UL
#define T1MS    				(65536-FOSC/12/1000)

//锟斤拷锟斤拷锟斤拷锟斤拷
void sys_init(void);
void sys_delay_about_ms(unsigned int ms);
void sys_delay_about_us(unsigned char us);
void sys_delay_ms(unsigned int ms);
void sys_read_vp(unsigned int addr, unsigned char* buf, unsigned int len);
void sys_write_vp(unsigned int addr, unsigned char* buf, unsigned int len);

void DWIN_Key_Process(unsigned char key);

#endif