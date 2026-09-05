/* ld.so _start: save the original user stack pointer, run the C bootstrap,
 * then jump to the relocated main entry point on the same original stack. */

.section .text
.globl _start
.type _start, @function

_start:
    movl %esp, %ebx          /* ebx = original stack pointer (points at argc) */
    andl $-16, %esp          /* align; ABI wants esp ≡ 0 right before `call` */
    subl $12, %esp           /* + pushed arg below = 16 bytes of callee args */
    pushl %ebx               /* arg1: pointer to argc */
    call ldso_entry
    addl $16, %esp
    movl %ebx, %esp          /* restore original user stack */
    testl %eax, %eax
    jz 1f
    jmp *%eax                /* enter the relocated main image */
1:
    call ldso_die            /* returns 0 from ldso_entry -> fatal */
2:
    hlt
    jmp 2b

.section .note.GNU-stack,"",@progbits
