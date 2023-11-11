TARGET = firmware

ENABLE_FMRADIO := 1
ENABLE_SWD := 0
ENABLE_UART := 1
# Broken above -O1 on stock due to not enough GPIO pin delays (driver/gpio.c)
# https://stackoverflow.com/questions/50175117/what-gets-discarded-by-gccs-flto
ENABLE_LTO := 1

BSP_DEFINITIONS := $(wildcard hardware/*/*.def)
BSP_HEADERS := $(patsubst hardware/%,bsp/%,$(BSP_DEFINITIONS))
BSP_HEADERS := $(patsubst %.def,%.h,$(BSP_HEADERS))

OBJS =
# Startup files
OBJS += start.o
OBJS += init.o
OBJS += external/printf/printf.o

# Drivers
OBJS += driver/adc.o
ifeq ($(ENABLE_UART),1)
OBJS += driver/aes.o
endif
OBJS += driver/backlight.o
ifeq ($(ENABLE_FMRADIO),1)
OBJS += driver/bk1080.o
endif
OBJS += driver/bk4819.o
ifeq ($(ENABLE_UART),1)
OBJS += driver/crc.o
endif
OBJS += driver/eeprom.o
OBJS += driver/gpio.o
OBJS += driver/i2c.o
OBJS += driver/keyboard.o
OBJS += driver/spi.o
OBJS += driver/st7565.o
OBJS += driver/system.o
OBJS += driver/systick.o
ifeq ($(ENABLE_UART),1)
OBJS += driver/uart.o
endif

# Main
OBJS += app/action.o
OBJS += app/app.o
OBJS += app/dtmf.o
ifeq ($(ENABLE_FMRADIO),1)
OBJS += app/fm.o
endif
OBJS += app/generic.o
OBJS += app/main.o
OBJS += app/menu.o
OBJS += app/scanner.o
ifeq ($(ENABLE_UART),1)
OBJS += app/uart.o
endif
OBJS += bitmaps.o
OBJS += board.o
OBJS += dcs.o
OBJS += font.o
OBJS += frequencies.o
OBJS += functions.o
OBJS += helper/battery.o
OBJS += helper/boot.o
OBJS += mdc1200.o
OBJS += misc.o
OBJS += radio.o
OBJS += scheduler.o
OBJS += settings.o
OBJS += ui/battery.o
ifeq ($(ENABLE_FMRADIO),1)
OBJS += ui/fmradio.o
endif
OBJS += ui/helper.o
OBJS += ui/inputbox.o
OBJS += ui/lock.o
OBJS += ui/main.o
OBJS += ui/menu.o
OBJS += ui/rssi.o
OBJS += ui/scanner.o
OBJS += ui/status.o
OBJS += ui/ui.o

OBJS += main.o

ifeq ($(OS),Windows_NT)
TOP := $(dir $(realpath $(lastword $(MAKEFILE_LIST))))
else
TOP := $(shell pwd)
endif

AS = arm-none-eabi-gcc
CC = arm-none-eabi-gcc
LD = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE = arm-none-eabi-size

GIT_HASH := $(shell git rev-parse --short HEAD)

ASFLAGS = -c -mcpu=cortex-m0
CFLAGS = -Wall -Wextra -mcpu=cortex-m0 -pipe -std=c11 -MMD
CFLAGS += -Os -Wpadded -free -freorder-blocks-algorithm=stc
CFLAGS += -DPRINTF_INCLUDE_CONFIG_H
CFLAGS += -DGIT_HASH=\"$(GIT_HASH)\"

ifeq ($(ENABLE_FMRADIO),1)
CFLAGS += -DENABLE_FMRADIO
endif
ifeq ($(ENABLE_LTO),0)
CFLAGS += -ffunction-sections -fdata-sections
else
CFLAGS += -flto=2
endif
ifeq ($(ENABLE_SWD),1)
CFLAGS += -DENABLE_SWD
endif
ifeq ($(ENABLE_UART),1)
CFLAGS += -DENABLE_UART
endif

LDFLAGS = -mcpu=cortex-m0 -nostartfiles -Wl,-T,firmware.ld
LDFLAGS += --specs=nano.specs
ifeq ($(ENABLE_LTO),0)
LDFLAGS += -Wl,--gc-sections
endif

ifeq ($(DEBUG),1)
ASFLAGS += -g
CFLAGS += -g
LDFLAGS += -g
endif

INC =
INC += -I $(TOP)
INC += -I $(TOP)/external/CMSIS_5/CMSIS/Core/Include/
INC += -I $(TOP)/external/CMSIS_5/Device/ARM/ARMCM0/Include

LIBS =

DEPS = $(OBJS:.o=.d)

all: $(TARGET)
	$(OBJCOPY) -O binary $< $<.bin
	-python fw-pack.py $<.bin $(GIT_HASH) $<.packed.bin
	-python3 fw-pack.py $<.bin $(GIT_HASH) $<.packed.bin
	$(SIZE) $<

debug:
	/opt/openocd/bin/openocd -c "bindto 0.0.0.0" -f interface/jlink.cfg -f dp32g030.cfg

flash:
	/opt/openocd/bin/openocd -c "bindto 0.0.0.0" -f interface/jlink.cfg -f dp32g030.cfg -c "write_image firmware.bin 0; shutdown;"

version.o: .FORCE

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) $^ -o $@ $(LIBS)

bsp/dp32g030/%.h: hardware/dp32g030/%.def

%.o: %.c | $(BSP_HEADERS)
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

%.o: %.S
	$(AS) $(ASFLAGS) $< -o $@

.FORCE:

-include $(DEPS)

clean:
	rm -f $(TARGET).bin $(TARGET).packed.bin $(TARGET) $(OBJS) $(DEPS)

