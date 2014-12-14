#include <avr/io.h>
#include <util/delay.h>
#include "lcd.h"

void lcd_GenStrobE (void) {
//  lcd_PORT &= 0xef; // 1 110 1111
//  _delay_us(1);
  lcd_PORT |= 0x10; // 0 001 0000
  _delay_us(1);
  lcd_PORT &= 0xef; // 1 110 1111
  _delay_us(1);
}

void lcd_WriteByte(uint8_t b, uint8_t cd) {
//wait for busy flag
//  lcd_PORT=0x0;
  lcd_DDR=0x70; // 0 111 0000
  lcd_PORT=0x20; // 0 010 0000
//  _delay_us(5);
  lcd_PORT |= 0x10; // 001 0000
	asm("nop");
//  _delay_us(1);
//  lcd_GenStrobE();
  while (lcd_PIN & 0x08);
  lcd_GenStrobE();
  lcd_GenStrobE();

  lcd_PORT=0;
  lcd_DDR=0x7f; // 0 111 1111
//  lcd_PORT = cd << 6; // 0 (cd)00 0000
//  _delay_us(5);
  lcd_PORT = (cd<<6) | (b >> 4);
  lcd_GenStrobE();
//  lcd_PORT &= 0xf0; // 1 111 0000
  lcd_PORT = (lcd_PORT&0xf0)|(b & 0x0f);
  lcd_GenStrobE();
  lcd_PORT=0x0;
}

void lcd_WriteCmd(uint8_t c) {
  lcd_WriteByte(c, 0);
}

void lcd_WriteData(uint8_t d) {
  lcd_WriteByte(d, 1);
}

void lcd_Init(void) {
  lcd_PORT=0;
  lcd_DDR=0x7f; // 0 111 1111

  _delay_ms(40);

  lcd_PORT = 3; // 0 000 0011
  lcd_GenStrobE();
  _delay_ms(5);

  lcd_GenStrobE();
  _delay_ms(1);

  lcd_GenStrobE();
  _delay_ms(1);
 
  lcd_PORT = 2;
  lcd_GenStrobE();
  _delay_ms(1);

  lcd_WriteCmd(0x28);
//  lcd_WriteCmd(0x0f); // blinking cursor
  lcd_WriteCmd(0x0c); // no cursor
  lcd_WriteCmd(0x01); // clear display, place cursor at left
  lcd_WriteCmd(0x06); // right shift cursor
}

void lcd_Clear(void) {
  lcd_WriteCmd(0x01);
}

void lcd_Print(char *s) {
  uint8_t i=0;
	uint8_t j=0;
  lcd_WriteCmd(0x02); // put cursor at left and shift display to the left
  lcd_WriteCmd(0x80); // set DDRAM address to 0
  lcd_WriteCmd(0x6);  // move cursor right
  while (s[i]) {
    if (j==LCD_NCHARS || s[i]=='\n') lcd_WriteCmd(0xc0);// goto next line, i.e. set DDRAM adress to 0x40
    if (s[i]=='\n') {i++;j=0;}
    else {lcd_WriteData(s[i++]); j++;}
  }
}
/*
void lcd_PrintR(unsigned char *s) {
  uint8_t i=0;
  uint8_t j=0;
  lcd_WriteCmd(0x02); // put cursor at left and shift display to the left
  while (s[i] && i < LCD_NCHARS) i++;
  lcd_WriteCmd(0xc0 + LCD_S2LEN-1); // set DDRAM address
  lcd_WriteCmd(0x4);  // move cursor left
  while (i > 0 ) {
    if ( j ==  LCD_S2LEN)
      lcd_WriteCmd(0x87); // set DDRAM adress to 7
    lcd_WriteData(s[i-1]);
    i--;
    j++;
  }
}*/
