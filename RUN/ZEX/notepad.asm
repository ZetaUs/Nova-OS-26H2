[bits 64]
org 0x440000
BSS_SIZE  equ 2048

zex_header:
    dw 0x575A
    dw 0x0002
    dq entry
    dq code_end - code_start
    dq data_end - data_start
    dq BSS_SIZE

code_start:
entry:
    mov rax, 5
    mov rdi, win_title_np
    mov rsi, 600
    mov rdx, 420
    syscall

    mov rax, 1
    mov rdi, msg_np
    syscall

    mov rax, 2
    syscall

    mov rax, 0
    mov rdi, 0
    syscall
code_end:

data_start:
win_title_np db "记事本",0
msg_np       db "[记事本] 编辑文本，保存为 .txt / .asm / .cfg",0
data_end:
