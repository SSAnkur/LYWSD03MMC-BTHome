# LYWSD03MMC B1.5 BTHome v2 beacon + LCD
#
# Builds against the Telink TLSR825x BLE SDK as shipped in
# pvvx/ATC_MiThermometer, reusing its startup code, linker script and
# post-build scripts:
#   git clone https://github.com/pvvx/ATC_MiThermometer ../ATC_MiThermometer
#   make ATC_PATH=../ATC_MiThermometer
# TC32 toolchain: $(TEL_PATH)/tools/linux/tc32/bin (or set TC32_PATH)

PROJECT_NAME ?= LYWSD03MMC-BTHome
ATC_PATH     ?= ../ATC_MiThermometer
TEL_PATH     ?= $(ATC_PATH)/SDK
TC32_PATH    ?= $(TEL_PATH)/tools/linux/tc32/bin/
PYTHON       ?= python3

SRC_PATH := ./src
OUT_PATH := ./out
ATC_SRC  := $(ATC_PATH)/src

ifeq ($(wildcard $(TEL_PATH)/components/drivers/8258/gpio_8258.c),)
$(error SDK not found: set ATC_PATH or TEL_PATH)
endif

CC  := $(TC32_PATH)tc32-elf-gcc
LD  := $(TC32_PATH)tc32-elf-ld
OBJCOPY := $(TC32_PATH)tc32-elf-objcopy
NM  := $(TC32_PATH)tc32-elf-nm

CFLAGS := -DCHIP_TYPE=CHIP_TYPE_8258 -DMCU_STARTUP_825X \
	-ffunction-sections -fdata-sections -Wall -O2 -fpack-struct \
	-fshort-enums -finline-small-functions -std=gnu99 -funsigned-char \
	-fshort-wchar -fms-extensions \
	-I$(TEL_PATH)/components -I$(SRC_PATH)

APP_SRCS := main.c ble.c i2c.c sensor.c battery.c lcd.c
DRV_SRCS := adc.c analog.c bsp.c clock.c flash.c gpio_8258.c i2c.c lpc.c rf_pa.c timer.c uart.c
COM_SRCS := string.c utility.c

OBJS := \
	$(OUT_PATH)/boot/cstartup_825x.o \
	$(OUT_PATH)/boot/div_mod.o \
	$(addprefix $(OUT_PATH)/app/,$(APP_SRCS:.c=.o)) \
	$(addprefix $(OUT_PATH)/drv/,$(DRV_SRCS:.c=.o)) \
	$(addprefix $(OUT_PATH)/common/,$(COM_SRCS:.c=.o)) \
	$(OUT_PATH)/vendor/blt_common.o

ELF := $(OUT_PATH)/$(PROJECT_NAME).elf
BIN := $(PROJECT_NAME).bin

all: $(BIN)

$(OUT_PATH)/app/%.o: $(SRC_PATH)/%.c $(wildcard $(SRC_PATH)/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT_PATH)/drv/%.o: $(TEL_PATH)/components/drivers/8258/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT_PATH)/common/%.o: $(TEL_PATH)/components/common/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT_PATH)/vendor/%.o: $(TEL_PATH)/components/vendor/common/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT_PATH)/boot/%.o: $(ATC_SRC)/%.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# Two-pass link: the second pass places code after the retention RAM area
$(ELF): $(OBJS)
	$(LD) --gc-sections -L $(TEL_PATH)/components/proj_lib -T $(ATC_SRC)/boot.link \
		-o $@ $(OBJS) -llt_8258
	$(LD) --gc-sections -Ttext `$(PYTHON) $(ATC_SRC)/TlsrRetMemAddr.py -e $@ -t $(NM)` \
		-L $(TEL_PATH)/components/proj_lib -T $(ATC_SRC)/boot.link \
		-o $@ $(OBJS) -llt_8258
	@$(PYTHON) $(ATC_SRC)/TlsrMemInfo.py -s 32768 -t $(NM) $@

# Raw image + Telink CRC (required by the OTA bootloader)
$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) $(ATC_PATH)/utils/tl_check_fw.py $@

clean:
	rm -rf $(OUT_PATH) $(BIN)

.PHONY: all clean
