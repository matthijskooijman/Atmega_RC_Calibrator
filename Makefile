# Makefile to compile a simple AVR program
#
# Copyright 2014 Matthijs Kooijman <matthijs@stdin.nl>
#
# Permission is hereby granted, free of charge, to anyone obtaining a
# copy of this document to do whatever they want with them without any
# restriction, including, but not limited to, copying, modification and
# redistribution.
#
# NO WARRANTY OF ANY KIND IS PROVIDED.
#
# To compile, just make sure that avr-gcc and friends are in your path
# and type "make".
#
PRG            = Calibrator
OBJ            = Calibrator.o
MCU            = atmega328p
DEFS           = -DF_CPU=8000000UL
OPTIMIZE       = -Os
#DEBUG          = 1

CFLAGS        = $(DEFS) $(OPTIMIZE)
CFLAGS       += -fdata-sections -fpack-struct -fshort-enums -g3 -Wall -pedantic -mmcu=$(MCU)
CFLAGS       += -std=gnu99 -fno-strict-aliasing -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith -mrelax
LDFLAGS      += -mmcu=$(MCU) -Wl,--relax

CC             = avr-gcc
OBJCOPY        = avr-objcopy
OBJDUMP        = avr-objdump
AVRSIZE        = avr-size

ifdef DEBUG
OBJ += uart.o
CFLAGS += -DDEBUG
endif


all: hex

lst:  $(PRG).lst

hex:  $(PRG).hex

clean:
	rm -rf $(OBJ) $(OBJ:.o=.d) $(PRG).elf $(PRG).hex $(PRG).lst $(PRG).map

$(PRG).elf: $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ) $(LIBS)
	$(AVRSIZE) -C $@

%.o: %.c Makefile
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

%.lst: %.elf
	$(OBJDUMP) -h -S $< > $@

%.hex: %.elf
	$(OBJCOPY) -j .text -j .data -O ihex $< $@


.PHONY: all lst hex clean

# pull in dependency info for *existing* .o files
-include $(OBJ:.o=.d)
