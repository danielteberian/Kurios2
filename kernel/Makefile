# Kurios2 Kernel Makefile

include ../toolchain/config.mk

# Directories
BUILD_DIR := ../build/kernel
SRC_DIR := .

# Source files
ASM_SOURCES := entry.asm arch/x86_64/gdt_flush.asm arch/x86_64/isr.asm sched/context.asm syscall/syscall_entry.asm user/ring3_enter.asm smp/ap_trampoline.asm
C_SOURCES := \
    main.c \
    stack_protector.c \
    lib/string.c \
    debug/debug.c \
    debug/gdb_stub.c \
    arch/x86_64/serial.c \
    arch/x86_64/gdt.c \
    arch/x86_64/idt.c \
    mm/pmm.c \
    mm/vmm.c \
    mm/slab.c \
    mm/as.c \
    mm/uaccess.c \
    mm/vma.c \
    mm/fault.c \
    mm/page_cache.c \
    fs/vfs.c \
    fs/ramfs.c \
    fs/fd_table.c \
    fs/procfs.c \
    fs/pipe.c \
    fs/ext2.c \
    fs/fat32.c \
    drivers/keyboard.c \
    drivers/vga.c \
    drivers/pit.c \
    drivers/hpet.c \
    drivers/tty.c \
    drivers/mouse.c \
    drivers/pci.c \
    drivers/block.c \
    drivers/io_sched.c \
    drivers/pty.c \
    drivers/rtc.c \
    drivers/virtio/virtio.c \
    drivers/virtio/virtio_blk.c \
    drivers/virtio/virtio_net.c \
    drivers/partition.c \
    sched/thread.c \
    sched/sched.c \
    process/process.c \
    process/pgrp.c \
    syscall/syscall.c \
    user/user_entry.c \
    loader/elf_loader.c \
    acpi/acpi.c \
    apic/apic.c \
    initrd/initrd.c \
    smp/percpu.c \
    smp/smp.c \
    smp/tlb.c \
    signal/signal.c \
    net/netdev.c \
    net/ip.c \
    net/icmp.c \
    net/udp.c \
    net/tcp.c \
    net/socket.c \
    net/loopback.c

# Test source files (only included in test builds)
TEST_SOURCES := \
    tests/test_framework.c \
    tests/test_all.c \
    tests/test_pmm.c \
    tests/test_vmm.c \
    tests/test_slab.c \
    tests/test_spinlock.c \
    tests/test_rlimits.c \
    tests/test_socket.c \
    tests/test_netdev.c

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
KERNEL_CFLAGS := $(CFLAGS) $(INCLUDES) $(EXTRA_CFLAGS)
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
	mkdir -p $(BUILD_DIR)/lib
	mkdir -p $(BUILD_DIR)/debug
	mkdir -p $(BUILD_DIR)/arch/x86_64
	mkdir -p $(BUILD_DIR)/mm
	mkdir -p $(BUILD_DIR)/fs
	mkdir -p $(BUILD_DIR)/sync
	mkdir -p $(BUILD_DIR)/drivers
	mkdir -p $(BUILD_DIR)/drivers/virtio
	mkdir -p $(BUILD_DIR)/sched
	mkdir -p $(BUILD_DIR)/process
	mkdir -p $(BUILD_DIR)/syscall
	mkdir -p $(BUILD_DIR)/tests
	mkdir -p $(BUILD_DIR)/user
	mkdir -p $(BUILD_DIR)/loader
	mkdir -p $(BUILD_DIR)/acpi
	mkdir -p $(BUILD_DIR)/apic
	mkdir -p $(BUILD_DIR)/initrd
	mkdir -p $(BUILD_DIR)/smp
	mkdir -p $(BUILD_DIR)/signal
	mkdir -p $(BUILD_DIR)/net

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
