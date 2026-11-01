[bits 64]
org 0x4C0000
BSS_SIZE  equ 1024

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
    mov rdi, win_title_st
    mov rsi, 480
    mov rdx, 340
    syscall

    mov rax, 1
    mov rdi, msg_st
    syscall

    mov rax, 2
    syscall

    mov rax, 0
    mov rdi, 0
    syscall
code_end:

data_start:
win_title_st db "设置",0
msg_st       db "[设置] 调整系统参数、外观、驱动",0
data_end:
