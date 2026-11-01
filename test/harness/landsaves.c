/* Whether a landing puts back everything a function has to hand back.
 *
 * A landing skips every frame between the rule that jumps and the rule that
 * planted it, so none of their epilogues runs. Whatever those frames changed
 * in a register their callers trusted them to keep stays changed, unless the
 * landing puts it back itself -- and the planter then returns it to callers
 * that never knew a landing happened. Which registers those are is the calling
 * convention's to say, not the engine's: rbx, rbp and r12 to r15 on both,
 * and on Windows rdi, rsi, xmm6 to xmm15, the control half of MXCSR and the
 * x87 control word as well.
 *
 * The Windows half went unsaved until 1 November 2026, on the reasoning that
 * the rules are integer code and nothing holds a value in an SSE register.
 * The compiler disagreed: it packs pairs of integers into xmm registers and
 * keeps them in xmm6 across calls, in compiled rules that plant landings.
 *
 * So this loads a known value into every register the convention makes the
 * callee's, calls a function that plants a landing, has a deeper call change
 * every one of them and jump to it, and reads them all back once the planter
 * has returned. Every one has to be what it was.
 *
 * usage: landsaves        answers 0 only if every register came back
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "evv_land.h"

#if defined(__x86_64__)

#if defined(_WIN32)
#define NGPR 8
static const char *const gpr[NGPR] = {
    "rbx", "rbp", "rdi", "rsi", "r12", "r13", "r14", "r15"
};
#define NXMM 10
#else
#define NGPR 6
static const char *const gpr[NGPR] = {
    "rbx", "rbp", "r12", "r13", "r14", "r15"
};
#define NXMM 0
#endif

/* What goes in before the call, and what the deeper call puts there before it
   jumps. The two control words are rounding towards zero, where nothing else
   would leave them, and back to what Windows starts a thread with. */
uint64_t landsaves_want[NGPR + 2 * 10];
uint64_t landsaves_junk[NGPR + 2 * 10];
#define WANT_MXCSR 0x7f80u
#define WANT_FPCW  0x0e7fu

/* The registers as they came back: the general ones, then each xmm register
   as two halves, then MXCSR and the x87 control word. */
typedef struct {
    uint64_t gpr[NGPR];
    uint64_t xmm[2 * 10];
    uint32_t mxcsr;
    uint16_t fpcw;
} seen_regs;

void landsaves_run(void (*planter)(void), seen_regs *seen);
void landsaves_clobber_and_jump(void *place, int value)
    __attribute__((noreturn));

#if defined(_WIN32)

/* Planter in rcx, the record in rdx. The frame keeps the caller's own copies
   of everything it is about to load, the shadow space the call wants, the
   record's address and somewhere for the two control words. */
__asm__(
".text\n"
".globl landsaves_run\n"
"landsaves_run:\n"
"    push %rbx\n push %rbp\n push %rdi\n push %rsi\n"
"    push %r12\n push %r13\n push %r14\n push %r15\n"
"    sub $216, %rsp\n"
"    movdqu %xmm6,   56(%rsp)\n movdqu %xmm7,   72(%rsp)\n"
"    movdqu %xmm8,   88(%rsp)\n movdqu %xmm9,  104(%rsp)\n"
"    movdqu %xmm10, 120(%rsp)\n movdqu %xmm11, 136(%rsp)\n"
"    movdqu %xmm12, 152(%rsp)\n movdqu %xmm13, 168(%rsp)\n"
"    movdqu %xmm14, 184(%rsp)\n movdqu %xmm15, 200(%rsp)\n"
"    stmxcsr 32(%rsp)\n fnstcw 36(%rsp)\n"
"    mov %rdx, 40(%rsp)\n"
"    mov %rcx, %rax\n"
"    lea landsaves_want(%rip), %r11\n"
"    mov  0(%r11), %rbx\n mov  8(%r11), %rbp\n"
"    mov 16(%r11), %rdi\n mov 24(%r11), %rsi\n"
"    mov 32(%r11), %r12\n mov 40(%r11), %r13\n"
"    mov 48(%r11), %r14\n mov 56(%r11), %r15\n"
"    movdqu  64(%r11), %xmm6\n  movdqu  80(%r11), %xmm7\n"
"    movdqu  96(%r11), %xmm8\n  movdqu 112(%r11), %xmm9\n"
"    movdqu 128(%r11), %xmm10\n movdqu 144(%r11), %xmm11\n"
"    movdqu 160(%r11), %xmm12\n movdqu 176(%r11), %xmm13\n"
"    movdqu 192(%r11), %xmm14\n movdqu 208(%r11), %xmm15\n"
"    movl $0x7f80, 48(%rsp)\n ldmxcsr 48(%rsp)\n"
"    movw $0x0e7f, 52(%rsp)\n fldcw 52(%rsp)\n"
"    call *%rax\n"
"    mov 40(%rsp), %rax\n"
"    mov %rbx,  0(%rax)\n mov %rbp,  8(%rax)\n"
"    mov %rdi, 16(%rax)\n mov %rsi, 24(%rax)\n"
"    mov %r12, 32(%rax)\n mov %r13, 40(%rax)\n"
"    mov %r14, 48(%rax)\n mov %r15, 56(%rax)\n"
"    movdqu %xmm6,   64(%rax)\n movdqu %xmm7,   80(%rax)\n"
"    movdqu %xmm8,   96(%rax)\n movdqu %xmm9,  112(%rax)\n"
"    movdqu %xmm10, 128(%rax)\n movdqu %xmm11, 144(%rax)\n"
"    movdqu %xmm12, 160(%rax)\n movdqu %xmm13, 176(%rax)\n"
"    movdqu %xmm14, 192(%rax)\n movdqu %xmm15, 208(%rax)\n"
"    stmxcsr 224(%rax)\n fnstcw 228(%rax)\n"
"    ldmxcsr 32(%rsp)\n fldcw 36(%rsp)\n"
"    movdqu  56(%rsp), %xmm6\n  movdqu  72(%rsp), %xmm7\n"
"    movdqu  88(%rsp), %xmm8\n  movdqu 104(%rsp), %xmm9\n"
"    movdqu 120(%rsp), %xmm10\n movdqu 136(%rsp), %xmm11\n"
"    movdqu 152(%rsp), %xmm12\n movdqu 168(%rsp), %xmm13\n"
"    movdqu 184(%rsp), %xmm14\n movdqu 200(%rsp), %xmm15\n"
"    add $216, %rsp\n"
"    pop %r15\n pop %r14\n pop %r13\n pop %r12\n"
"    pop %rsi\n pop %rdi\n pop %rbp\n pop %rbx\n"
"    ret\n"

/* Place in rcx and value in rdx, which it leaves alone for the jump. It never
   returns, so it has nothing of its own to keep. */
".globl landsaves_clobber_and_jump\n"
"landsaves_clobber_and_jump:\n"
"    lea landsaves_junk(%rip), %r11\n"
"    mov  0(%r11), %rbx\n mov  8(%r11), %rbp\n"
"    mov 16(%r11), %rdi\n mov 24(%r11), %rsi\n"
"    mov 32(%r11), %r12\n mov 40(%r11), %r13\n"
"    mov 48(%r11), %r14\n mov 56(%r11), %r15\n"
"    movdqu  64(%r11), %xmm6\n  movdqu  80(%r11), %xmm7\n"
"    movdqu  96(%r11), %xmm8\n  movdqu 112(%r11), %xmm9\n"
"    movdqu 128(%r11), %xmm10\n movdqu 144(%r11), %xmm11\n"
"    movdqu 160(%r11), %xmm12\n movdqu 176(%r11), %xmm13\n"
"    movdqu 192(%r11), %xmm14\n movdqu 208(%r11), %xmm15\n"
"    sub $8, %rsp\n"
"    movl $0x1f80, (%rsp)\n ldmxcsr (%rsp)\n"
"    movw $0x027f, 4(%rsp)\n fldcw 4(%rsp)\n"
"    add $8, %rsp\n"
"    jmp evv_land_jump\n"
);

#else

/* Planter in rdi, the record in rsi. Nothing but the general registers is
   the callee's here, and glibc's setjmp keeps no more than they are. */
__asm__(
".text\n"
".globl landsaves_run\n"
".hidden landsaves_run\n"
"landsaves_run:\n"
"    push %rbx\n push %rbp\n push %r12\n push %r13\n push %r14\n push %r15\n"
"    sub $8, %rsp\n"
"    mov %rsi, (%rsp)\n"
"    mov %rdi, %rax\n"
"    lea landsaves_want(%rip), %r11\n"
"    mov  0(%r11), %rbx\n mov  8(%r11), %rbp\n"
"    mov 16(%r11), %r12\n mov 24(%r11), %r13\n"
"    mov 32(%r11), %r14\n mov 40(%r11), %r15\n"
"    call *%rax\n"
"    mov (%rsp), %rax\n"
"    mov %rbx,  0(%rax)\n mov %rbp,  8(%rax)\n"
"    mov %r12, 16(%rax)\n mov %r13, 24(%rax)\n"
"    mov %r14, 32(%rax)\n mov %r15, 40(%rax)\n"
"    add $8, %rsp\n"
"    pop %r15\n pop %r14\n pop %r13\n pop %r12\n pop %rbp\n pop %rbx\n"
"    ret\n"

".globl landsaves_clobber_and_jump\n"
".hidden landsaves_clobber_and_jump\n"
"landsaves_clobber_and_jump:\n"
"    lea landsaves_junk(%rip), %r11\n"
"    mov  0(%r11), %rbx\n mov  8(%r11), %rbp\n"
"    mov 16(%r11), %r12\n mov 24(%r11), %r13\n"
"    mov 32(%r11), %r14\n mov 40(%r11), %r15\n"
"    jmp evv_land_jump\n"
);

#endif

/* Sixty-four bytes, the size the machine plants one in. */
static unsigned long long name[8];
static volatile int answered;

static void planter(void)
{
    int v = EVV_LAND_SAVE((uintptr_t)name);

    if (v == 0)
        landsaves_clobber_and_jump(evv_land_planted((uintptr_t)name), 7);
    answered = v;
}

int main(void)
{
    seen_regs seen;
    int i, bad = 0;

    for (i = 0; i < NGPR + 2 * 10; i++) {
        landsaves_want[i] = 0x5a5a000000000000ull + 0x0101010101ull * (i + 1);
        landsaves_junk[i] = 0xa5a5000000000000ull + 0x0202020202ull * (i + 1);
    }
    memset(&seen, 0, sizeof seen);

    printf("landsaves: %d general, %d xmm registers and %s to come back\n",
           NGPR, NXMM, NXMM ? "both control words" : "no control words");
    fflush(stdout);
    landsaves_run(planter, &seen);

    if (answered != 7) {
        printf("landsaves: the planter answered %d, not the 7 jumped with\n",
               answered);
        bad++;
    }
    for (i = 0; i < NGPR; i++)
        if (seen.gpr[i] != landsaves_want[i]) {
            printf("landsaves: %s came back %s\n", gpr[i],
                   seen.gpr[i] == landsaves_junk[i] ? "as the deeper call left it"
                                                    : "as something else");
            bad++;
        }
    for (i = 0; i < NXMM; i++)
        if (seen.xmm[2 * i] != landsaves_want[NGPR + 2 * i] ||
            seen.xmm[2 * i + 1] != landsaves_want[NGPR + 2 * i + 1]) {
            printf("landsaves: xmm%d came back %s\n", 6 + i,
                   seen.xmm[2 * i] == landsaves_junk[NGPR + 2 * i]
                       ? "as the deeper call left it" : "as something else");
            bad++;
        }
    if (NXMM && (seen.mxcsr & 0xffc0u) != (WANT_MXCSR & 0xffc0u)) {
        printf("landsaves: MXCSR came back 0x%04x, not 0x%04x\n",
               seen.mxcsr, WANT_MXCSR);
        bad++;
    }
    if (NXMM && seen.fpcw != WANT_FPCW) {
        printf("landsaves: the x87 control word came back 0x%04x, not 0x%04x\n",
               seen.fpcw, WANT_FPCW);
        bad++;
    }

    if (bad) {
        printf("landsaves: FAILED, %d did not come back\n", bad);
        return 1;
    }
    printf("landsaves: every one came back\n");
    return 0;
}

#else

int main(void)
{
    printf("landsaves: a landing here is the C library's setjmp, so there is"
           " nothing of ours to check\n");
    return 0;
}

#endif
