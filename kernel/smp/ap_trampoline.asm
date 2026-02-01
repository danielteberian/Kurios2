; ap_trampoline.asm - Application Processor startup code
;
; This code is copied to 0x7000 and executed by APs when they receive a SIPI.
; It transitions the AP from real mode to long mode and jumps to ap_entry().
;
; Memory layout at startup time (set by BSP):
;   0x7F00: CR3 value (8 bytes) - page table address
;   0x7F08: AP kernel stack (8 bytes) - top of per-CPU kernel stack
;   0x7F10: Per-CPU data pointer (8 bytes) - percpu_data_t *
;   0x7F18: Entry point (8 bytes) - address of ap_entry()
;
; The code is position-dependent and expects to run at 0x7000.
; We use explicit addresses calculated from AP_TRAMPOLINE_ADDR.
;
; IMPORTANT: We load all parameters while in 32-bit mode (before paging),
; then save them in registers to use after switching to long mode.

%define AP_TRAMPOLINE_ADDR 0x7000

section .note.GNU-stack noalloc noexec nowrite progbits

section .text

global ap_trampoline_start
global ap_trampoline_end

; VGA debug helper: write character to VGA memory
; In 16-bit mode: writes to 0xB8000 + offset
%define VGA_BASE 0xB8000
%define VGA_ATTR 0x4F    ; White on red - very visible

; 16-bit real mode code
ap_trampoline_start:
[BITS 16]
    ; Disable interrupts
    cli

    ; Set up segments for real mode
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7000      ; Temporary stack just below trampoline

    ; DEBUG: Write '1' to VGA memory to show we started (top-right corner)
    ; VGA segment = 0xB800, offset = 158 (column 79 of row 0)
    mov ax, 0xB800
    mov gs, ax
    mov word [gs:158], 0x4F31    ; '1' with white on red

    ; Load a temporary GDT for protected mode transition
    lgdt [ap_gdt_ptr - ap_trampoline_start + AP_TRAMPOLINE_ADDR]

    ; Enable protected mode (set PE bit in CR0)
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Far jump to 32-bit protected mode code
    jmp dword 0x08:(.pm32 - ap_trampoline_start + AP_TRAMPOLINE_ADDR)

[BITS 32]
.pm32:
    ; Set up 32-bit segments
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    ; DEBUG: Write '2' to VGA memory (32-bit mode reached)
    mov dword [0xB8000 + 156], 0x4F324F32    ; '22' at position 78-79

    ; Load all parameters NOW while we can still access physical memory directly
    ; Save them in callee-saved registers that will survive the mode switch

    ; Load CR3 value
    mov esi, [0x7F00]       ; Low 32 bits of CR3 (we only need low 32 for now)

    ; Load stack pointer (64-bit, save both halves)
    mov edi, [0x7F08]       ; Low 32 bits of stack
    mov ebp, [0x7F0C]       ; High 32 bits of stack

    ; Save per-CPU pointer to temporary location (we'll reload after long mode)
    ; For now, just save the addresses
    ; ebx will hold low part of percpu, stack holds high part
    mov ebx, [0x7F10]       ; Low 32 bits of percpu

    ; Enable PAE (Physical Address Extension) - required for long mode
    mov eax, cr4
    or eax, (1 << 5)    ; PAE bit
    mov cr4, eax

    ; Load CR3 with page table address
    mov cr3, esi

    ; Enable long mode in EFER MSR
    mov ecx, 0xC0000080 ; EFER MSR
    rdmsr
    or eax, (1 << 8)    ; LME (Long Mode Enable)
    wrmsr

    ; Enable paging (sets LMA in EFER automatically)
    mov eax, cr0
    or eax, (1 << 31)   ; PG bit
    mov cr0, eax

    ; Load 64-bit GDT
    lgdt [ap_gdt64_ptr - ap_trampoline_start + AP_TRAMPOLINE_ADDR]

    ; Far jump to 64-bit long mode code
    jmp dword 0x08:(.lm64 - ap_trampoline_start + AP_TRAMPOLINE_ADDR)

[BITS 64]
.lm64:
    ; Set up 64-bit segments
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; DEBUG: Write '3' to VGA memory (64-bit mode reached)
    ; VGA is at physical 0xB8000, identity-mapped
    mov dword [0xB8000 + 152], 0x4F334F33    ; '33' at position 76-77

    ; Reconstruct the 64-bit stack pointer from saved values
    ; rdi holds low 32 bits, rbp holds high 32 bits
    mov rsp, rbp
    shl rsp, 32
    mov eax, edi
    or rsp, rax

    ; Now we can access the data area through identity mapping
    ; (the bootloader's page tables should have low memory identity-mapped)

    ; Load per-CPU data pointer from 0x7F10 into RDI (first argument)
    ; Note: We're relying on identity mapping working here
    mov rdi, [0x7F10]

    ; DEBUG: Write '4' to VGA memory (about to jump to C)
    mov dword [0xB8000 + 148], 0x4F344F34    ; '44' at position 74-75

    ; Load entry point from 0x7F18
    mov rax, [0x7F18]

    ; Jump to C entry point (ap_entry)
    ; We use a jump instead of call since we never return
    jmp rax

    ; Should never reach here
.hang:
    cli
    hlt
    jmp .hang

; Temporary 32-bit GDT for protected mode transition
align 16
ap_gdt:
    ; Null descriptor
    dq 0
    ; Code segment (0x08): base=0, limit=4GB, execute/read, ring 0
    dq 0x00CF9A000000FFFF
    ; Data segment (0x10): base=0, limit=4GB, read/write, ring 0
    dq 0x00CF92000000FFFF

ap_gdt_ptr:
    dw ap_gdt_ptr - ap_gdt - 1  ; Limit
    dd ap_gdt - ap_trampoline_start + AP_TRAMPOLINE_ADDR  ; Base (physical address)

; 64-bit GDT for long mode
align 16
ap_gdt64:
    ; Null descriptor
    dq 0
    ; Code segment (0x08): 64-bit, execute/read, ring 0
    dq 0x00AF9A000000FFFF
    ; Data segment (0x10): read/write, ring 0
    dq 0x00CF92000000FFFF

ap_gdt64_ptr:
    dw ap_gdt64_ptr - ap_gdt64 - 1  ; Limit
    dd ap_gdt64 - ap_trampoline_start + AP_TRAMPOLINE_ADDR  ; Base (32-bit for lgdt in 32-bit mode)

ap_trampoline_end:
