[bits 64]
[org 0x400000]
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
    ; rax=6 打开文件管理器
    mov rax, 6
    syscall

    mov rax, 0
    syscall
code_end:

data_start:
data_end:
