bits 64

global kernel_entry
global zex_interrupt_handler
global zex_exit_trampoline
global zex_call_entry
extern kernel_main
extern zex_syscall_dispatch

section .text
kernel_entry:
    mov rsp, 0x120008
    sub rsp, 40
    mov dword [rsp + 32], 32
    call kernel_main
.halt:
    cli
    hlt
    jmp .halt

zex_interrupt_handler:
    cld
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov r12, rsp
    mov rcx, rsp
    and rsp, -16
    sub rsp, 32
    call zex_syscall_dispatch
    mov rsp, r12
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
    iretq

zex_exit_trampoline:
    ret

zex_call_entry:
    push rbx
    push rbp
    push rdi
    push rsi
    push r12
    push r13
    push r14
    push r15
    sub rsp, 40
    call rcx
    add rsp, 40
    pop r15
    pop r14
    pop r13
    pop r12
    pop rsi
    pop rdi
    pop rbp
    pop rbx
    ret
