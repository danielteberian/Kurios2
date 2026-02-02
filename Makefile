# Kurios2 - Top Level Makefile

.PHONY: all clean boot kernel userspace image-bios image-uefi run-bios run-uefi debug run-debug run-gdb run-gdb-wait test run-test run-shell initrd

# Build directories
BUILD_DIR := build
ISO_DIR := $(BUILD_DIR)/iso

all: boot kernel

# Full build with shell
shell: boot kernel userspace initrd

# Build bootloader
boot:
	$(MAKE) -C boot

# Build kernel
kernel:
	$(MAKE) -C kernel

# Build userspace programs
userspace:
	$(MAKE) -C userspace

# Create initrd with userspace programs
initrd: userspace
	$(MAKE) -C userspace initrd

# Debug build (with DEBUG_TESTS enabled)
debug: boot
	$(MAKE) -C kernel debug

# Run debug build
run-debug: debug _image-bios
	qemu-system-x86_64 \
		-drive format=raw,file=$(BUILD_DIR)/kurios2-bios.img \
		-m 256M \
		-serial stdio \
		-no-reboot \
		-no-shutdown

# Run with GDB support (COM1=stdio for logs, COM2=TCP:1234 for GDB)
run-gdb: image-bios
	@echo "Starting QEMU with GDB support..."
	@echo "  COM1 (stdio): kernel debug output"
	@echo "  COM2 (TCP:1234): GDB connection"
	@echo ""
	@echo "To connect GDB:"
	@echo "  gdb build/kernel/kernel.elf"
	@echo "  (gdb) target remote localhost:1234"
	@echo ""
	@echo "Tip: Use 'make run-gdb-wait' to break on boot"
	@echo ""
	qemu-system-x86_64 \
		-drive format=raw,file=$(BUILD_DIR)/kurios2-bios.img \
		-m 256M \
		-serial stdio \
		-serial tcp::1234,server,nowait \
		-no-reboot \
		-no-shutdown

# Run with GDB, waiting for connection on boot
run-gdb-wait: boot
	$(MAKE) -C kernel EXTRA_CFLAGS="-DGDB_BREAK_ON_BOOT"
	$(MAKE) _image-bios
	@echo "Kernel will WAIT for GDB connection on boot..."
	@echo "Connect with: gdb build/kernel/kernel.elf -ex 'target remote localhost:1234'"
	@echo ""
	qemu-system-x86_64 \
		-drive format=raw,file=$(BUILD_DIR)/kurios2-bios.img \
		-m 256M \
		-serial stdio \
		-serial tcp::1234,server,nowait \
		-no-reboot \
		-no-shutdown

# Build with test framework
test: boot
	$(MAKE) -C kernel test

# Run tests (build and execute in QEMU)
run-test: test _image-bios
	qemu-system-x86_64 \
		-drive format=raw,file=$(BUILD_DIR)/kurios2-bios.img \
		-m 256M \
		-serial stdio \
		-no-reboot \
		-no-shutdown

# Initrd file (can be overridden: make INITRD=path/to/initrd.cpio)
INITRD ?=

# Create BIOS bootable disk image (internal, no deps - used after test/debug builds)
# Note: This target uses pre-built bootloader, so initrd will only work if
# bootloader was built with correct KERNEL_SIZE and INITRD_SIZE values
_image-bios:
	@echo "Creating BIOS bootable disk image..."
	@mkdir -p $(BUILD_DIR)
	# Create disk image (4MB for room for kernel + initrd)
	dd if=/dev/zero of=$(BUILD_DIR)/kurios2-bios.img bs=512 count=8192 2>/dev/null
	# Write bootloader (stage1 + stage2)
	dd if=$(BUILD_DIR)/boot/bios_boot.bin of=$(BUILD_DIR)/kurios2-bios.img conv=notrunc 2>/dev/null
	# Write kernel starting at sector 34 (after stage1 + stage2)
	dd if=$(BUILD_DIR)/kernel/kernel.bin of=$(BUILD_DIR)/kurios2-bios.img bs=512 seek=34 conv=notrunc 2>/dev/null
	# Write initrd after kernel (if provided)
	@if [ -n "$(INITRD)" ] && [ -f "$(INITRD)" ]; then \
		KERNEL_SIZE=$$(stat -c%s $(BUILD_DIR)/kernel/kernel.bin); \
		KERNEL_SECTORS=$$(( ($$KERNEL_SIZE + 511) / 512 )); \
		INITRD_LBA=$$(( 34 + $$KERNEL_SECTORS )); \
		echo "Writing initrd at sector $$INITRD_LBA..."; \
		dd if=$(INITRD) of=$(BUILD_DIR)/kurios2-bios.img bs=512 seek=$$INITRD_LBA conv=notrunc 2>/dev/null; \
	fi
	@echo "BIOS image: $(BUILD_DIR)/kurios2-bios.img"

# Build BIOS image with initrd support (rebuilds bootloader with correct sizes)
image-bios-initrd: kernel
	@if [ -z "$(INITRD)" ] || [ ! -f "$(INITRD)" ]; then \
		echo "Error: INITRD not specified or file not found"; \
		echo "Usage: make INITRD=path/to/initrd.cpio image-bios-initrd"; \
		exit 1; \
	fi
	@KERNEL_SIZE=$$(stat -c%s $(BUILD_DIR)/kernel/kernel.bin); \
	INITRD_SIZE=$$(stat -c%s $(INITRD)); \
	echo "Building bootloader with KERNEL_SIZE=$$KERNEL_SIZE INITRD_SIZE=$$INITRD_SIZE"; \
	$(MAKE) -C boot bios-force KERNEL_SIZE=$$KERNEL_SIZE INITRD_SIZE=$$INITRD_SIZE
	$(MAKE) INITRD=$(INITRD) _image-bios

# Create BIOS bootable disk image (public, with deps)
image-bios: boot kernel _image-bios

# Create UEFI bootable disk image
image-uefi: boot kernel
	@echo "Creating UEFI bootable disk image..."
	@mkdir -p $(BUILD_DIR)
	# Create FAT32 disk image (64MB)
	dd if=/dev/zero of=$(BUILD_DIR)/kurios2-uefi.img bs=1M count=64 2>/dev/null
	# Create FAT32 filesystem
	mformat -i $(BUILD_DIR)/kurios2-uefi.img -F ::
	# Create EFI directories
	mmd -i $(BUILD_DIR)/kurios2-uefi.img ::/EFI
	mmd -i $(BUILD_DIR)/kurios2-uefi.img ::/EFI/BOOT
	mmd -i $(BUILD_DIR)/kurios2-uefi.img ::/EFI/KURIOS
	# Copy UEFI bootloader
	mcopy -i $(BUILD_DIR)/kurios2-uefi.img $(BUILD_DIR)/boot/BOOTX64.EFI ::/EFI/BOOT/
	# Copy kernel
	mcopy -i $(BUILD_DIR)/kurios2-uefi.img $(BUILD_DIR)/kernel/kernel.bin ::/EFI/KURIOS/KERNEL.BIN
	@echo "UEFI image: $(BUILD_DIR)/kurios2-uefi.img"

# Create hybrid ISO (BIOS + UEFI)
image-iso: boot kernel
	@echo "Creating hybrid ISO..."
	@mkdir -p $(ISO_DIR)/boot/grub
	@mkdir -p $(ISO_DIR)/EFI/BOOT
	@mkdir -p $(ISO_DIR)/EFI/KURIOS
	# Copy kernel
	cp $(BUILD_DIR)/kernel/kernel.bin $(ISO_DIR)/boot/
	# Copy UEFI bootloader
	cp $(BUILD_DIR)/boot/BOOTX64.EFI $(ISO_DIR)/EFI/BOOT/
	cp $(BUILD_DIR)/kernel/kernel.bin $(ISO_DIR)/EFI/KURIOS/KERNEL.BIN
	# Create GRUB config for BIOS boot
	echo 'set timeout=0' > $(ISO_DIR)/boot/grub/grub.cfg
	echo 'set default=0' >> $(ISO_DIR)/boot/grub/grub.cfg
	echo 'menuentry "Kurios2" {' >> $(ISO_DIR)/boot/grub/grub.cfg
	echo '    multiboot2 /boot/kernel.bin' >> $(ISO_DIR)/boot/grub/grub.cfg
	echo '    boot' >> $(ISO_DIR)/boot/grub/grub.cfg
	echo '}' >> $(ISO_DIR)/boot/grub/grub.cfg
	# Create ISO
	grub-mkrescue -o $(BUILD_DIR)/kurios2.iso $(ISO_DIR) 2>/dev/null
	@echo "ISO image: $(BUILD_DIR)/kurios2.iso"

# Run with QEMU (BIOS mode)
run-bios: image-bios
	qemu-system-x86_64 \
		-drive format=raw,file=$(BUILD_DIR)/kurios2-bios.img \
		-m 256M \
		-serial stdio \
		-no-reboot \
		-no-shutdown

# Run with shell (builds userspace, creates initrd, boots with shell)
run-shell: shell
	@KERNEL_SIZE=$$(stat -c%s $(BUILD_DIR)/kernel/kernel.bin); \
	INITRD_SIZE=$$(stat -c%s $(BUILD_DIR)/userspace/initrd.cpio); \
	echo "Rebuilding bootloader with KERNEL_SIZE=$$KERNEL_SIZE INITRD_SIZE=$$INITRD_SIZE"; \
	$(MAKE) -C boot bios-force KERNEL_SIZE=$$KERNEL_SIZE INITRD_SIZE=$$INITRD_SIZE
	@echo "Creating disk image with initrd..."
	@dd if=/dev/zero of=$(BUILD_DIR)/kurios2-bios.img bs=512 count=16384 2>/dev/null
	@dd if=$(BUILD_DIR)/boot/bios_boot.bin of=$(BUILD_DIR)/kurios2-bios.img conv=notrunc 2>/dev/null
	@dd if=$(BUILD_DIR)/kernel/kernel.bin of=$(BUILD_DIR)/kurios2-bios.img bs=512 seek=34 conv=notrunc 2>/dev/null
	@KERNEL_SIZE=$$(stat -c%s $(BUILD_DIR)/kernel/kernel.bin); \
	KERNEL_SECTORS=$$(( ($$KERNEL_SIZE + 511) / 512 )); \
	INITRD_LBA=$$(( 34 + $$KERNEL_SECTORS )); \
	echo "Writing initrd at sector $$INITRD_LBA..."; \
	dd if=$(BUILD_DIR)/userspace/initrd.cpio of=$(BUILD_DIR)/kurios2-bios.img bs=512 seek=$$INITRD_LBA conv=notrunc 2>/dev/null
	@echo "Booting with shell..."
	qemu-system-x86_64 \
		-drive format=raw,file=$(BUILD_DIR)/kurios2-bios.img \
		-m 256M \
		-serial stdio \
		-no-reboot \
		-no-shutdown

# Run with QEMU (UEFI mode) - requires OVMF
run-uefi: image-uefi
	@if [ ! -f /usr/share/OVMF/OVMF_CODE.fd ]; then \
		echo "OVMF not found. Install with: sudo apt install ovmf"; \
		exit 1; \
	fi
	qemu-system-x86_64 \
		-drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE.fd \
		-drive format=raw,file=$(BUILD_DIR)/kurios2-uefi.img \
		-m 256M \
		-serial stdio \
		-no-reboot \
		-no-shutdown

# Clean all build artifacts
clean:
	$(MAKE) -C boot clean
	$(MAKE) -C kernel clean
	$(MAKE) -C userspace clean
	rm -rf $(BUILD_DIR)

# Show help
help:
	@echo "Kurios2 Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all         - Build bootloader and kernel"
	@echo "  shell       - Build kernel + userspace + initrd"
	@echo "  boot        - Build bootloader only"
	@echo "  kernel      - Build kernel only"
	@echo "  userspace   - Build userspace programs"
	@echo "  initrd      - Create initrd with userspace programs"
	@echo "  debug       - Build with DEBUG_TESTS enabled"
	@echo "  test        - Build with test framework"
	@echo "  run-test    - Build and run kernel tests in QEMU"
	@echo "  run-shell   - Build everything and run with shell"
	@echo "  image-bios  - Create BIOS bootable disk image"
	@echo "  image-uefi  - Create UEFI bootable disk image"
	@echo "  image-iso   - Create hybrid ISO (requires grub-mkrescue)"
	@echo "  run-bios    - Run in QEMU with BIOS"
	@echo "  run-uefi    - Run in QEMU with UEFI (requires OVMF)"
	@echo "  run-debug   - Run debug build in QEMU"
	@echo "  run-gdb     - Run with GDB support (connect via localhost:1234)"
	@echo "  run-gdb-wait - Run with GDB, kernel waits for connection on boot"
	@echo "  clean       - Remove all build artifacts"
