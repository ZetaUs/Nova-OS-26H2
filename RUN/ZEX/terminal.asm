[bits 64]
org 0x500000
BSS_SIZE  equ 4096

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
    mov rdi, win_title_tm
    mov rsi, 580
    mov rdx, 440
    syscall

    mov rax, 1
    mov rdi, msg_tm
    syscall

    mov rax, 2
    syscall

    mov rax, 0
    mov rdi, 0
    syscall
code_end:

data_start:
win_title_tm db "终端",0
msg_tm       db "[终端] 运行命令、启动 zex、调用 slb 工具链",0
data_end:
