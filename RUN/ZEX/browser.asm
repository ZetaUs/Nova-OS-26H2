[bits 64]
org 0x480000
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
    mov rdi, win_title_br
    mov rsi, 640
    mov rdx, 400
    syscall

    mov rax, 1
    mov rdi, msg_br
    syscall

    mov rax, 2
    syscall

    mov rax, 0
    mov rdi, 0
    syscall
code_end:

data_start:
win_title_br db "浏览器",0
msg_br       db "[浏览器] 访问网页资源",0
data_end:
