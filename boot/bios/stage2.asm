; stage2.asm - BIOS Stage 2 Bootloader
; Loaded at 0x10000 by Stage 1
; Purpose: Enable A20, get memory map, enter long mode, load kernel

[BITS 16]
[ORG 0x10000]

%include "boot/common/boot_info.inc"

; -----------------------------------------------------------------------------
; Entry point from Stage 1
; DL = boot drive
; -----------------------------------------------------------------------------
stage2_entry:
    ; Set up segments for stage 2
    cli
    mov ax, cs
    mov ds, ax
    mov es, ax
    xor ax, ax
    mov ss, ax
    mov sp, STACK_TOP
    sti

    ; Save boot drive
    mov [boot_drive], dl

    ; Print stage 2 message
    mov si, msg_stage2
    call print_string

    ; Enable A20 line
    call enable_a20
    test ax, ax
    jz a20_error

    mov si, msg_a20_ok
    call print_string

    ; Get memory map via E820
    call get_memory_map
    test ax, ax
    jz mmap_error

    mov si, msg_mmap_ok
    call print_string

    ; Try to get VESA framebuffer
    call setup_vesa

    ; Load kernel from disk
    call load_kernel
    test ax, ax
    jz kernel_error

    mov si, msg_kernel_ok
    call print_string

    ; Prepare boot info structure
    call prepare_boot_info

    ; Disable interrupts for mode switch
    cli

    ; Load GDT
    lgdt [gdt_descriptor]

    ; Enable protected mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Jump to 32-bit code
    jmp 0x08:protected_mode

; -----------------------------------------------------------------------------
; Error handlers (16-bit)
; -----------------------------------------------------------------------------
a20_error:
    mov si, msg_a20_err
    call print_string
    jmp halt16

mmap_error:
    mov si, msg_mmap_err
    call print_string
    jmp halt16

kernel_error:
    mov si, msg_kernel_err
    call print_string
    jmp halt16

halt16:
    cli
    hlt
    jmp halt16

; -----------------------------------------------------------------------------
; Print string (16-bit real mode)
; -----------------------------------------------------------------------------
print_string:
    push ax
    push bx
    mov ah, 0x0E
    mov bh, 0
.loop:
    lodsb
    test al, al
    jz .done
    int 0x10
    jmp .loop
.done:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; Enable A20 line
; Returns: AX = 1 on success, 0 on failure
; Tries multiple methods: check first, then BIOS, Fast A20, keyboard controller
; -----------------------------------------------------------------------------
enable_a20:
    ; First check if A20 is already enabled (common in emulators)
    call check_a20
    test ax, ax
    jnz .done               ; Already enabled, we're done

    ; Method 1: BIOS INT 15h
    mov ax, 0x2401
    int 0x15
    call check_a20
    test ax, ax
    jnz .done

    ; Method 2: Fast A20 gate (port 0x92)
    in al, 0x92
    test al, 2              ; Check if already set
    jnz .try_kbd
    or al, 2                ; Set A20 bit
    and al, 0xFE            ; Don't reset CPU (bit 0)
    out 0x92, al
    call check_a20
    test ax, ax
    jnz .done

.try_kbd:
    ; Method 3: Keyboard controller
    cli
    call .wait_input
    mov al, 0xAD            ; Disable keyboard
    out 0x64, al

    call .wait_input
    mov al, 0xD0            ; Read output port command
    out 0x64, al

    call .wait_output
    in al, 0x60             ; Read output port data
    push ax

    call .wait_input
    mov al, 0xD1            ; Write output port command
    out 0x64, al

    call .wait_input
    pop ax
    or al, 2                ; Set A20 bit
    out 0x60, al            ; Write output port data

    call .wait_input
    mov al, 0xAE            ; Enable keyboard
    out 0x64, al

    call .wait_input
    sti

    ; Final check
    call check_a20

.done:
    ret

.wait_input:
    mov cx, 0xFFFF
.wait_input_loop:
    in al, 0x64
    test al, 2
    jz .wait_input_done
    loop .wait_input_loop
.wait_input_done:
    ret

.wait_output:
    mov cx, 0xFFFF
.wait_output_loop:
    in al, 0x64
    test al, 1
    jnz .wait_output_done
    loop .wait_output_loop
.wait_output_done:
    ret

; -----------------------------------------------------------------------------
; Check if A20 is enabled
; Returns: AX = 1 if enabled, 0 if disabled
; Uses wrap-around test: 0000:0500 vs FFFF:0510 (= 0x100500 physical)
; -----------------------------------------------------------------------------
check_a20:
    pushf
    push ds
    push es
    push di
    push si
    push bx

    cli                     ; Disable interrupts during test

    ; Set up segments
    xor ax, ax
    mov es, ax              ; ES = 0x0000
    mov ax, 0xFFFF
    mov ds, ax              ; DS = 0xFFFF

    mov di, 0x0500          ; ES:DI = 0000:0500 = 0x00500 physical
    mov si, 0x0510          ; DS:SI = FFFF:0510 = 0x100500 physical (if A20 on)
                            ;                   = 0x00500 physical (if A20 off)

    ; Save original values
    mov al, [es:di]
    mov bl, al              ; Save [0x00500]
    mov al, [ds:si]
    mov bh, al              ; Save [0x100500] or [0x00500]

    ; Write test pattern
    mov byte [es:di], 0x00  ; Write 0x00 to 0x00500
    mov byte [ds:si], 0xFF  ; Write 0xFF to 0x100500 (or 0x00500 if A20 off)

    ; Read back and compare
    mov al, [es:di]         ; Read 0x00500

    ; Restore original values
    mov [es:di], bl
    mov [ds:si], bh

    ; If A20 is off, both addresses point to same location,
    ; so [es:di] will be 0xFF (overwritten by second write)
    ; If A20 is on, they're different locations,
    ; so [es:di] will still be 0x00

    cmp al, 0xFF            ; If 0xFF, A20 is disabled
    je .a20_disabled

    ; A20 is enabled
    mov ax, 1
    jmp .check_done

.a20_disabled:
    xor ax, ax

.check_done:
    pop bx
    pop si
    pop di
    pop es
    pop ds
    popf
    ret

; -----------------------------------------------------------------------------
; Get memory map using BIOS E820
; Returns: AX = 1 on success, 0 on failure
; -----------------------------------------------------------------------------
get_memory_map:
    push es
    push di
    push ebx
    push ecx
    push edx

    ; Set up destination
    xor ax, ax
    mov es, ax
    mov di, MEMORY_MAP_ADDR
    xor ebx, ebx            ; Continuation value
    xor bp, bp              ; Entry count

.loop:
    mov eax, 0xE820
    mov ecx, 24             ; Buffer size
    mov edx, 0x534D4150     ; "SMAP" signature
    int 0x15

    jc .done                ; Carry set = error or end
    cmp eax, 0x534D4150     ; Verify signature
    jne .error

    test ebx, ebx           ; EBX = 0 means end
    jz .done

    ; Move to next entry
    add di, MMAP_ENTRY_SIZE
    inc bp
    cmp bp, 64              ; Max 64 entries
    jl .loop

.done:
    ; Store entry count
    mov [memory_map_count], bp
    pop edx
    pop ecx
    pop ebx
    pop di
    pop es
    mov ax, 1
    ret

.error:
    pop edx
    pop ecx
    pop ebx
    pop di
    pop es
    xor ax, ax
    ret

; -----------------------------------------------------------------------------
; Set up VESA framebuffer
; -----------------------------------------------------------------------------
setup_vesa:
    push es
    push di

    ; Get VESA info
    mov ax, 0x4F00
    mov di, vesa_info
    int 0x10
    cmp ax, 0x004F
    jne .no_vesa

    ; Try to set mode 1024x768x32
    mov ax, 0x4F01
    mov cx, 0x118           ; 1024x768x32
    mov di, vesa_mode_info
    int 0x10
    cmp ax, 0x004F
    jne .try_lower

    ; Set mode
    mov ax, 0x4F02
    mov bx, 0x4118          ; Mode with linear framebuffer
    int 0x10
    cmp ax, 0x004F
    jne .try_lower
    jmp .save_info

.try_lower:
    ; Try 800x600x32
    mov ax, 0x4F01
    mov cx, 0x115
    mov di, vesa_mode_info
    int 0x10
    cmp ax, 0x004F
    jne .no_vesa

    mov ax, 0x4F02
    mov bx, 0x4115
    int 0x10
    cmp ax, 0x004F
    jne .no_vesa

.save_info:
    ; Save framebuffer info
    mov byte [has_framebuffer], 1
    jmp .done

.no_vesa:
    mov byte [has_framebuffer], 0

.done:
    pop di
    pop es
    ret

; -----------------------------------------------------------------------------
; Load kernel from disk
; For now, kernel follows stage 2 on disk (starting at sector 34)
; Returns: AX = 1 on success, 0 on failure
; -----------------------------------------------------------------------------
load_kernel:
    push es
    push bx
    push cx
    push dx

    ; Load kernel to 1MB (use unreal mode to access high memory)
    ; First, set up unreal mode
    call enter_unreal_mode

    ; Read kernel sectors using BIOS extended read
    mov si, disk_packet
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc .error

    ; Copy from temporary buffer to 1MB using unreal mode
    mov esi, 0x20000        ; Temporary load address
    mov edi, KERNEL_LOAD_ADDR
    mov ecx, [kernel_size]
    shr ecx, 2              ; Convert to dwords

.copy_loop:
    mov eax, [esi]
    mov [edi], eax
    add esi, 4
    add edi, 4
    dec ecx
    jnz .copy_loop

    pop dx
    pop cx
    pop bx
    pop es
    mov ax, 1
    ret

.error:
    pop dx
    pop cx
    pop bx
    pop es
    xor ax, ax
    ret

; -----------------------------------------------------------------------------
; Enter unreal mode (big real mode)
; Allows accessing memory above 1MB in real mode
; -----------------------------------------------------------------------------
enter_unreal_mode:
    push ds
    push es

    cli
    lgdt [gdt_descriptor]

    mov eax, cr0
    or al, 1
    mov cr0, eax

    mov bx, 0x10            ; Data segment selector
    mov ds, bx
    mov es, bx

    and al, 0xFE
    mov cr0, eax

    pop es
    pop ds
    sti
    ret

; -----------------------------------------------------------------------------
; Prepare boot info structure
; -----------------------------------------------------------------------------
prepare_boot_info:
    push es
    push di
    xor ax, ax
    mov es, ax
    mov di, BOOT_INFO_ADDR

    ; Magic number
    mov dword [es:di + BOOT_INFO_MAGIC], KURIOS_BOOT_MAGIC & 0xFFFFFFFF
    mov dword [es:di + BOOT_INFO_MAGIC + 4], KURIOS_BOOT_MAGIC >> 32

    ; Version
    mov dword [es:di + BOOT_INFO_VERSION], BOOT_PROTOCOL_VERSION
    mov dword [es:di + BOOT_INFO_VERSION + 4], 0

    ; Flags
    mov dword [es:di + BOOT_INFO_FLAGS], BOOT_FLAG_BIOS
    mov dword [es:di + BOOT_INFO_FLAGS + 4], 0

    ; Memory map pointer
    mov dword [es:di + BOOT_INFO_MEMORY_MAP], MEMORY_MAP_ADDR
    mov dword [es:di + BOOT_INFO_MEMORY_MAP + 4], 0

    ; Memory map count
    movzx eax, word [memory_map_count]
    mov [es:di + BOOT_INFO_MEMORY_COUNT], eax
    mov dword [es:di + BOOT_INFO_MEMORY_COUNT + 4], 0

    ; Kernel physical address
    mov dword [es:di + BOOT_INFO_KERNEL_PHYS], KERNEL_LOAD_ADDR
    mov dword [es:di + BOOT_INFO_KERNEL_PHYS + 4], 0

    ; Kernel size
    mov eax, [kernel_size]
    mov [es:di + BOOT_INFO_KERNEL_SIZE], eax
    mov dword [es:di + BOOT_INFO_KERNEL_SIZE + 4], 0

    ; Boot drive
    movzx eax, byte [boot_drive]
    mov [es:di + BOOT_INFO_BOOT_DRIVE], eax
    mov dword [es:di + BOOT_INFO_BOOT_DRIVE + 4], 0

    ; Set framebuffer flag if available
    cmp byte [has_framebuffer], 1
    jne .no_fb
    or dword [es:di + BOOT_INFO_FLAGS], BOOT_FLAG_FRAMEBUFFER
.no_fb:

    pop di
    pop es
    ret

; =============================================================================
; 32-BIT PROTECTED MODE CODE
; =============================================================================
[BITS 32]
protected_mode:
    ; Set up 32-bit segments
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    ; Check for long mode support
    call check_long_mode
    test eax, eax
    jz no_long_mode

    ; Set up paging for long mode
    call setup_paging

    ; Enable PAE
    mov eax, cr4
    or eax, (1 << 5)        ; PAE bit
    mov cr4, eax

    ; Load PML4 address into CR3
    mov eax, 0x1000         ; PML4 table at 0x1000
    mov cr3, eax

    ; Enable long mode via EFER MSR
    mov ecx, 0xC0000080     ; EFER MSR
    rdmsr
    or eax, (1 << 8)        ; LME bit
    wrmsr

    ; Enable paging (enters long mode)
    mov eax, cr0
    or eax, (1 << 31)       ; PG bit
    mov cr0, eax

    ; Jump to 64-bit code
    jmp 0x18:long_mode

no_long_mode:
    ; Print error and halt (could implement VGA text mode output here)
    cli
    hlt
    jmp no_long_mode

; -----------------------------------------------------------------------------
; Check if CPU supports long mode
; Returns: EAX = 1 if supported, 0 otherwise
; -----------------------------------------------------------------------------
check_long_mode:
    ; Check for CPUID support
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 0x200000
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    xor eax, ecx
    jz .no_cpuid

    ; Check for extended CPUID
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no_longmode

    ; Check for long mode
    mov eax, 0x80000001
    cpuid
    test edx, (1 << 29)     ; LM bit
    jz .no_longmode

    mov eax, 1
    ret

.no_cpuid:
.no_longmode:
    xor eax, eax
    ret

; -----------------------------------------------------------------------------
; Set up identity-mapped paging for first 4GB
; Uses 2MB pages for simplicity
; -----------------------------------------------------------------------------
setup_paging:
    push edi
    push ecx
    push eax

    ; Clear page tables area (0x1000 - 0x5000)
    mov edi, 0x1000
    mov ecx, 0x4000 / 4
    xor eax, eax
    rep stosd

    ; PML4[0] -> PDPT at 0x2000
    mov dword [0x1000], 0x2003      ; Present + Writable

    ; PDPT[0] -> PD at 0x3000 (first 1GB)
    mov dword [0x2000], 0x3003

    ; PDPT[1] -> PD at 0x4000 (second 1GB)
    mov dword [0x2008], 0x4003

    ; PDPT[2] -> PD at 0x5000 (third 1GB)
    mov dword [0x2010], 0x5003

    ; PDPT[3] -> PD at 0x6000 (fourth 1GB)
    mov dword [0x2018], 0x6003

    ; Fill PD entries with 2MB pages
    mov edi, 0x3000
    mov eax, 0x0000_0083     ; Present + Writable + 2MB page
    mov ecx, 512 * 4         ; 512 entries * 4 page directories

.fill_pd:
    mov [edi], eax
    add eax, 0x200000        ; Next 2MB
    add edi, 8
    dec ecx
    jnz .fill_pd

    pop eax
    pop ecx
    pop edi
    ret

; =============================================================================
; 64-BIT LONG MODE CODE
; =============================================================================
[BITS 64]
long_mode:
    ; Set up 64-bit segments
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Set up stack
    mov rsp, 0x90000

    ; Clear screen (optional, VGA text mode)
    mov edi, 0xB8000
    mov rax, 0x0F200F200F200F20  ; White space characters
    mov rcx, 500
    rep stosq

    ; Print "64" to confirm we're in long mode
    mov edi, 0xB8000
    mov ax, 0x0F36              ; '6' with white on black
    stosw
    mov ax, 0x0F34              ; '4'
    stosw

    ; Load kernel address
    mov rax, KERNEL_LOAD_ADDR

    ; Pass boot info pointer in RDI (System V ABI)
    mov rdi, BOOT_INFO_ADDR

    ; Jump to kernel
    call rax

    ; Should never return
    cli
.halt:
    hlt
    jmp .halt

; =============================================================================
; DATA SECTION
; =============================================================================
align 16

; GDT for protected and long mode
gdt_start:
    ; Null descriptor
    dq 0

    ; 32-bit code segment (selector 0x08)
    dw 0xFFFF       ; Limit low
    dw 0x0000       ; Base low
    db 0x00         ; Base middle
    db 10011010b    ; Access: present, ring 0, code, executable, readable
    db 11001111b    ; Flags: 4KB granularity, 32-bit + limit high
    db 0x00         ; Base high

    ; 32-bit data segment (selector 0x10)
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10010010b    ; Access: present, ring 0, data, writable
    db 11001111b
    db 0x00

    ; 64-bit code segment (selector 0x18)
    dw 0x0000
    dw 0x0000
    db 0x00
    db 10011010b    ; Access: present, ring 0, code, executable, readable
    db 00100000b    ; Flags: long mode
    db 0x00

    ; 64-bit data segment (selector 0x20)
    dw 0x0000
    dw 0x0000
    db 0x00
    db 10010010b
    db 00000000b
    db 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1  ; Size
    dd gdt_start                ; Address (will be fixed for long mode)

; Disk address packet for extended BIOS read
disk_packet:
    db 0x10         ; Packet size
    db 0            ; Reserved
    dw 64           ; Number of sectors to read
    dw 0x0000       ; Offset
    dw 0x2000       ; Segment (load at 0x20000 temp)
    dq 34           ; Starting LBA (after stage1 + stage2)

; Variables
boot_drive:         db 0
has_framebuffer:    db 0
memory_map_count:   dw 0
kernel_size:        dd 0x10000      ; 64KB default, updated by build

; VESA info structures (512 bytes each)
align 256
vesa_info:          times 256 db 0
vesa_mode_info:     times 256 db 0

; Messages
msg_stage2:         db "Kurios2 Stage2", 0x0D, 0x0A, 0
msg_a20_ok:         db "A20 enabled", 0x0D, 0x0A, 0
msg_a20_err:        db "A20 failed!", 0x0D, 0x0A, 0
msg_mmap_ok:        db "Memory map OK", 0x0D, 0x0A, 0
msg_mmap_err:       db "Memory map failed!", 0x0D, 0x0A, 0
msg_kernel_ok:      db "Kernel loaded", 0x0D, 0x0A, 0
msg_kernel_err:     db "Kernel load failed!", 0x0D, 0x0A, 0

; Pad to sector boundary
times 8192 - ($ - $$) db 0
