TOOLCHAIN ?= platforms/ECR6600/nds32le-elf/bin/
CROSS_COMPILE ?= nds32le-elf-

.DEFAULT_GOAL := all
SV6X66_BANK_AWARE ?= 0

CFLAGS := -mcpu=n10 -march=v3 -mcmodel=large -flto -Os -std=gnu99 \
	-nostartfiles -nostdlib -nodefaultlibs -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections \
	-DNO_MINIZ_COMPRESSION -DNO_MINIZ_DECOMPRESSION -DDISABLE_KV \
	-DSV6X66_BANK_AWARE=$(SV6X66_BANK_AWARE)
LDFLAGS := -mcpu=n10 -march=v3 -mcmodel=large -flto \
	-nostartfiles -nostdlib -nodefaultlibs -ffreestanding \
	-Wl,--gc-sections -Wl,-Tplatforms/$(PLATFORM)/stub.ld \
	-Wl,-Map=$(BUILD_DIR)/$(PLATFORM)_stub.map \
	-Wl,--no-warn-rwx-segments -z noexecstack

SRCS_S = platforms/SV6X66/start.S
# Keep the SV engine source list when the root Makefile appends common sources.
override SRCS_C := platforms/SV6X66/flasher_stub.c \
	platforms/SV6X66/sv6x66_hal.c \
	src/libc.c \
	platforms/hal_generic.c
RAM_OBJS := $(SRCS_S:%.S=$(BUILD_DIR)/%.o) $(SRCS_C:%.c=$(BUILD_DIR)/%.o)

CARRIER_DIR := $(BUILD_DIR)/carrier
CARRIER_OBJS := $(CARRIER_DIR)/carrier_start.o $(CARRIER_DIR)/payload.o $(CARRIER_DIR)/carrier.o
CARRIER_ELF := $(CARRIER_DIR)/SV6X66_carrier.elf
CARRIER_RAW := $(CARRIER_DIR)/SV6X66_carrier_code.bin
CARRIER_MAP := $(CARRIER_DIR)/SV6X66_carrier.map
CARRIER_ASM := $(CARRIER_DIR)/SV6X66_carrier.asm
SV6X66_CARRIER_BANK ?= B
ifeq ($(SV6X66_CARRIER_BANK),A)
SV6X66_CARRIER_BASE := 0x3000B000
SV6X66_CARRIER_NUMBER := 0
else ifeq ($(SV6X66_CARRIER_BANK),B)
SV6X66_CARRIER_BASE := 0x300A6000
SV6X66_CARRIER_NUMBER := 1
else
$(error SV6X66_CARRIER_BANK must be A or B)
endif
ifeq ($(SV6X66_CARRIER_BANK)$(SV6X66_BANK_AWARE),A0)
$(error Bank A requires SV6X66_BANK_AWARE=1)
endif
CARRIER_FINAL := $(BUILD_DIR)/SV6X66_Bank$(SV6X66_CARRIER_BANK)_Carrier.bin
CARRIER_CFLAGS = -mcpu=n10 -march=v3 -mcmodel=large -Os -std=gnu99 \
	-nostartfiles -nostdlib -nodefaultlibs -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections -fno-jump-tables $(SHARED_FLAGS) $(CLIFLAGS) \
	-DSV6X66_CARRIER_BANK=$(SV6X66_CARRIER_NUMBER) -DSV6X66_BANK_AWARE=$(SV6X66_BANK_AWARE) \
	-DSV6X66_PAYLOAD_PATH='"$(BIN)"'
CARRIER_LDFLAGS = -mcpu=n10 -march=v3 -mcmodel=large \
	-nostartfiles -nostdlib -nodefaultlibs -ffreestanding \
	-Wl,--gc-sections -Wl,-Tplatforms/SV6X66/carrier.ld \
	-Wl,-Map=$(CARRIER_MAP) -Wl,--defsym=SV6X66_CARRIER_BASE=$(SV6X66_CARRIER_BASE) \
	-Wl,--defsym=SV6X66_CARRIER_BANK_ID=$(SV6X66_CARRIER_NUMBER) \
	-Wl,--no-warn-rwx-segments -z noexecstack \
	$(SHARED_FLAGS) -Wl,--print-memory-usage

$(FINAL): $(CARRIER_FINAL)
	@echo "Producing gzip carrier $(FINAL)"
	@gzip -n -c "$(CARRIER_FINAL)" > "$@.tmp"
	@mv -f "$@.tmp" "$@"

$(CARRIER_FINAL): $(CARRIER_RAW) $(BIN) $(CARRIER_MAP) $(CARRIER_ASM) platforms/SV6X66/verify_carrier.py
	@$(PYTHON) platforms/SV6X66/verify_carrier.py \
		$(CARRIER_RAW) $(BIN) $(CARRIER_MAP) $(CARRIER_ASM) $@ $(SV6X66_CARRIER_BANK) $(SV6X66_BANK_AWARE)

$(CARRIER_RAW): $(CARRIER_ELF)
	@$(OBJCOPY) -O binary $< $@

$(CARRIER_MAP): $(CARRIER_ELF)
	@test -s "$@"

$(CARRIER_ASM): $(CARRIER_ELF)
	@$(OBJDUMP) -d $< > $@

$(CARRIER_ELF): $(CARRIER_OBJS) platforms/SV6X66/carrier.ld
	@$(CC) $(CARRIER_LDFLAGS) $(CARRIER_OBJS) -o $@
	@$(SIZE) $@

$(CARRIER_DIR)/%.o: platforms/SV6X66/%.S | prebuild
	@mkdir -p $(dir $@)
	@$(CC) $(CARRIER_CFLAGS) -c $< -o $@

$(CARRIER_DIR)/%.o: platforms/SV6X66/%.c | prebuild
	@mkdir -p $(dir $@)
	@$(CC) $(CARRIER_CFLAGS) -c $< -o $@

$(CARRIER_DIR)/payload.o: $(BIN)

$(RAM_OBJS): platforms/hal_generic.h platforms/SV6X66/sv6x66_hal.h platforms/SV6X66/stub.ld | prebuild
$(BUILD_DIR)/platforms/SV6X66/flasher_stub.o: platforms/SV6X66/sv6x66_restore_transaction.h platforms/SV6X66/sv6x66_app_transaction.h

# Separate output trees prevent a stale Bank-B object from being relinked as A.
SV6X66_UNIFIED_BUILD_DIR ?= build/SV6X66/unified
.PHONY: bank-aware-carriers
bank-aware-carriers:
	@$(MAKE) PLATFORM=SV6X66 BUILD_DIR=$(SV6X66_UNIFIED_BUILD_DIR)/bankA SV6X66_CARRIER_BANK=A SV6X66_BANK_AWARE=1 all
	@$(MAKE) PLATFORM=SV6X66 BUILD_DIR=$(SV6X66_UNIFIED_BUILD_DIR)/bankB SV6X66_CARRIER_BANK=B SV6X66_BANK_AWARE=1 all

# Full recovery uses the same unified A/B carriers and retains V3 ordering.
.PHONY: bank-aware-restore-carriers
bank-aware-restore-carriers: bank-aware-carriers

prebuild:
	@if [ ! -e platforms/ECR6600/nds32le-elf ]; then cd platforms/ECR6600 && xz -d < nds32le-elf.tar.xz | tar xvf - > /dev/null; fi
