/* The two halves of a landing place, written out because no C construct says
 * "put the registers back and go there" without also unwinding.
 *
 * Both calling conventions are here. They agree about rbx, rbp and r12 to r15
 * belonging to the callee; Windows adds rdi and rsi, which are saved too, so
 * one buffer serves either. The first argument is the buffer -- rdi on the
 * System V convention, rcx on Windows -- and the second, the value the save
 * appears to answer with when it is landed on, is rsi or rdx.
 *
 * Nothing here is allowed to touch the red zone below the stack pointer, and
 * nothing does: the save writes only through the buffer.
 */

#include "evv_land.h"

#include <stdio.h>
#include <stdlib.h>

/* One landing place per name, kept here rather than in the machine.
 *
 * A name is the address of the buffer a rule planted its landing in, and the
 * same few addresses come round again and again: a frame comes off the frame
 * stack, so a rule at the same depth gets the same one. That is why nothing is
 * ever given back -- the table settles at as many entries as there are places
 * a landing is ever planted, which is dozens, and a name that comes round
 * again wants the same place anyway.
 *
 * One table per thread, so there is no lock on the way in. Two threads cannot
 * share a name: their frames come from different blocks.
 */
#define LAND_BUCKETS 256

typedef struct land_entry {
    uintptr_t          name;
    struct land_entry *next;
    unsigned long long saved[EVV_LAND_WORDS];
} land_entry;

static __thread land_entry *land_tab[LAND_BUCKETS];

void *evv_land_place(uintptr_t name)
{
    unsigned h = (unsigned)((name >> 4) & (LAND_BUCKETS - 1));
    land_entry *e;

    for (e = land_tab[h]; e != 0; e = e->next)
        if (e->name == name)
            return e->saved;

    /* Not the arena: the machine never holds one of these as a value, and it
       must go on existing after the frame that planted it has gone. */
    e = (land_entry *)calloc(1, sizeof *e);
    if (e == 0) {
        fprintf(stderr, "evv: no room for a landing place\n");
        abort();
    }
    e->name = name;
    e->next = land_tab[h];
    land_tab[h] = e;
    return e->saved;
}

#if defined(__x86_64__)

#if defined(_WIN32)
#define EVV_A1 "%rcx"
#define EVV_A2 "%edx"
#else
#define EVV_A1 "%rdi"
#define EVV_A2 "%esi"
#endif

__asm__(
".text\n"
".globl evv_land_save\n"
".globl evv_land_jump\n"

/* Answers nought. The stack pointer stored is the one this will have after it
   returns, and the address stored is where it returns to, so landing on it
   later arrives exactly where the save did. */
"evv_land_save:\n"
"    mov " EVV_A1 ", %rax\n"
"    mov %rbx,   0(%rax)\n"
"    mov %rbp,   8(%rax)\n"
"    mov %r12,  16(%rax)\n"
"    mov %r13,  24(%rax)\n"
"    mov %r14,  32(%rax)\n"
"    mov %r15,  40(%rax)\n"
"    mov %rdi,  48(%rax)\n"
"    mov %rsi,  56(%rax)\n"
"    lea 8(%rsp), %r11\n"
"    mov %r11,  64(%rax)\n"
"    mov (%rsp), %r11\n"
"    mov %r11,  72(%rax)\n"
"    xor %eax, %eax\n"
"    ret\n"

/* Puts them back and goes there, and the save appears to answer with the
   value. It never returns to its own caller. */
"evv_land_jump:\n"
"    mov " EVV_A1 ", %rax\n"
"    mov " EVV_A2 ", %r10d\n"
"    mov   0(%rax), %rbx\n"
"    mov   8(%rax), %rbp\n"
"    mov  16(%rax), %r12\n"
"    mov  24(%rax), %r13\n"
"    mov  32(%rax), %r14\n"
"    mov  40(%rax), %r15\n"
"    mov  48(%rax), %rdi\n"
"    mov  56(%rax), %rsi\n"
"    mov  72(%rax), %r11\n"
"    mov  64(%rax), %rsp\n"
"    mov  %r10d, %eax\n"
"    jmp *%r11\n"
);

#else

/* Where the registers are not x86-64, the C library will do: a thirty-two bit
   build has a pointer that fits a value, so nothing here has ever been the
   problem there. */
#include <setjmp.h>

int evv_land_save(void *place)
{
    return setjmp(*(jmp_buf *)place);
}

void evv_land_jump(void *place, int value)
{
    longjmp(*(jmp_buf *)place, value);
}

#endif
