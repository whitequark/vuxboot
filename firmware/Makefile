# START CONFIGURATION
CPU=mega8
F_CPU=2000000
BAUD=115200
# END CONFIGURATION

include Makefile.$(CPU)

ifeq ($(EEPROM),1)
  CCONFIG += -DEEPROM -DE_EEPROM_BYTES=$(EEPROM_BYTES)
endif
ifneq ($(UBRR),)
  CCONFIG += -DUSER_UBRR=$(UBRR)
  ifneq ($(U2X),)
    CCONFIG += -DUSE_2X
  endif
endif

OBJECTS = main.S
CC      = avr-gcc
CFLAGS  = $(CCONFIG) -DBOOT_BYTE=$(BOOT_BYTE) -DE_PAGE_WORDS=$(PAGE_WORDS) \
   -DE_FLASH_PAGES=$(FLASH_PAGES) -DBOOT_PAGES=$(BOOT_PAGES) -Os -g -Wall \
   -Wl,-e,entry -nostartfiles -DF_CPU=$(F_CPU) -DBAUD=$(BAUD)

all:	firmware.hex

clean:
	rm -f *.o firmware.elf firmware.hex

firmware.elf: Makefile $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $(OBJECTS)

firmware.hex: firmware.elf
	rm -f firmware.hex
	avr-objcopy -j .text -j .data -O ihex firmware.elf firmware.hex
	avr-size firmware.elf
	@echo
	@echo "  <==== NOTES ====>"
	@echo "    "$(INFO)
	@echo

flash:
	avrdude $(DUDECONFIG) -c $(PROG) -U flash:w:firmware.hex
