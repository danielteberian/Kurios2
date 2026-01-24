# Kurios2 Bootloader Makefile

include ../toolchain/config.mk

# Directories
BIOS_DIR := bios
UEFI_DIR := uefi
COMMON_DIR := common
BUILD_DIR := ../build/boot

# Output files
BIOS_STAGE1 := $(BUILD_DIR)/stage1.bin
BIOS_STAGE2 := $(BUILD_DIR)/stage2.bin
BIOS_BOOT := $(BUILD_DIR)/bios_boot.bin
UEFI_BOOT := $(BUILD_DIR)/BOOTX64.EFI

# NASM flags
NASM_FLAGS := -f bin -I ../

# GCC flags for UEFI (compile as position-independent)
UEFI_CFLAGS := \
    -ffreestanding \
    -fno-builtin \
    -fno-stack-protector \
    -fno-stack-check \
    -fshort-wchar \
    -mno-red-zone \
    -mno-mmx \
    -mno-sse \
    -mno-sse2 \
    -fpic \
    -Wall \
    -Wextra \
    -Wno-builtin-declaration-mismatch \
    -std=c11 \
    -O2 \
    -I $(COMMON_DIR)

# Linker flags for UEFI (create shared object, then convert to PE)
UEFI_LDFLAGS := \
    -nostdlib \
    -shared \
    -Bsymbolic \
    -T $(UEFI_DIR)/uefi.ld

.PHONY: all clean bios uefi

all: $(BUILD_DIR) bios uefi

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# BIOS Bootloader
bios: $(BIOS_BOOT)

$(BIOS_STAGE1): $(BIOS_DIR)/stage1.asm $(COMMON_DIR)/boot_info.inc | $(BUILD_DIR)
	$(AS) $(NASM_FLAGS) -o $@ $<

$(BIOS_STAGE2): $(BIOS_DIR)/stage2.asm $(COMMON_DIR)/boot_info.inc | $(BUILD_DIR)
	$(AS) $(NASM_FLAGS) -o $@ $<

$(BIOS_BOOT): $(BIOS_STAGE1) $(BIOS_STAGE2)
	cat $(BIOS_STAGE1) $(BIOS_STAGE2) > $@
	@echo "BIOS bootloader: $@ ($(shell stat -c%s $@) bytes)"

# UEFI Bootloader
uefi: $(UEFI_BOOT)

$(BUILD_DIR)/bootloader.o: $(UEFI_DIR)/bootloader.c $(UEFI_DIR)/efi_types.h | $(BUILD_DIR)
	$(CC) $(UEFI_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/bootloader.so: $(BUILD_DIR)/bootloader.o $(UEFI_DIR)/uefi.ld
	$(LD) $(UEFI_LDFLAGS) -o $@ $(BUILD_DIR)/bootloader.o

# Convert ELF shared object to PE/COFF for UEFI
$(UEFI_BOOT): $(BUILD_DIR)/bootloader.so
	$(OBJCOPY) \
		-j .text \
		-j .rodata \
		-j .data \
		-j .bss \
		-j .reloc \
		--target=efi-app-x86_64 \
		$< $@
	@echo "UEFI bootloader: $@ ($(shell stat -c%s $@) bytes)"

clean:
	rm -rf $(BUILD_DIR)
