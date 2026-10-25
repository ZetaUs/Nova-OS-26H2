bits 16
org 0x7E00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [boot_drive], dl
    sti

    in al, 0x92
    or al, 2
    out 0x92, al

    mov ax, 0x0013
    int 0x10
    mov ax, 0x4F01
    mov cx, 0x0118
    mov di, vbe_mode_info
    int 0x10
    cmp ax, 0x004F
    jne vbe_error
    test word [vbe_mode_info], 0x0081
    jz vbe_error
    mov eax, [vbe_mode_info + 40]
    test eax, eax
    jz vbe_error
    mov [framebuffer_base], eax

    mov ax, 0x4F02
    mov bx, 0x4118
    int 0x10
    cmp ax, 0x004F
    jne vbe_error

    mov si, kernel_dap
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jc kernel_error

    mov word [wallpaper_remaining], 993
    ; Keep the wallpaper beyond the kernel's loaded image and zeroed BSS.
    mov word [wallpaper_dap + 6], 0x4000
    mov dword [wallpaper_dap + 8], 136
    mov dword [wallpaper_dap + 12], 0
.read_wallpaper:
    mov ax, [wallpaper_remaining]
    cmp ax, 64
    jbe .wallpaper_count_ready
    mov ax, 64
.wallpaper_count_ready:
    mov [wallpaper_chunk], ax
    mov [wallpaper_dap + 2], ax
    mov si, wallpaper_dap
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jc wallpaper_error

    movzx eax, word [wallpaper_chunk]
    sub [wallpaper_remaining], ax
    add [wallpaper_dap + 8], eax
    adc dword [wallpaper_dap + 12], 0
    shl ax, 5
    add [wallpaper_dap + 6], ax
    cmp word [wallpaper_remaining], 0
    jne .read_wallpaper

    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:protected_mode_entry

vbe_error:
    mov si, vbe_error_message
    jmp print_error

kernel_error:
    mov si, kernel_error_message
    jmp print_error

wallpaper_error:
    mov si, wallpaper_error_message

print_error:
    mov ah, 0x0E
.next:
    lodsb
    test al, al
    jz .halt
    int 0x10
    jmp .next
.halt:
    cli
    hlt
    jmp .halt

align 8
gdt_start:
    dq 0
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
    dq 0x00AF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

kernel_dap:
    db 0x10, 0
    dw 127
    dw 0
    dw 0x1000
    dd 9
    dd 0

wallpaper_dap:
    db 0x10, 0
    dw 0
    dw 0
    dw 0x4000
    dd 105
    dd 0

boot_drive: db 0
wallpaper_remaining: dw 0
wallpaper_chunk: dw 0
vbe_error_message: db 'Nova OS: VBE mode unavailable', 0
kernel_error_message: db 'Nova OS: kernel read failed', 0
wallpaper_error_message: db 'Nova OS: wallpaper read failed', 0
framebuffer_base: dd 0
align 4
vbe_mode_info: times 256 db 0

bits 32
protected_mode_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x120000

    ; Configure the Bochs/QEMU VBE extension for the 1920x1080x32 desktop.
    mov dx, 0x01CE
    xor ax, ax
    out dx, ax
    inc dx
    in ax, dx
    cmp ax, 0xB0C5
    jne mode_error

    mov dx, 0x01CE
    mov ax, 4
    out dx, ax
    inc dx
    xor ax, ax
    out dx, ax
    mov dx, 0x01CE
    mov ax, 1
    out dx, ax
    inc dx
    mov ax, 1920
    out dx, ax
    mov dx, 0x01CE
    mov ax, 2
    out dx, ax
    inc dx
    mov ax, 1080
    out dx, ax
    mov dx, 0x01CE
    mov ax, 3
    out dx, ax
    inc dx
    mov ax, 32
    out dx, ax
    mov dx, 0x01CE
    mov ax, 6
    out dx, ax
    inc dx
    mov ax, 1920
    out dx, ax
    mov dx, 0x01CE
    mov ax, 4
    out dx, ax
    inc dx
    mov ax, 0x0041
    out dx, ax

    ; Identity-map the first 4 GiB so the high-address VBE framebuffer is reachable.
    mov edi, 0x100000
    xor eax, eax
    mov ecx, 6144
    rep stosd
    mov dword [0x100000], 0x101003
    mov ecx, 0
.map_pdpt:
    mov eax, ecx
    shl eax, 12
    add eax, 0x102003
    mov [0x101000 + ecx * 8], eax
    inc ecx
    cmp ecx, 4
    jne .map_pdpt

    mov edi, 0x102000
    xor ebx, ebx
    mov ecx, 0
.map_page:
    mov eax, ebx
    or eax, 0x83
    mov [edi], eax
    mov dword [edi + 4], 0
    add ebx, 0x200000
    add edi, 8
    inc ecx
    cmp ecx, 2048
    jne .map_page

    ; Keep the linear framebuffer uncached while normal RAM remains write-back.
    mov eax, [framebuffer_base]
    mov edx, eax
    and edx, 0x001FFFFF
    add edx, 0x007E9000
    add edx, 0x001FFFFF
    shr edx, 21
    shr eax, 21
    shl eax, 3
    add eax, 0x102000
    mov edi, eax
.mark_framebuffer:
    mov eax, [edi]
    or eax, 0x18
    mov [edi], eax
    add edi, 8
    dec edx
    jnz .mark_framebuffer

    mov eax, cr4
    or eax, 0x20
    mov cr4, eax
    mov eax, 0x100000
    mov cr3, eax
    mov ecx, 0xC0000080
    rdmsr
    or eax, 0x100
    wrmsr
    mov eax, cr0
    or eax, 0x80000001
    mov cr0, eax
    jmp 0x18:long_mode_entry

mode_error:
    mov esi, mode_error_message
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

mode_error_message: db 'Nova OS: graphics mode failed', 0

bits 64
long_mode_entry:
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov rsp, 0x120008
    cld

    mov ecx, [rel framebuffer_base]
    mov edi, 0x5000
    mov [rdi], ecx
    mov dword [rdi + 4], 1920
    mov dword [rdi + 8], 1080
    mov dword [rdi + 12], 7680
    mov dword [rdi + 16], 32

    mov edx, 1920
    mov r8d, 1080
    mov r9d, 7680
    mov rax, 0x10000
    jmp rax
