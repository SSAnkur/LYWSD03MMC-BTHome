# LYWSD03MMC B1.5 BTHome v2 beacon + LCD + OTA
#
# Builds against the official Telink BLE SDK for TLSR825x (B85m):
#   git clone -b release/V3.4.3.0 https://github.com/telink-semi/tc_ble_single_sdk ../tc_ble_single_sdk
#   (apply the patches in tc_ble_single_sdk_v3.4.3.0_Patch/ as Patch_Guide.txt says)
#   make TC32_PATH=/path/to/tc32/bin/
#
# Same compiler flags, startup code, linker script, libraries and post-build
# step as the SDK's own 825x_ble_sample project (project/tlsr_tc32/B85/.cproject).
# The SDK itself is used unmodified.

PROJECT_NAME ?= LYWSD03MMC-BTHome
SDK_PATH     ?= ../tc_ble_single_sdk/tc_ble_single_sdk
TC32_PATH    ?=

SRC_PATH := ./src
OUT_PATH := ./out

ifneq ($(MAKECMDGOALS),clean)
ifeq ($(wildcard $(SDK_PATH)/boot/B85/cstartup_825x.S),)
$(error Telink SDK not found: set SDK_PATH to the tc_ble_single_sdk/tc_ble_single_sdk directory)
endif
endif

CC      := $(TC32_PATH)tc32-elf-gcc
LD      := $(TC32_PATH)tc32-elf-ld
OBJCOPY := $(TC32_PATH)tc32-elf-objcopy
NM      := $(TC32_PATH)tc32-elf-nm

# TLSR8251: 32 KiB SRAM (stack top 0x848000)
STARTUP := MCU_STARTUP_8251

INCLUDES := -I$(SDK_PATH) -I$(SDK_PATH)/vendor/common -I$(SDK_PATH)/common \
	-I$(SDK_PATH)/drivers/B85 -I$(SRC_PATH)

# -include: the SDK selects app_config.h only for its own demo projects
# (vendor/common/user_config.h), so ours is forced into every file
CFLAGS := -DCHIP_TYPE=CHIP_TYPE_825x -D$(STARTUP)=1 \
	-ffunction-sections -fdata-sections -Wall -O2 -fpack-struct \
	-fshort-enums -finline-small-functions -std=gnu99 \
	-fshort-wchar -fms-extensions \
	$(INCLUDES) -include $(SRC_PATH)/app_config.h

ASFLAGS := -D$(STARTUP)=1 $(INCLUDES)

APP_SRCS := $(wildcard $(SRC_PATH)/*.c)

# Everything the SDK's ble_sample compiles from the SDK (minus USB/audio
# code, which this device has no use for); --gc-sections keeps only what
# is referenced
SDK_DIRS := drivers/B85 drivers/B85/driver_ext drivers/B85/flash common vendor/common
SDK_SRCS := $(foreach d,$(SDK_DIRS),$(wildcard $(SDK_PATH)/$(d)/*.c))
SDK_ASMS := $(SDK_PATH)/boot/B85/cstartup_825x.S $(SDK_PATH)/common/div_mod.S

OBJS := $(patsubst $(SRC_PATH)/%.c,$(OUT_PATH)/app/%.o,$(APP_SRCS)) \
	$(patsubst $(SDK_PATH)/%.c,$(OUT_PATH)/sdk/%.o,$(SDK_SRCS)) \
	$(patsubst $(SDK_PATH)/%.S,$(OUT_PATH)/sdk/%.o,$(SDK_ASMS))

LDSCRIPT := $(SDK_PATH)/project/tlsr_tc32/B85/boot.link
LIBS     := -L$(SDK_PATH)/proj_lib -llt_825x -llt_general_stack

ELF := $(OUT_PATH)/$(PROJECT_NAME).elf
BIN := $(PROJECT_NAME).bin

# Post-build: the SDK's check_fw (Linux x86-64 binary) pads the image, sets
# the size and marker in the header and appends the CRC the OTA server
# checks. Copied to out/ because the SDK checkout does not mark it executable.
CHECK_FW = cp $(SDK_PATH)/script/tl_check_fw/check_fw $(OUT_PATH)/check_fw && \
	chmod +x $(OUT_PATH)/check_fw && $(OUT_PATH)/check_fw $(BIN)

all: $(BIN)

$(OUT_PATH)/app/%.o: $(SRC_PATH)/%.c $(wildcard $(SRC_PATH)/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT_PATH)/sdk/%.o: $(SDK_PATH)/%.c $(SRC_PATH)/app_config.h
	@mkdir -p $(dir $@)
	@echo CC $<
	@$(CC) $(CFLAGS) -c -o $@ $<

$(OUT_PATH)/sdk/%.o: $(SDK_PATH)/%.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c -o $@ $<

$(ELF): $(OBJS)
	$(LD) --gc-sections -T $(LDSCRIPT) -o $@ $(OBJS) $(LIBS)
	@$(NM) $@ | awk ' \
		function hex(s,  i, n) { n = 0; for (i = 1; i <= length(s); i++) \
			n = n * 16 + index("0123456789abcdef", substr(tolower(s), i, 1)) - 1; return n } \
		$$3 == "_rstored_" { r = hex($$1) } \
		$$3 == "_retention_size_" { t = hex($$1) } \
		$$3 == "_bin_size_" { b = hex($$1) } \
		$$3 == "_ram_use_end_" { e = hex($$1) - 8650752 } \
		END { printf "RAM code %d B, retention (code + data) %d B -> %s deep retention, RAM used %d B of 32768, image %d B\n", \
			r, t, (t < 16384 ? "16K" : "32K"), e, b }'

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@
	$(CHECK_FW)

clean:
	rm -rf $(OUT_PATH) $(BIN)

.PHONY: all clean
