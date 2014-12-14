/*
 * Wiring:
 *
 * Atmega MCU		hd44780 compat.
 * +----+ 		  +---------+
 * | P0 |-------| DB4     |
 * | P1 |-------| DB5     |
 * | P2 |-------| DB6     |
 * | P3 |-------| DB7     |
 * | P4 |-------| E       |
 * | P5 |-------| RW      |
 * | P6 |-------| RS (A0) |
 * +----+		    +---------+
 *
 */
#include <stdint.h>

#ifndef LCD_PORT_NAME
#error "LCD_PORT_NAME is undefined"
#endif

#ifndef LCD_NCHARS
#define LCD_NCHARS 16
#endif

#ifndef __CONCAT
#define __CONCATenate(left, right) left ## right
#define __CONCAT(left, right) __CONCATenate(left, right)
#endif

#define lcd_DDR __CONCAT(DDR, LCD_PORT_NAME)
#define lcd_PORT __CONCAT(PORT, LCD_PORT_NAME)
#define lcd_PIN __CONCAT(PIN, LCD_PORT_NAME)


void lcd_WriteCmd(uint8_t c);
void lcd_WriteData(uint8_t d);

void lcd_Init(void);
void lcd_Clear(void);
void lcd_Print(char *s);
//void lcd_PrintR(unsigned char *s);
