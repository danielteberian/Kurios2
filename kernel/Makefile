# Kurios2 Kernel Makefile

include ../toolchain/config.mk

# Directories
BUILD_DIR := ../build/kernel
SRC_DIR := .

# Source files
ASM_SOURCES := entry.asm
C_SOURCES := main.c

# Object files
ASM_OBJECTS := $(ASM_SOURCES:%.asm=$(BUILD_DIR)/%.o)
C_OBJECTS := $(C_SOURCES:%.c=$(BUILD_DIR)/%.o)
OBJECTS := $(ASM_OBJECTS) $(C_OBJECTS)

# Output
KERNEL := $(BUILD_DIR)/kernel.bin
KERNEL_ELF := $(BUILD_DIR)/kernel.elf

# Flags
KERNEL_CFLAGS := $(CFLAGS) -I./include -I../boot/common
KERNEL_ASFLAGS := -f elf64 -g -F dwarf

.PHONY: all clean

all: $(KERNEL)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: %.asm | $(BUILD_DIR)
	$(AS) $(KERNEL_ASFLAGS) -o $@ $<

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

$(KERNEL_ELF): $(OBJECTS) linker.ld
	$(LD) $(LDFLAGS) -T linker.ld -o $@ $(OBJECTS)

$(KERNEL): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@
	@echo "Kernel: $@ ($(shell stat -c%s $@) bytes)"

clean:
	rm -rf $(BUILD_DIR)
