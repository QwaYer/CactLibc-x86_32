.section .text
.global _start
.type _start, @function

_start:
    /* i386 PIC: PLT uses `jmp *disp(%ebx)`; ebx must point at _GLOBAL_OFFSET_TABLE_. */
    call    1f
1:
    popl    %ebx
    addl    $(_GLOBAL_OFFSET_TABLE_ - 1b), %ebx

    movl (%esp),  %eax       /* argc */
    movl 4(%esp), %ecx       /* argv */
    movl 8(%esp), %edx       /* envp */
    pushl %edx
    pushl %ecx
    pushl %eax
    call main@PLT
    addl $12, %esp
    pushl %eax
    call exit@PLT
    hlt

.section .note.GNU-stack,"",@progbits
