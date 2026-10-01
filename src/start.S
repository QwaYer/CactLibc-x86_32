.section .text
.global _start
.type _start, @function

_start:
    /* i386 PIC: PLT uses `jmp *disp(%ebx)`; ebx must point at _GLOBAL_OFFSET_TABLE_. */
    call    1f
1:
    popl    %ebx
    addl    $(_GLOBAL_OFFSET_TABLE_ - 1b), %ebx

    /* The kernel (and, for a PT_INTERP image, ld.so handing back the original
     * stack) leaves esp wherever the argv/envp string copies ended, which is
     * not 16-byte aligned.  clang may emit `movaps` into aligned stack slots,
     * so establish the i386 ABI guarantee — esp ≡ 0 (mod 16) right before a
     * `call` — here.  ebp keeps the original pointer to argc/argv/envp. */
    movl    %esp, %ebp
    andl    $-16, %esp

    /* Seed `environ` from the exec-provided envp array before main().  12 bytes
     * of padding + the pushed arg keep the call 16-byte aligned. */
    movl    8(%ebp), %eax       /* envp */
    testl   %eax, %eax
    jz      2f
    subl    $12, %esp
    pushl   %eax
    call    __cact_init_env@PLT
    addl    $16, %esp
2:
    movl    (%ebp),  %eax       /* argc */
    movl    4(%ebp), %ecx       /* argv */
    movl    8(%ebp), %edx       /* envp */
    subl    $4, %esp            /* 4 bytes of padding + 3 args = 16 */
    pushl   %edx
    pushl   %ecx
    pushl   %eax
    call    main@PLT
    addl    $16, %esp
    subl    $12, %esp           /* 12 bytes of padding + the pushed arg = 16 */
    pushl   %eax
    call    exit@PLT
    hlt

.section .note.GNU-stack,"",@progbits
