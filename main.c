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

volatile uint32_t delay;
volatile uint8_t update;

unsigned char bmp[128 * 64 / 8];
volatile int bmp_pos, bmp_len;
volatile char glcd_update;

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
	
	/* receive bitmap */
	case 4:
		bmp_pos = 0;
		bmp_len = rq->wLength.word;
		return USB_NO_MSG;

	default:
		break;
	}
	return 0;
}

unsigned char usbFunctionWrite(unsigned char *data, unsigned char len)
{
	int i;
	for (i = 0; i < len; i++) {
		if (bmp_pos >= 128 * 64 / 8)
			break;
		bmp[bmp_pos++] = data[i];
	}

	if (bmp_pos == 128 * 64 / 8 || bmp_pos == bmp_len) {
		glcd_update = 1;
		return 1;
	}

	return 0;
}

void main(void)
{
	int msg_len, shift, len, msg_shift;
	char lcd_buff[LCD_LEN+1];
	int i, j, k, l, x, y;

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
	memset(bmp, 0, 128 * 64 / 8);
	for (i = 0; i < 64 / 8; i++) {
		for (j = 0; j < 128 / 8; j++) {
			if (i % 2 == 0 && j % 2 == 0
				|| i % 2 == 1 && j % 2 == 1) {
				for (k = 0; k < 8; k++) {
					for (l = 0; l < 8; l++) {
						x = 8 * j + k;
						y = 8 * i + l;
						set_pixel_buf(bmp, x, y);
	}}}}}
	GLCD_Bitmap(bmp, 0, 0, 128, 64);
	glcd_update = 0;

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

		if (glcd_update) {
			GLCD_Bitmap(bmp, 0, 0, 128, 64);
			glcd_update = 0;
		}
		usbPoll();
	}
}
