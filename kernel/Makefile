# Kurios2 Kernel Makefile

include ../toolchain/config.mk

# Directories
BUILD_DIR := ../build/kernel
SRC_DIR := .

# Source files
ASM_SOURCES := entry.asm arch/x86_64/gdt_flush.asm arch/x86_64/isr.asm
C_SOURCES := \
    main.c \
    stack_protector.c \
    debug/debug.c \
    arch/x86_64/serial.c \
    arch/x86_64/gdt.c \
    arch/x86_64/idt.c \
    mm/pmm.c \
    mm/vmm.c \
    mm/slab.c

# Test source files (only included in test builds)
TEST_SOURCES := \
    tests/test_framework.c \
    tests/test_all.c \
    tests/test_pmm.c \
    tests/test_vmm.c \
    tests/test_slab.c

# Object files
ASM_OBJECTS := $(ASM_SOURCES:%.asm=$(BUILD_DIR)/%.o)
C_OBJECTS := $(C_SOURCES:%.c=$(BUILD_DIR)/%.o)
TEST_OBJECTS := $(TEST_SOURCES:%.c=$(BUILD_DIR)/%.o)
OBJECTS := $(ASM_OBJECTS) $(C_OBJECTS)

# Add test objects when building tests
ifdef TEST_MODE
OBJECTS += $(TEST_OBJECTS)
endif

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

# Debug build adds DEBUG_TESTS
ifdef DEBUG
KERNEL_CFLAGS += -DDEBUG_TESTS
endif

# Test build adds TEST_MODE
ifdef TEST_MODE
KERNEL_CFLAGS += -DTEST_MODE
endif

.PHONY: all clean debug test

all: $(KERNEL)

# Debug build with tests enabled
debug:
	$(MAKE) DEBUG=1 all

# Test build - includes test framework
test:
	$(MAKE) TEST_MODE=1 all

# Create build directories
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)/debug
	mkdir -p $(BUILD_DIR)/arch/x86_64
	mkdir -p $(BUILD_DIR)/mm
	mkdir -p $(BUILD_DIR)/tests

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
