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
    cld
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
    call save_vbe_mode_info
    jc vbe_error

    mov byte [bochs_vbe_available], 0
    cmp byte [vbe_mode_info + 25], 32
    jne .select_standard_mode
    cmp byte [vbe_mode_info + 27], 6
    jne .select_standard_mode
    mov dx, 0x01CE
    xor ax, ax
    out dx, ax
    inc dx
    in ax, dx
    cmp ax, 0xB0C5
    jne .select_standard_mode
    mov byte [bochs_vbe_available], 1
    mov ax, 0x4F02
    mov bx, 0x4118
    int 0x10
    cmp ax, 0x004F
    je .video_mode_ready
    mov byte [bochs_vbe_available], 0

.select_standard_mode:
    call find_vbe_32bit_mode
    jc vbe_error

.video_mode_ready:
    cmp byte [boot_drive], 0xE0
    jae .kernel_preloaded
    mov ah, 0x08
    mov dl, [boot_drive]
    int 0x13
    jc kernel_error
    and cl, 0x3F
    mov [sectors_per_track], cl
    inc dh
    mov [heads_per_cylinder], dh

    mov dword [kernel_lba], 9
    mov word [kernel_segment], 0x1000
    mov word [kernel_sectors_left], 256
.load_kernel_chunk:
    mov ax, [kernel_lba]
    xor dx, dx
    xor cx, cx
    mov cl, [sectors_per_track]
    div cx
    inc dl
    mov [chs_sector], dl
    xor dx, dx
    xor cx, cx
    mov cl, [heads_per_cylinder]
    div cx
    mov dh, dl
    mov ch, al
    mov cl, [chs_sector]
    mov ax, [kernel_segment]
    mov es, ax
    xor bx, bx
    mov dl, [boot_drive]
    mov ax, 0x0201
    int 0x13
    jc kernel_error
    inc dword [kernel_lba]
    add word [kernel_segment], 0x20
    dec word [kernel_sectors_left]
    jnz .load_kernel_chunk

.kernel_preloaded:
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

save_vbe_mode_info:
    mov eax, [vbe_mode_info + 40]
    test eax, eax
    jz .invalid
    mov [framebuffer_base], eax
    movzx eax, word [vbe_mode_info + 18]
    test eax, eax
    jz .invalid
    mov [screen_width], eax
    movzx eax, word [vbe_mode_info + 20]
    test eax, eax
    jz .invalid
    mov [screen_height], eax
    movzx eax, word [vbe_mode_info + 50]
    test eax, eax
    jnz .linear_pitch
    movzx eax, word [vbe_mode_info + 16]
.linear_pitch:
    test eax, eax
    jz .invalid
    mov [screen_pitch], eax
    clc
    ret
.invalid:
    stc
    ret

find_vbe_32bit_mode:
    xor ax, ax
    mov es, ax
    mov di, vbe_controller_info
    mov dword [vbe_controller_info], 0x32454256
    mov ax, 0x4F00
    int 0x10
    cmp ax, 0x004F
    jne .not_found

    mov ax, [vbe_controller_info + 16]
    mov [mode_list_segment], ax
    mov ax, [vbe_controller_info + 14]
    mov [mode_list_offset], ax
    mov dword [best_mode_score], 0
    mov word [best_mode], 0xFFFF
    mov word [mode_scan_count], 0

.scan_mode:
    cmp word [mode_scan_count], 512
    jae .scan_complete
    inc word [mode_scan_count]
    mov ax, [mode_list_segment]
    mov es, ax
    mov di, [mode_list_offset]
    mov cx, [es:di]
    add word [mode_list_offset], 2
    cmp cx, 0xFFFF
    je .scan_complete
    and cx, 0x3FFF
    mov [candidate_mode], cx

    xor ax, ax
    mov es, ax
    mov di, vbe_mode_info
    mov ax, 0x4F01
    int 0x10
    cmp ax, 0x004F
    jne .scan_mode
    test word [vbe_mode_info], 0x0081
    jz .scan_mode
    cmp byte [vbe_mode_info + 25], 32
    jne .scan_mode
    cmp byte [vbe_mode_info + 27], 6
    jne .scan_mode
    cmp byte [vbe_mode_info + 31], 8
    jne .scan_mode
    cmp byte [vbe_mode_info + 32], 16
    jne .scan_mode
    cmp byte [vbe_mode_info + 33], 8
    jne .scan_mode
    cmp byte [vbe_mode_info + 34], 8
    jne .scan_mode
    cmp byte [vbe_mode_info + 35], 8
    jne .scan_mode
    cmp byte [vbe_mode_info + 36], 0
    jne .scan_mode
    cmp dword [vbe_mode_info + 40], 0
    je .scan_mode

    movzx eax, word [vbe_mode_info + 18]
    test eax, eax
    jz .scan_mode
    cmp eax, 1920
    ja .scan_mode
    movzx ecx, word [vbe_mode_info + 20]
    test ecx, ecx
    jz .scan_mode
    cmp ecx, 1080
    ja .scan_mode
    mov edx, eax
    shl edx, 2
    movzx eax, word [vbe_mode_info + 50]
    test eax, eax
    jnz .check_pitch
    movzx eax, word [vbe_mode_info + 16]
.check_pitch:
    cmp eax, edx
    jb .scan_mode

    movzx eax, word [vbe_mode_info + 18]
    movzx ecx, word [vbe_mode_info + 20]
    imul eax, ecx
    cmp eax, [best_mode_score]
    jbe .scan_mode
    mov [best_mode_score], eax
    mov ax, [candidate_mode]
    mov [best_mode], ax
    jmp .scan_mode

.scan_complete:
    cmp word [best_mode], 0xFFFF
    je .not_found
    mov cx, [best_mode]
    xor ax, ax
    mov es, ax
    mov di, vbe_mode_info
    mov ax, 0x4F01
    int 0x10
    cmp ax, 0x004F
    jne .not_found
    call save_vbe_mode_info
    jc .not_found
    mov bx, [best_mode]
    or bx, 0x4000
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    jne .not_found
    clc
    ret
.not_found:
    stc
    ret

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

kernel_lba: dd 0
kernel_segment: dw 0
kernel_sectors_left: dw 0
sectors_per_track: db 0
heads_per_cylinder: db 0
chs_sector: db 0
bochs_vbe_available: db 0
mode_list_segment: dw 0
mode_list_offset: dw 0
candidate_mode: dw 0
best_mode: dw 0
mode_scan_count: dw 0
best_mode_score: dd 0

boot_drive: db 0
vbe_error_message: db 'Nova OS: VBE mode unavailable', 0
kernel_error_message: db 'Nova OS: kernel read failed', 0
framebuffer_base: dd 0
screen_width: dd 0
screen_height: dd 0
screen_pitch: dd 0
align 4
vbe_mode_info: times 256 db 0
vbe_controller_info: times 512 db 0

bits 32
protected_mode_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x120000

    cmp byte [boot_drive], 0xE0
    jb .kernel_ready
    mov esi, 0x28DFF
    mov edi, 0x2FFFF
    mov ecx, 131072
    std
    rep movsb
    cld
.kernel_ready:

    ; Prefer the Bochs/QEMU full-HD mode; retain standard VBE mode otherwise.
    cmp byte [bochs_vbe_available], 1
    jne .graphics_configured

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
    mov dword [screen_width], 1920
    mov dword [screen_height], 1080
    mov dword [screen_pitch], 7680

.graphics_configured:

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
    mov ebx, eax
    and ebx, 0x001FFFFF
    mov eax, [screen_pitch]
    imul eax, [screen_height]
    add eax, ebx
    add eax, 0x001FFFFF
    shr eax, 21
    mov ecx, eax
    mov eax, [framebuffer_base]
    shr eax, 21
    shl eax, 3
    add eax, 0x102000
    mov edi, eax
.mark_framebuffer:
    mov eax, [edi]
    or eax, 0x18
    mov [edi], eax
    add edi, 8
    dec ecx
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
    mov eax, [rel screen_width]
    mov [rdi + 4], eax
    mov eax, [rel screen_height]
    mov [rdi + 8], eax
    mov eax, [rel screen_pitch]
    mov [rdi + 12], eax
    mov dword [rdi + 16], 32

    mov edx, [rel screen_width]
    mov r8d, [rel screen_height]
    mov r9d, [rel screen_pitch]
    mov rax, 0x10000
    jmp rax
