# Kurios2 User-space Makefile

# Toolchain
CC := gcc
AS := as
LD := ld

# Directories
BUILD_DIR := ../build/userspace
LIBC_DIR := libc
SH_DIR := sh

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

# Targets
PROGRAMS := $(BUILD_DIR)/bin/sh

.PHONY: all clean

all: dirs $(PROGRAMS)

dirs:
	@mkdir -p $(BUILD_DIR)/$(LIBC_DIR)
	@mkdir -p $(BUILD_DIR)/$(SH_DIR)
	@mkdir -p $(BUILD_DIR)/bin

# Shell binary
$(BUILD_DIR)/bin/sh: $(LIBC_OBJS) $(SH_OBJS) linker.ld
	@echo "  LD    $@"
	@$(LD) $(LDFLAGS) -o $@ $(LIBC_OBJS) $(SH_OBJS)

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
	@cd $(BUILD_DIR)/initrd_root && find . | cpio -o -H newc > ../initrd.cpio 2>/dev/null
	@echo "Initrd created: $(BUILD_DIR)/initrd.cpio"
	@ls -l $(BUILD_DIR)/initrd.cpio
