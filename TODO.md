# Kurios2 - 64-bit OS Kernel TODO

## Pending Tasks
(None)

## In Progress
(None)

## Blocked / Awaiting Verification
- [ ] Custom hybrid bootloader (BIOS + UEFI) - **READY FOR TESTING**
  - BIOS bootloader: `build/boot/bios_boot.bin` (8704 bytes)
  - UEFI bootloader: `build/boot/BOOTX64.EFI` (5388 bytes)
  - Test kernel: `build/kernel/kernel.bin` (4463 bytes)
  - BIOS disk image: `build/kurios2-bios.img`
  - Test with: `make run-bios`
  - For UEFI: Install OVMF (`sudo apt install ovmf`) then `make run-uefi`
