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

uint32_t tim, tim1;
char msg[MSG_LEN+1] = "quick brown fox jumps over a lazy dog";

uint32_t delay;
uint8_t update;

unsigned char bmp[128 * 64 / 8];

inline void set_pixel_buf(char *buf, unsigned char x, unsigned char y)
{	if (x >= 128 || y >= 64)
		return;
	buf[x + 128 * (y / 8)] |= 1 << (y % 8);
}

struct star {
	char x;
	char y;
};

#define NUM_STARS 50
struct star stars[NUM_STARS];

ISR(TIMER1_OVF_vect)
{
	TCNT1 = 0x10000ull - F_CPU / F_TIM1;
	if (tim)
		tim--;
	if (tim1)
		tim1--;
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

	memset(stars, 0, sizeof(stars));
	srand(42);

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

		if (!tim1) {
			tim1 = 100;
			memset(bmp, 0, 128 * 64 / 8);
			for (i = 0; i < NUM_STARS; i++) {
				if (!(stars[i].x == 0 && stars[i].y == 0)) {
					x = abs(stars[i].x) > abs(stars[i].y) ?
						abs(stars[i].y) : abs(stars[i].x);
					if (x == 0)
						x = abs(stars[i].x) < abs(stars[i].y) ?
							abs(stars[i].y) : abs(stars[i].x);
					stars[i].x += stars[i].x / x;
					stars[i].y += stars[i].y / x;
				}
				if (stars[i].x >= 64 ||
					stars[i].x < -64 ||
					stars[i].y >= 32 ||
					stars[i].y < -32 ||
					stars[i].x == 0 && stars[i].y == 0) {
						stars[i].x = rand() % 20 - 10;
						stars[i].y = rand() % 20 - 10;
					}
				set_pixel_buf(bmp, stars[i].x + 128 / 2, stars[i].y + 64 / 2);
				set_pixel_buf(bmp, stars[i].x + 1 + 128 / 2, stars[i].y + 64 / 2);
				set_pixel_buf(bmp, stars[i].x + 128 / 2, stars[i].y + 1 + 64 / 2);
				set_pixel_buf(bmp, stars[i].x + 1 + 128 / 2, stars[i].y + 1 + 64 / 2);
			}
			GLCD_Bitmap(bmp, 0, 0, 128, 64);
		}

		usbPoll();
	}
}
