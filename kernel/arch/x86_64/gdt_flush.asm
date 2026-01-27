; gdt_flush.asm - GDT and TSS loading routines
; x86_64 assembly for loading the GDT and reloading segment registers

section .note.GNU-stack noalloc noexec nowrite progbits

section .text
bits 64

;------------------------------------------------------------------------------
; gdt_flush - Load the GDT and reload segment registers
;
; void gdt_flush(gdt_pointer_t *gdt_ptr);
;
; In 64-bit long mode:
; - CS must be reloaded via a far return
; - DS, ES, SS can be loaded directly
; - FS, GS are typically set to 0 (base address set via MSRs)
;------------------------------------------------------------------------------
global gdt_flush
gdt_flush:
    ; rdi = pointer to gdt_pointer_t structure
    lgdt [rdi]

    ; Reload data segments with kernel data selector (0x10)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    ; Set FS and GS to 0 (their base addresses are set via MSRs)
    xor ax, ax
    mov fs, ax
    mov gs, ax

    ; Reload CS via a far return
    ; We push the new CS and the return address, then do a far return
    pop rdi                     ; Get return address
    push 0x08                   ; Push kernel code segment selector
    push rdi                    ; Push return address
    retfq                       ; Far return (pops CS:RIP)

;------------------------------------------------------------------------------
; tss_flush - Load the Task Register with the TSS selector
;
; void tss_flush(uint16_t selector);
;------------------------------------------------------------------------------
global tss_flush
tss_flush:
    ; di = TSS selector (16-bit value in rdi)
    ltr di
    ret
