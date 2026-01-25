# Kurios2 Kernel Makefile

include ../toolchain/config.mk

# Directories
BUILD_DIR := ../build/kernel
SRC_DIR := .

# Source files
ASM_SOURCES := entry.asm
C_SOURCES := \
    main.c \
    debug/debug.c \
    arch/x86_64/serial.c

# Object files
ASM_OBJECTS := $(ASM_SOURCES:%.asm=$(BUILD_DIR)/%.o)
C_OBJECTS := $(C_SOURCES:%.c=$(BUILD_DIR)/%.o)
OBJECTS := $(ASM_OBJECTS) $(C_OBJECTS)

# Output
KERNEL := $(BUILD_DIR)/kernel.bin
KERNEL_ELF := $(BUILD_DIR)/kernel.elf

# Include paths
INCLUDES := \
    -I. \
    -I./include \
    -I./arch/x86_64 \
    -I../boot/common

# Flags
KERNEL_CFLAGS := $(CFLAGS) $(INCLUDES)
KERNEL_ASFLAGS := -f elf64 -g -F dwarf

.PHONY: all clean

all: $(KERNEL)

# Create build directories
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)/debug
	mkdir -p $(BUILD_DIR)/arch/x86_64

# Ensure directories exist before compiling
$(BUILD_DIR)/debug/%.o: | $(BUILD_DIR)
$(BUILD_DIR)/arch/x86_64/%.o: | $(BUILD_DIR)

# Assembly files
$(BUILD_DIR)/%.o: %.asm | $(BUILD_DIR)
	$(AS) $(KERNEL_ASFLAGS) -o $@ $<

# C files (with directory creation)
$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(KERNEL_CFLAGS) -c -o $@ $<

# Link kernel
$(KERNEL_ELF): $(OBJECTS) linker.ld
	$(LD) $(LDFLAGS) -T linker.ld -o $@ $(OBJECTS)
	@echo "Kernel ELF: $@"

# Create flat binary
$(KERNEL): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@
	@echo "Kernel binary: $@ ($$(stat -c%s $@ 2>/dev/null || stat -f%z $@) bytes)"

# Show kernel info
info: $(KERNEL_ELF)
	@echo "=== Kernel Sections ==="
	$(OBJDUMP) -h $(KERNEL_ELF)
	@echo ""
	@echo "=== Kernel Symbols ==="
	nm $(KERNEL_ELF) | grep -E "^[0-9a-f]+ [TDBtdb]" | sort

clean:
	rm -rf $(BUILD_DIR)
