bits 16
org 0x7C00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl
    cmp dl, 0xE0
    jae .stage2_preloaded
    mov dl, [boot_drive]
    mov ax, 0x0208
    mov bx, 0x7E00
    xor cx, cx
    mov cl, 2
    xor dh, dh
    int 0x13
    jc disk_error
.stage2_preloaded:
    jmp 0x0000:0x7E00

disk_error:
    mov si, error_message
    mov ah, 0x0E
.print:
    lodsb
    test al, al
    jz .halt
    int 0x10
    jmp .print
.halt:
    cli
    hlt
    jmp .halt

boot_drive: db 0
error_message: db 'Nova OS: boot disk error', 0

times 510-($-$$) db 0
dw 0xAA55
