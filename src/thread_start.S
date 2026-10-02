.section .text
.global __cact_thread_exit_stub
.type __cact_thread_exit_stub, @function

/* Entered via `ret` from a thread's start routine, with its return value in
 * %eax and an unspecified stack (and an unspecified %ebx: the kernel starts a
 * new thread without a GOT pointer).  Realign the stack and call the hidden
 * __cact_thread_exit_body directly — a plain PC-relative call, so no PLT entry
 * has to read %ebx.  It never returns. */
__cact_thread_exit_stub:
    andl    $-16, %esp
    subl    $16, %esp
    movl    %eax, (%esp)
    call    __cact_thread_exit_body
    hlt
.size __cact_thread_exit_stub, . - __cact_thread_exit_stub

.section .note.GNU-stack,"",@progbits
