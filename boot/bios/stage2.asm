; stage2.asm - BIOS Stage 2 Bootloader
; Loaded at 0x10000 by Stage 1
; Purpose: Enable A20, get memory map, enter long mode, load kernel

[BITS 16]
[ORG 0x0000]

%include "boot/common/boot_info.inc"

; -----------------------------------------------------------------------------
; Entry point from Stage 1
; DL = boot drive
; Loaded at segment 0x1000 (linear 0x10000)
; With ORG 0, labels are offsets from start
; DS=0x1000 so DS:label points to the correct physical address
; -----------------------------------------------------------------------------
stage2_entry:
    ; Set up segments for stage 2
    cli
    mov ax, 0x1000
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

    ; Skip VESA for now
    mov byte [has_framebuffer], 0

    ; Load kernel from disk
    call load_kernel
    test ax, ax
    jz kernel_error

    mov si, msg_kernel_ok
    call print_string

    ; DEBUG: Print 'B' for before boot_info
    mov ax, 0x0E42
    xor bx, bx
    int 0x10

    ; Prepare boot info structure
    call prepare_boot_info

.continue_after_bootinfo:
    ; DEBUG: Print newline for cleaner output
    mov ax, 0x0E0D
    xor bx, bx
    int 0x10
    mov ax, 0x0E0A
    int 0x10

    ; DEBUG: Print "PM" to indicate starting protected mode transition
    mov ax, 0x0E50     ; 'P'
    xor bx, bx
    int 0x10
    mov ax, 0x0E4D     ; 'M'
    int 0x10

    ; DEBUG: Print 'S' for starting protected mode switch
    mov ax, 0x0E53
    xor bx, bx
    int 0x10

    ; Disable interrupts for mode switch
    cli

    ; Load GDT
    lgdt [gdt_descriptor]

    ; Enable protected mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Set up the far pointer for the jump
    mov dword [pm_jump_target], protected_mode + 0x10000
    mov word [pm_jump_target + 4], 0x08

    ; Jump to 32-bit protected mode code
    jmp dword far [pm_jump_target]

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
    ; DEBUG: Print 'L' to show we entered
    mov ax, 0x0E4C
    xor bx, bx
    int 0x10

    push es
    push bx
    push cx
    push dx

    ; Read kernel sectors using BIOS extended read (before unreal mode)
    ; Need normal DS to access disk_packet and boot_drive
    mov si, disk_packet
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc .error

    ; DEBUG: Print 'R' to show read done
    mov ax, 0x0E52
    xor bx, bx
    int 0x10

    ; Save kernel_size to register before entering unreal mode
    ; (because DS will change)
    mov ecx, [kernel_size]
    shr ecx, 2              ; Convert to dwords

    ; Now enter unreal mode for the high memory copy
    call enter_unreal_mode

    ; DEBUG: Print 'U' to show unreal mode done
    ; NOTE: BIOS calls may not work reliably in unreal mode
    ; Use VGA direct write instead
    mov byte [0xB8008], 'U'
    mov byte [0xB8009], 0x0F

    ; Copy from temporary buffer to 2MB using unreal mode
    ; DS now has 4GB limit, base=0
    mov esi, 0x20000        ; Temporary load address (linear)
    mov edi, KERNEL_LOAD_ADDR   ; 0x200000

.copy_loop:
    mov eax, [esi]          ; Read from source (uses DS with 4GB limit)
    mov [edi], eax          ; Write to dest (uses DS with 4GB limit)
    add esi, 4
    add edi, 4
    dec ecx
    jnz .copy_loop

    ; Restore DS to our code segment (needed for rest of bootloader)
    mov ax, 0x1000
    mov ds, ax

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
; After return: DS has 4GB limit with base=0, ES unchanged
; -----------------------------------------------------------------------------
enter_unreal_mode:
    cli
    lgdt [gdt_descriptor]

    ; Enter protected mode
    mov eax, cr0
    or al, 1
    mov cr0, eax

    ; Load DS with 4GB data selector - this caches the 4GB limit
    mov bx, 0x10            ; Data segment selector (4GB limit, base=0)
    mov ds, bx

    ; Exit protected mode - the DS descriptor cache keeps 4GB limit!
    and al, 0xFE
    mov cr0, eax

    ; Now DS register = 0x10, but descriptor cache has 4GB limit, base=0
    ; We do NOT reload DS here - that would destroy the 4GB limit

    sti
    ret

; -----------------------------------------------------------------------------
; Prepare boot info structure
; Fills in the boot_info structure at BOOT_INFO_ADDR
; -----------------------------------------------------------------------------
prepare_boot_info:
    push es
    push di

    ; Set up ES:DI to point to boot info structure
    xor ax, ax
    mov es, ax
    mov di, BOOT_INFO_ADDR

    ; Magic number: "KURIS" = 0x4B55524953
    mov word [es:di + BOOT_INFO_MAGIC], 0x4953      ; "IS"
    mov word [es:di + BOOT_INFO_MAGIC + 2], 0x5552  ; "RU"
    mov byte [es:di + BOOT_INFO_MAGIC + 4], 0x4B    ; "K"
    mov byte [es:di + BOOT_INFO_MAGIC + 5], 0x00
    mov word [es:di + BOOT_INFO_MAGIC + 6], 0x00

    ; Version
    mov dword [es:di + BOOT_INFO_VERSION], BOOT_PROTOCOL_VERSION
    mov dword [es:di + BOOT_INFO_VERSION + 4], 0

    ; Flags - BIOS boot
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

    ; Use jmp instead of ret to work around unreal mode issue
    add sp, 2           ; Discard return address
    jmp stage2_entry.continue_after_bootinfo

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

    ; DEBUG: 'P' = entered protected mode
    mov word [0xB8000], 0x0F50

    ; Check for long mode support
    call check_long_mode
    test eax, eax
    jz no_long_mode

    ; DEBUG: 'L' = long mode supported
    mov word [0xB8002], 0x0F4C

    ; Set up paging for long mode
    call setup_paging

    ; DEBUG: 'G' = paging tables set up
    mov word [0xB8004], 0x0F47

    ; Enable PAE
    mov eax, cr4
    or eax, (1 << 5)        ; PAE bit
    mov cr4, eax

    ; DEBUG: 'A' = PAE enabled
    mov word [0xB8006], 0x0F41

    ; Load PML4 address into CR3
    mov eax, 0x1000         ; PML4 table at 0x1000
    mov cr3, eax

    ; DEBUG: '3' = CR3 loaded
    mov word [0xB8008], 0x0F33

    ; Enable long mode via EFER MSR
    mov ecx, 0xC0000080     ; EFER MSR
    rdmsr
    or eax, (1 << 8)        ; LME bit
    wrmsr

    ; DEBUG: 'E' = EFER set
    mov word [0xB800A], 0x0F45

    ; Enable paging (enters long mode)
    mov eax, cr0
    or eax, (1 << 31)       ; PG bit
    mov cr0, eax

    ; DEBUG: If we get here, paging is enabled - '!'
    mov word [0xB800C], 0x0F21

    ; Jump to 64-bit code
    ; Set up far pointer with correct linear address
    mov dword [lm_jump_target], long_mode + 0x10000
    mov word [lm_jump_target + 4], 0x18
    jmp far [lm_jump_target]

no_long_mode:
    ; Print 'N' for no long mode
    mov word [0xB8000], 0x0C4E
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
; Set up paging for long mode:
; 1. Identity map first 4GB (for bootloader and early kernel)
; 2. Map higher half (0xFFFFFFFF80000000) to kernel at 1MB physical
; Uses 2MB pages for simplicity
; -----------------------------------------------------------------------------
setup_paging:
    push edi
    push ecx
    push eax
    push ebx

    ; Clear page tables area (0x1000 - 0x9000)
    mov edi, 0x1000
    mov ecx, 0x8000 / 4
    xor eax, eax
    rep stosd

    ; =========================================================================
    ; Identity mapping for first 4GB
    ; =========================================================================

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

    ; Fill PD entries with 2MB pages (identity mapping)
    mov edi, 0x3000
    mov eax, 0x0000_0083     ; Present + Writable + 2MB page (PS bit)
    mov ecx, 512 * 4         ; 512 entries * 4 page directories

.fill_pd:
    mov [edi], eax
    add eax, 0x200000        ; Next 2MB
    add edi, 8
    dec ecx
    jnz .fill_pd

    ; =========================================================================
    ; Higher-half mapping for kernel at 0xFFFFFFFF80000000
    ; Virtual 0xFFFFFFFF80000000 -> Physical 0x100000 (1MB)
    ;
    ; Address breakdown for 0xFFFFFFFF80000000:
    ;   PML4 index:  (addr >> 39) & 0x1FF = 511
    ;   PDPT index:  (addr >> 30) & 0x1FF = 510
    ;   PD index:    (addr >> 21) & 0x1FF = 0
    ; =========================================================================

    ; PML4[511] -> Higher-half PDPT at 0x7000
    mov dword [0x1000 + 511*8], 0x7003      ; Present + Writable
    mov dword [0x1000 + 511*8 + 4], 0       ; High 32 bits = 0

    ; Higher-half PDPT[510] -> Higher-half PD at 0x8000
    mov dword [0x7000 + 510*8], 0x8003      ; Present + Writable
    mov dword [0x7000 + 510*8 + 4], 0       ; High 32 bits = 0

    ; Higher-half PD: Map 128MB starting at physical 0x200000 (2MB aligned!)
    ; This gives us room for kernel + page array + future heap
    ; PD[0] -> 0x200000, PD[1] -> 0x400000, etc.
    mov edi, 0x8000
    mov eax, 0x00200083      ; Physical 0x200000 + Present + Writable + 2MB page
    mov ecx, 64              ; Map 64 * 2MB = 128MB

.fill_kernel_pd:
    mov [edi], eax
    mov dword [edi + 4], 0   ; High 32 bits = 0
    add eax, 0x200000        ; Next 2MB physical
    add edi, 8
    dec ecx
    jnz .fill_kernel_pd

    pop ebx

    pop eax
    pop ecx
    pop edi
    ret

; =============================================================================
; 64-BIT LONG MODE CODE
; =============================================================================
[BITS 64]

; Higher-half kernel constants
KERNEL_VIRT_BASE    equ 0xFFFFFFFF80000000

long_mode:
    ; Set up 64-bit segments
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Set up temporary stack (will be replaced by kernel)
    mov rsp, 0x90000

    ; Initialize serial port for debug output
    mov dx, 0x3F8 + 1       ; COM1 + Interrupt Enable Register
    xor al, al
    out dx, al              ; Disable interrupts

    mov dx, 0x3F8 + 3       ; Line Control Register
    mov al, 0x80
    out dx, al              ; Enable DLAB

    mov dx, 0x3F8           ; Divisor Latch Low
    mov al, 3               ; 38400 baud (115200/3)
    out dx, al

    mov dx, 0x3F8 + 1       ; Divisor Latch High
    xor al, al
    out dx, al

    mov dx, 0x3F8 + 3       ; Line Control Register
    mov al, 0x03            ; 8 bits, no parity, 1 stop bit
    out dx, al

    mov dx, 0x3F8 + 2       ; FIFO Control Register
    mov al, 0xC7            ; Enable FIFO, clear, 14-byte threshold
    out dx, al

    mov dx, 0x3F8 + 4       ; Modem Control Register
    mov al, 0x0B            ; RTS, DTR, OUT2
    out dx, al

    ; Send "64" to serial
    mov dx, 0x3F8
    mov al, '6'
    out dx, al
    mov al, '4'
    out dx, al
    mov al, 10              ; newline
    out dx, al

    ; Pass boot info pointer in RDI (System V ABI)
    mov rdi, BOOT_INFO_ADDR

    ; Jump to kernel at higher-half virtual address
    mov rax, KERNEL_VIRT_BASE
    call rax

    ; Kernel should never return - halt if it does
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
    dd gdt_start + 0x10000      ; Linear address (ORG 0 offset + segment base 0x10000)

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

; Far jump targets (6 bytes each: 4 byte offset + 2 byte segment)
pm_jump_target:     dd 0            ; 32-bit offset
                    dw 0            ; 16-bit segment
lm_jump_target:     dd 0            ; 32-bit offset
                    dw 0            ; 16-bit segment

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
msg_debug_d:        db "D", 0
msg_debug_v:        db "V", 0

; Pad to sector boundary
times 8192 - ($ - $$) db 0
