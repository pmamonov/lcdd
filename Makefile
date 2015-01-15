DEVICE  = atmega32
F_CPU   = 12000000ull

CC = avr-gcc
CFLAGS = -Wall -O3 -mmcu=$(DEVICE) -DF_CPU=$(F_CPU) \
 -DLCD_PORT_NAME=C -DLCD_NCHARS=8\
 -Iusbdrv

OBJ = main.o
OBJ += usbdrv/usbdrv.o usbdrv/usbdrvasm.o usbdrv/oddebug.o
OBJ += lcd.o
OBJ += graphic.o KS0108.o KS0108-AVR.o 

main.bin: main.elf
	rm -f main.hex
	avr-objcopy -R .eeprom -O binary main.elf main.bin
	avr-size main.elf

main.elf: $(OBJ)
	$(CC) $(CFLAGS) -o main.elf -Wl,-Map,main.map $(OBJ) -Wl,-u,vfprintf -lprintf_flt -lm
#	$(CC) $(CFLAGS) -o main.elf -Wl,-Map,main.map,-u,vfprintf -lprintf_min $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

.S.o:
	$(CC) $(CFLAGS) -x assembler-with-cpp -c $< -o $@

clean:
	rm -f $(OBJ) main.elf main.hex main.bin

load:
	avrdude -P usb -c usbasp -p m32 -U flash:w:main.bin:r -F

fuse:
	avrdude -P usb -c usbasp -p m32 -U lfuse:w:lfuse.bin:r -U hfuse:w:hfuse.bin:r

