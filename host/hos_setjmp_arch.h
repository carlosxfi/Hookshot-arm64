/* Host register preservation for lifted setjmp/longjmp. Included once per translation unit. */
#ifdef _WIN32
/* buf: rbx rbp rdi rsi r12 r13 r14 r15 at 0-56, rsp (after the return) at 64, the return address at 72, xmm6-15 at
   80-224, mxcsr at 240 and the x87 control word at 244 */
__attribute__((naked, returns_twice)) int recomp_sj_setjmp(void **buf) {
    __asm__ volatile(
        "movq %rbx, 0(%rcx)\n\t"
        "movq %rbp, 8(%rcx)\n\t"
        "movq %rdi, 16(%rcx)\n\t"
        "movq %rsi, 24(%rcx)\n\t"
        "movq %r12, 32(%rcx)\n\t"
        "movq %r13, 40(%rcx)\n\t"
        "movq %r14, 48(%rcx)\n\t"
        "movq %r15, 56(%rcx)\n\t"
        "leaq 8(%rsp), %rdx\n\t"
        "movq %rdx, 64(%rcx)\n\t"
        "movq (%rsp), %rdx\n\t"
        "movq %rdx, 72(%rcx)\n\t"
        "movdqu %xmm6, 80(%rcx)\n\t"
        "movdqu %xmm7, 96(%rcx)\n\t"
        "movdqu %xmm8, 112(%rcx)\n\t"
        "movdqu %xmm9, 128(%rcx)\n\t"
        "movdqu %xmm10, 144(%rcx)\n\t"
        "movdqu %xmm11, 160(%rcx)\n\t"
        "movdqu %xmm12, 176(%rcx)\n\t"
        "movdqu %xmm13, 192(%rcx)\n\t"
        "movdqu %xmm14, 208(%rcx)\n\t"
        "movdqu %xmm15, 224(%rcx)\n\t"
        "stmxcsr 240(%rcx)\n\t"
        "fnstcw 244(%rcx)\n\t"
        "xorl %eax, %eax\n\t"
        "ret\n\t");
}

__attribute__((naked, noreturn)) static void recomp_sj_longjmp(void **buf, int value) {
    __asm__ volatile(
        "movq 0(%rcx), %rbx\n\t"
        "movq 8(%rcx), %rbp\n\t"
        "movq 16(%rcx), %rdi\n\t"
        "movq 24(%rcx), %rsi\n\t"
        "movq 32(%rcx), %r12\n\t"
        "movq 40(%rcx), %r13\n\t"
        "movq 48(%rcx), %r14\n\t"
        "movq 56(%rcx), %r15\n\t"
        "movdqu 80(%rcx), %xmm6\n\t"
        "movdqu 96(%rcx), %xmm7\n\t"
        "movdqu 112(%rcx), %xmm8\n\t"
        "movdqu 128(%rcx), %xmm9\n\t"
        "movdqu 144(%rcx), %xmm10\n\t"
        "movdqu 160(%rcx), %xmm11\n\t"
        "movdqu 176(%rcx), %xmm12\n\t"
        "movdqu 192(%rcx), %xmm13\n\t"
        "movdqu 208(%rcx), %xmm14\n\t"
        "movdqu 224(%rcx), %xmm15\n\t"
        "ldmxcsr 240(%rcx)\n\t"
        "fldcw 244(%rcx)\n\t"
        "movl %edx, %eax\n\t"
        "testl %eax, %eax\n\t"
        "jnz 1f\n\t"
        "incl %eax\n\t"
        "1:\n\t"
        "movq 64(%rcx), %rsp\n\t"
        "jmpq *72(%rcx)\n\t");
}

#elif defined(__aarch64__) && defined(__linux__)
/* AAPCS64: x19-x30, SP, d8-d15, FPCR and FPSR. The 184-byte frame fits the existing 256-byte slot.
   The register set follows TotK Recomp's ARM64 fiber switch; this saves into Hookshot's slot rather than
   switching to a fiber stack. See host/ARM64_PROVENANCE.md. */
__attribute__((naked, returns_twice)) int recomp_sj_setjmp(void **buf) {
    __asm__ volatile(
        "stp x19, x20, [x0, #0]\n\t"
        "stp x21, x22, [x0, #16]\n\t"
        "stp x23, x24, [x0, #32]\n\t"
        "stp x25, x26, [x0, #48]\n\t"
        "stp x27, x28, [x0, #64]\n\t"
        "stp x29, x30, [x0, #80]\n\t"
        "mov x2, sp\n\t"
        "str x2, [x0, #96]\n\t"
        "stp d8, d9, [x0, #104]\n\t"
        "stp d10, d11, [x0, #120]\n\t"
        "stp d12, d13, [x0, #136]\n\t"
        "stp d14, d15, [x0, #152]\n\t"
        "mrs x2, fpcr\n\t"
        "mrs x3, fpsr\n\t"
        "stp x2, x3, [x0, #168]\n\t"
        "mov w0, wzr\n\t"
        "ret\n\t");
}

__attribute__((naked, noreturn)) static void recomp_sj_longjmp(void **buf, int value) {
    __asm__ volatile(
        "ldp x19, x20, [x0, #0]\n\t"
        "ldp x21, x22, [x0, #16]\n\t"
        "ldp x23, x24, [x0, #32]\n\t"
        "ldp x25, x26, [x0, #48]\n\t"
        "ldp x27, x28, [x0, #64]\n\t"
        "ldp x29, x30, [x0, #80]\n\t"
        "ldr x2, [x0, #96]\n\t"
        "ldp d8, d9, [x0, #104]\n\t"
        "ldp d10, d11, [x0, #120]\n\t"
        "ldp d12, d13, [x0, #136]\n\t"
        "ldp d14, d15, [x0, #152]\n\t"
        "ldp x3, x4, [x0, #168]\n\t"
        "msr fpcr, x3\n\t"
        "msr fpsr, x4\n\t"
        "mov sp, x2\n\t"
        "cmp w1, #0\n\t"
        "csinc w0, w1, wzr, ne\n\t"
        "ret\n\t");
}

#elif defined(__x86_64__)
/* (linux) the System V ABI's: the buffer as above, its callee-saved registers only (rbx rbp r12-r15: rdi, rsi and the
   xmm registers are the caller's to keep there), buf in rdi and the value in esi */
__attribute__((naked, returns_twice)) int recomp_sj_setjmp(void **buf) {
    __asm__ volatile(
        "movq %rbx, 0(%rdi)\n\t"
        "movq %rbp, 8(%rdi)\n\t"
        "movq %r12, 32(%rdi)\n\t"
        "movq %r13, 40(%rdi)\n\t"
        "movq %r14, 48(%rdi)\n\t"
        "movq %r15, 56(%rdi)\n\t"
        "leaq 8(%rsp), %rdx\n\t"
        "movq %rdx, 64(%rdi)\n\t"
        "movq (%rsp), %rdx\n\t"
        "movq %rdx, 72(%rdi)\n\t"
        "stmxcsr 240(%rdi)\n\t"
        "fnstcw 244(%rdi)\n\t"
        "xorl %eax, %eax\n\t"
        "ret\n\t");
}

__attribute__((naked, noreturn)) static void recomp_sj_longjmp(void **buf, int value) {
    __asm__ volatile(
        "movq 0(%rdi), %rbx\n\t"
        "movq 8(%rdi), %rbp\n\t"
        "movq 32(%rdi), %r12\n\t"
        "movq 40(%rdi), %r13\n\t"
        "movq 48(%rdi), %r14\n\t"
        "movq 56(%rdi), %r15\n\t"
        "ldmxcsr 240(%rdi)\n\t"
        "fldcw 244(%rdi)\n\t"
        "movl %esi, %eax\n\t"
        "testl %eax, %eax\n\t"
        "jnz 1f\n\t"
        "incl %eax\n\t"
        "1:\n\t"
        "movq 64(%rdi), %rsp\n\t"
        "jmpq *72(%rdi)\n\t");
}

#else
#error "Hookshot nonlocal jumps require Linux AArch64 or x86-64"
#endif
