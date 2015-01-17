#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "usbdrv.h"
#include "lcd.h"
#include "KS0108.h"
#include "graphic.h"

#define F_TIM1 1000ull
#define LCD_LEN 10
#define MSG_LEN 100

uint32_t tim;
char msg[MSG_LEN+1] = "quick brown fox jumps over a lazy dog";

uint32_t delay;
uint8_t update;

unsigned char bmp[128 * 64 / 8];

inline void set_pixel_buf(char *buf, unsigned char x, unsigned char y)
{	if (x >= 128 || y >= 64)
		return;
	buf[x + 128 * (y / 8)] |= 1 << (y % 8);
}

ISR(TIMER1_OVF_vect)
{
	TCNT1 = 0x10000ull - F_CPU / F_TIM1;
	if (tim)
		tim--;
}

usbMsgLen_t usbFunctionSetup(uchar data[8])
{
	usbRequest_t *rq = (void *)data;
	switch (rq->bRequest) {
	/* set scrolling delay */
	case 0:
		delay = (uint32_t)rq->wValue.word;
		update = 1;
		break;

	/* set text */
	case 1:
		if (rq->wIndex.word < MSG_LEN)
			msg[rq->wIndex.word] = rq->wValue.word;
		update = 1;
		break;

	/* set pixel */
	case 2:
		GLCD_SetPixel(rq->wValue.bytes[0],
					  rq->wValue.bytes[1],
					  1);
		break;

	/* clear screen */
	case 3:
		GLCD_ClearScreen();
		break;

	default:
		break;
	}
	return 0;
}

void main(void)
{
	int msg_len, shift, len, msg_shift;
	char lcd_buff[LCD_LEN+1];

	delay = 1000;
	tim = delay;
	shift = -LCD_LEN;
	msg[MSG_LEN] = 0;

	TCCR1B |= 1 << CS10;
	TIMSK |= 1 << TOIE1;

	lcd_Init();
	lcd_Print("0123456789");

	GLCD_Initialize();
	GLCD_ClearScreen();
	GLCD_GoTo(10,10);
	GLCD_Circle(64, 32, 10);

	usbInit();
	usbDeviceDisconnect();
	_delay_ms(250);
	usbDeviceConnect();

	sei();


	while (1) {
		if (delay > 0 && tim == 0) {
			tim = delay;

			msg_len = strlen(msg);

			if (shift >= msg_len)
				shift = -LCD_LEN;

			msg_shift = shift < 0 ? 0 : shift;

			if (shift < 0)
				len = LCD_LEN + shift;
			else
				len = LCD_LEN;
			
			if (len > msg_len - msg_shift)
				len = msg_len - msg_shift;

			memset(lcd_buff, ' ', LCD_LEN);
			memcpy(&lcd_buff[shift < 0 ? -shift : 0],
				   &msg[msg_shift], len);
			lcd_buff[LCD_LEN] = 0;
			lcd_Print(lcd_buff);
			shift++;
		}
		if (delay == 0 && update) {
			update = 0;
			lcd_Print(msg);
		}
		usbPoll();
	}
}
