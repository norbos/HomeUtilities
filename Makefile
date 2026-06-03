# AVR project Makefile for ATmega328P

MCU = atmega328p
F_CPU = 16000000UL
CC = avr-gcc
OBJCOPY = avr-objcopy
CFLAGS = -std=gnu11 -Os -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Iinclude
LDFLAGS = -mmcu=$(MCU)

SRC = src/main.c
OBJ = $(SRC:.c=.o)
TARGET = firmware

all: $(TARGET).hex

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJ)
	$(CC) $(LDFLAGS) $^ -o $@

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

flash: $(TARGET).hex
	@echo "Update PORT and PROGRAMMER in the Makefile before flashing."
	avrdude -p $(MCU) -c arduino -P /dev/ttyACM0 -b 115200 -D -U flash:w:$(TARGET).hex:i

clean:
	rm -f $(OBJ) $(TARGET).elf $(TARGET).hex

.PHONY: all flash clean
