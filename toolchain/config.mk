# Kurios2 Toolchain Configuration
# For x86_64 freestanding kernel development

# Cross-compiler prefix (empty = use system compiler with freestanding flags)
CROSS_PREFIX ?=

# Tools
CC      := $(CROSS_PREFIX)gcc
CXX     := $(CROSS_PREFIX)g++
AS      := nasm
LD      := $(CROSS_PREFIX)ld
AR      := $(CROSS_PREFIX)ar
OBJCOPY := $(CROSS_PREFIX)objcopy
OBJDUMP := $(CROSS_PREFIX)objdump

# Target architecture
ARCH := x86_64

# Common flags for freestanding environment
FREESTANDING_FLAGS := \
    -ffreestanding \
    -fno-builtin \
    -nostdlib \
    -nostdinc \
    -fno-stack-protector \
    -fno-stack-check \
    -fno-lto \
    -fno-PIC \
    -fno-PIE \
    -m64 \
    -mno-red-zone \
    -mno-mmx \
    -mno-sse \
    -mno-sse2 \
    -mcmodel=kernel

# Warning flags
WARN_FLAGS := \
    -Wall \
    -Wextra \
    -Werror \
    -Wno-unused-parameter

# C standard and flags
CFLAGS := \
    $(FREESTANDING_FLAGS) \
    $(WARN_FLAGS) \
    -std=c11 \
    -O2 \
    -g

# C++ flags
CXXFLAGS := \
    $(FREESTANDING_FLAGS) \
    $(WARN_FLAGS) \
    -std=c++20 \
    -O2 \
    -g \
    -fno-exceptions \
    -fno-rtti

# Assembler flags (NASM)
ASFLAGS := \
    -f elf64 \
    -g \
    -F dwarf

# Linker flags
LDFLAGS := \
    -nostdlib \
    -static \
    -z max-page-size=0x1000

# QEMU for testing
QEMU := qemu-system-x86_64
QEMU_FLAGS := \
    -m 256M \
    -serial stdio \
    -no-reboot \
    -no-shutdown
