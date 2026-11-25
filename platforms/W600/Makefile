CROSS_COMPILE ?= arm-none-eabi-

.DEFAULT_GOAL := all

CFLAGS := -mcpu=cortex-m3 -mthumb -Os -ffreestanding -fno-builtin \
	-fdata-sections -ffunction-sections -Wall -Wextra -Werror -nostdlib
LDFLAGS := -mcpu=cortex-m3 -mthumb -Os -ffreestanding -fno-builtin \
	-fdata-sections -ffunction-sections -Wall -Wextra -Werror -nostdlib \
	-Wl,--gc-sections -Wl,-Tplatforms/W600/loader.ld \
	-Wl,-Map=$(BUILD_DIR)/$(PLATFORM)_stub.map

# W600 retains its standalone RD32 protocol rather than the shared A5 engine.
override SRCS_S := platforms/W600/startup.S
override SRCS_C := platforms/W600/w600_uart_flash_reader.c

COPY_STUB_OBJ := $(BUILD_DIR)/secboot-copy-stub.o
COPY_STUB_BIN := $(BUILD_DIR)/secboot-copy-stub.bin
W600_OBJS := $(SRCS_S:%.S=$(BUILD_DIR)/%.o) $(SRCS_C:%.c=$(BUILD_DIR)/%.o)

$(FINAL): $(BIN) $(COPY_STUB_BIN) platforms/W600/make_w600_image.py
	@echo "Producing stub $(FINAL)"
	@$(PYTHON) platforms/W600/make_w600_image.py $(BIN) $(COPY_STUB_BIN) $@

$(COPY_STUB_OBJ): platforms/W600/secboot_copy_stub.S | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	@$(CC) -c -mcpu=cortex-m3 -mthumb $< -o $@

$(COPY_STUB_BIN): $(COPY_STUB_OBJ)
	@$(OBJCOPY) -O binary -j .text $< $@

$(W600_OBJS): platforms/W600/Makefile platforms/W600/loader.ld

.PHONY: prebuild
prebuild:
