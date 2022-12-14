format ELF64
public __switch_thread__

section '.code' executable
__switch_thread__:
    ; save previous context
    push rdi
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    pushf

    ; change pointer to previous context
    mov [rdi], rsp
    ; set pointer to new context
    mov rsp, rsi

    ; load new context
    popf
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    pop rdi
    ret
