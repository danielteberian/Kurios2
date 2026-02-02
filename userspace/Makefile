# Kurios2 User-space Makefile

# Toolchain
CC := gcc
AS := as
LD := ld

# Directories
BUILD_DIR := ../build/userspace
LIBC_DIR := libc
SH_DIR := sh
TESTS_DIR := tests

# Flags
CFLAGS := -Wall -Wextra -O2 -ffreestanding -fno-stack-protector \
          -fno-builtin -nostdlib -nostdinc -fno-pie -no-pie \
          -mcmodel=small -mno-red-zone \
          -I$(LIBC_DIR)/include
ASFLAGS := --64
LDFLAGS := -T linker.ld -nostdlib

# Libc sources
LIBC_C_SRCS := $(LIBC_DIR)/string.c $(LIBC_DIR)/stdio.c $(LIBC_DIR)/start.c
LIBC_ASM_SRCS := $(LIBC_DIR)/syscall.S
LIBC_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(LIBC_C_SRCS)) \
             $(patsubst %.S,$(BUILD_DIR)/%.o,$(LIBC_ASM_SRCS))

# Shell sources
SH_SRCS := $(SH_DIR)/sh.c
SH_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SH_SRCS))

# Test sources
TEST_SRCS := $(TESTS_DIR)/test_rlimits.c \
             $(TESTS_DIR)/test_tcp.c \
             $(TESTS_DIR)/test_limits_enforce.c
TEST_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(TEST_SRCS))
TEST_BINS := $(patsubst $(TESTS_DIR)/%.c,$(BUILD_DIR)/bin/%,$(TEST_SRCS))

# Targets
PROGRAMS := $(BUILD_DIR)/bin/sh $(TEST_BINS)

.PHONY: all clean

all: dirs $(PROGRAMS)

dirs:
	@mkdir -p $(BUILD_DIR)/$(LIBC_DIR)
	@mkdir -p $(BUILD_DIR)/$(SH_DIR)
	@mkdir -p $(BUILD_DIR)/$(TESTS_DIR)
	@mkdir -p $(BUILD_DIR)/bin

# Shell binary
$(BUILD_DIR)/bin/sh: $(LIBC_OBJS) $(SH_OBJS) linker.ld
	@echo "  LD    $@"
	@$(LD) $(LDFLAGS) -o $@ $(LIBC_OBJS) $(SH_OBJS)

# Test binaries (simple, no libc - they're standalone)
$(BUILD_DIR)/bin/test_%: $(BUILD_DIR)/$(TESTS_DIR)/test_%.o
	@echo "  LD    $@"
	@$(LD) -static -nostdlib -T linker.ld -o $@ $<

# C source files
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) -c -o $@ $<

# Assembly source files
$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	@echo "  AS    $<"
	@$(AS) $(ASFLAGS) -o $@ $<

clean:
	rm -rf $(BUILD_DIR)

# Create initrd (CPIO archive)
# Usage: make initrd INITRD_OUT=path/to/initrd.cpio
INITRD_OUT ?= $(BUILD_DIR)/initrd.cpio

initrd: all
	@echo "Creating initrd..."
	@mkdir -p $(BUILD_DIR)/initrd_root/bin
	@cp $(BUILD_DIR)/bin/sh $(BUILD_DIR)/initrd_root/bin/
	@cp $(TEST_BINS) $(BUILD_DIR)/initrd_root/bin/ 2>/dev/null || true
	@cd $(BUILD_DIR)/initrd_root && find . | cpio -o -H newc > ../initrd.cpio 2>/dev/null
	@echo "Initrd created: $(BUILD_DIR)/initrd.cpio"
	@ls -l $(BUILD_DIR)/initrd.cpio

.PHONY: test
test: all
	@echo "Available tests:"
	@echo "  make run-test-rlimits         - Test resource limits syscalls"
	@echo "  make run-test-tcp             - Test TCP socket operations"
	@echo "  make run-test-limits-enforce  - Test RLIMIT_NOFILE enforcement"
