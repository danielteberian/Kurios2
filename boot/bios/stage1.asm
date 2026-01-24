; stage1.asm - BIOS Boot Sector (Stage 1)
; Loaded by BIOS at 0x7C00
; Purpose: Initialize segments, load Stage 2 from disk, jump to it

[BITS 16]
[ORG 0x7C00]

%include "boot/common/boot_info.inc"

; -----------------------------------------------------------------------------
; Entry point - BIOS jumps here
; -----------------------------------------------------------------------------
start:
    ; Disable interrupts during setup
    cli

    ; Set up segments
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, STACK_TOP       ; Stack just below boot sector

    ; Save boot drive number
    mov [boot_drive], dl

    ; Enable interrupts
    sti

    ; Print loading message
    mov si, msg_loading
    call print_string

    ; Reset disk system
    xor ax, ax
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    ; Load Stage 2 from disk
    ; Stage 2 starts at sector 2 (sector 1 is this boot sector)
    ; Load 32 sectors (16KB) - enough for stage 2
    mov ah, 0x02            ; BIOS read sectors
    mov al, 32              ; Number of sectors to read
    mov ch, 0               ; Cylinder 0
    mov cl, 2               ; Start from sector 2 (1-indexed)
    mov dh, 0               ; Head 0
    mov dl, [boot_drive]    ; Drive number
    mov bx, STAGE2_LOAD_SEG
    mov es, bx
    mov bx, STAGE2_LOAD_OFF ; ES:BX = destination
    int 0x13
    jc disk_error

    ; Verify we read the expected number of sectors
    cmp al, 32
    jne disk_error

    ; Print success message
    mov si, msg_loaded
    call print_string

    ; Jump to Stage 2
    ; Pass boot drive in DL
    mov dl, [boot_drive]
    jmp STAGE2_LOAD_SEG:STAGE2_LOAD_OFF

; -----------------------------------------------------------------------------
; Error handlers
; -----------------------------------------------------------------------------
disk_error:
    mov si, msg_disk_err
    call print_string
    jmp halt

halt:
    cli
    hlt
    jmp halt

; -----------------------------------------------------------------------------
; Print null-terminated string
; Input: SI = pointer to string
; -----------------------------------------------------------------------------
print_string:
    push ax
    push bx
    mov ah, 0x0E            ; BIOS teletype output
    mov bh, 0               ; Page 0
.loop:
    lodsb                   ; Load next character
    test al, al             ; Check for null terminator
    jz .done
    int 0x10                ; Print character
    jmp .loop
.done:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; Data
; -----------------------------------------------------------------------------
boot_drive:     db 0
msg_loading:    db "Kurios2 Stage1", 0x0D, 0x0A, 0
msg_loaded:     db "Loading Stage2...", 0x0D, 0x0A, 0
msg_disk_err:   db "Disk error!", 0x0D, 0x0A, 0

; -----------------------------------------------------------------------------
; Boot sector padding and signature
; -----------------------------------------------------------------------------
times 510 - ($ - $$) db 0   ; Pad to 510 bytes
dw 0xAA55                   ; Boot signature
