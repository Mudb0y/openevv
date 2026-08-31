/* Rules written as C rather than left as bytecode, and what they need of the
   machine they were written for.
 *
 * This is not generated. Each module's delta_rules_<lang>.h is, and anything
 * put there is lost the next time the lifter runs.
 */

#ifndef DELTA_RULES_C_H
#define DELTA_RULES_C_H

#include <stddef.h>
#include <stdint.h>

#include "evv_land.h"
#include "delta_lang.h"

/* The four flags the machine keeps, and the operations that set them. A rule
   written as C works them with the interpreter's own code, or a comparison
   after an operation would part company with it over what it says. */
#include "delta_flags.h"

/* One rule written as C, and the table of them. The interpreter looks there
   once it has said what it is about to run, so a rule can be swapped between
   the two without anything that calls it knowing, and the two can be set
   against each other by speaking the same text twice. */
typedef struct delta_rule_c {
    int            rule;
    delta_rule_cfn fn;
} delta_rule_c;

/* The call a rule makes, whichever way it is being run. Both go through here
   so that what a run says it did is the same either way, which is what a rule
   written as C is held against. */
int32_t delta_rule_direct(int which, const int32_t *a, int n);

/* The language's constants, as values rather than as addresses in the program.
   delta_syms_bind copies the stores into the arena and fills this in; a rule
   naming a constant reads it here, whichever way the rule is being run. See
   src/delta_syms.c for why the addresses in the program will not do. */
/* Whichever language's, bound once the arena exists. */
#define delta_sym_ref (*delta_lang_now()->sym_ref)
void delta_syms_bind(void);

/* A copy, in the arena, of something that lives in the program. The
   language's link tables hand the machine the addresses of arrays
   which are in the program, where a value cannot name them. */
void *delta_low_copy(const void *what, size_t bytes);

/* A store of the language's bytes, copied out of the program once, and the
   translation of an address in one into the address of its copy. Every place
   an address in the program becomes a value goes through this. */
void  delta_low_region(const void *at, size_t bytes);
void *delta_low_at(const void *p);

/* A double written as its bits, which is how the compiler named the constants
   the Frenches multiply and add by, so the value is had exactly rather than
   through a decimal that may not read back the same. */
static inline long double evv_dbl(unsigned long long bits)
{
    union { unsigned long long b; double d; } u;

    u.b = bits;
    return (long double)u.d;
}
#define EVV_DBL(bits) evv_dbl(bits)

int32_t delta_rule_called(int which, const int32_t *stack, int argn,
                          int want);

/* What a rule tests, works out and asks, under the names the
   machine's own operations carry. A rule reads better saying which
   comparison it made than saying that it made comparison four. */
enum {
    DELTA_IF_e = 0,
    DELTA_IF_ne = 1,
    DELTA_IF_a = 2,
    DELTA_IF_ae = 3,
    DELTA_IF_b = 4,
    DELTA_IF_be = 5,
    DELTA_IF_g = 6,
    DELTA_IF_ge = 7,
    DELTA_IF_l = 8,
    DELTA_IF_le = 9,
    DELTA_IF_s = 10,
    DELTA_IF_ns = 11,
};

enum {
    DELTA_CMP_testl = 0,
    DELTA_CMP_testw = 1,
    DELTA_CMP_testb = 2,
    DELTA_CMP_cmpl = 3,
    DELTA_CMP_cmpw = 4,
    DELTA_CMP_cmpb = 5,
};

enum {
    DELTA_ALU_addl = 0,
    DELTA_ALU_addw = 1,
    DELTA_ALU_subl = 2,
    DELTA_ALU_subw = 3,
    DELTA_ALU_andl = 4,
    DELTA_ALU_andw = 5,
    DELTA_ALU_orl = 6,
    DELTA_ALU_orw = 7,
    DELTA_ALU_incl = 8,
    DELTA_ALU_incw = 9,
    DELTA_ALU_decl = 10,
    DELTA_ALU_decw = 11,
    DELTA_ALU_shll = 12,
    DELTA_ALU_shlw = 13,
    DELTA_ALU_sarl = 14,
    DELTA_ALU_sarw = 15,
    DELTA_ALU_negl = 16,
    DELTA_ALU_negw = 17,
    DELTA_ALU_sbbl = 18,
    DELTA_ALU_imull = 19,
    DELTA_ALU_imulw = 20,
};

#define IF(cond)      delta_condition(&fl, DELTA_IF_##cond)
#define CMP(k, a, b)  delta_rule_cmp(&fl, DELTA_CMP_##k, (a), (b))
#define ALU(k, a, b)  delta_rule_alu(&fl, DELTA_ALU_##k, (a), (b))

/* Where a rule keeps its own working memory, and where the machine keeps
   what every rule shares. A rule names a place in either by the offset the
   language's compiler gave it, so these say which of the two is meant and
   leave the number alone.

   AT and FLD are the value in a place; SLOT and FIELD are the place itself,
   as something a rule can hand to a call. */
#define SLOT(n)      ((int32_t)(intptr_t)(base + (n)))
#define FIELD(n)     ((int32_t)(intptr_t)((unsigned char *)state + (n)))
#define AT(t, n)     (*(t *)(base + (n)))
#define FLD(t, n)    (*(t *)((unsigned char *)state + (n)))

/* A rule's own argument stack, and the two things it does with it. The
   machine pushes what a call is to be given and the call takes them from
   there, so these are what stands between a rule and every call it makes;
   written out in full they were a fifth of the decompiled C.

   How deep it goes is the rule's own, worked out by the decompiler and said
   in the size of the array itself, so the guards below follow it without
   being told. The interpreter gives every rule 64 words because it has one
   piece of code for all of them; here the median rule wants four. */
#define DELTA_RULE_ARGS ((int)(sizeof arg / sizeof arg[0]))

/* Taking one back off. The machine pops an argument into a register after a
   call, which is how it reads what the call left behind. */
/* The count is there because the machine let go of several at once and a
   decoder can only see that as one pop after another. Only the last of them
   is kept, which is what popping into the same register means. */
#define POP(r, n)  do { int k_ = (n); \
                        while (k_-- > 0) { if (argn > 0) { argn--; \
                            if (argn < DELTA_RULE_ARGS) (r) = arg[argn]; } } \
                   } while (0)

/* An expression rather than a statement, so that the pushes a call needs can
   sit inside the call itself. They stay in the order the machine made them,
   which is the reverse of the order the entry takes them: the last thing
   pushed is the first argument. */
#ifdef EVV_ARG_CHECK
void evv_arg_over(const char *who, int argn, int room);
#define ARG(x)  ((argn >= DELTA_RULE_ARGS \
                  ? evv_arg_over(__func__, argn, DELTA_RULE_ARGS) : (void)0), \
                 ((argn < DELTA_RULE_ARGS) \
                  ? (void)(arg[argn] = (int32_t)(x)) : (void)0), \
                 (void)argn++)
#else
#define ARG(x)  (((argn < DELTA_RULE_ARGS) \
                  ? (void)(arg[argn] = (int32_t)(x)) : (void)0), \
                 (void)argn++)
#endif
#define DROP(n) do { argn -= (n); if (argn < 0) argn = 0; } while (0)

/* What every rule does before its own work, in the two pieces the compiler
   emitted it as.

   LANDING plants the place a thrown error comes back to. ENTER tells the
   machine the rule has been entered and hands it the record to save what a
   backtrack must put back, the three fence arrays the rule is about to stand
   on, and that landing place. Both leave the answer in r0 with the flags set
   from it, so the line after either is the rule's own test of whether it may
   go on.

   The arguments are named in the order they are pushed, which is the reverse
   of the order ventproc takes them: the last thing pushed is the first
   argument. */
#define LANDING(jb) \
    do { r0 = SLOT(jb); ARG(0); ARG(SLOT(jb)); \
         { int32_t buf = (argn > 0) ? arg[argn - 1] : 0; int depth = argn; \
           r0 = EVV_LAND_SAVE((intptr_t)buf); \
           argn = depth; } \
         CMP(testl, r0, r0); } while (0)

#define ENTER(jb, marks, chars, index, rec) \
    do { r0 = SLOT(jb);    ARG(SLOT(jb)); \
         r0 = SLOT(marks); ARG(SLOT(marks)); \
         r0 = SLOT(chars); ARG(SLOT(chars)); \
         r0 = SLOT(index); ARG(SLOT(index)); \
         r0 = SLOT(rec);   ARG(SLOT(rec)); \
         ARG(FIELD(0)); \
         r0 = CALL(ventproc, 6); DROP(6); \
         CMP(testl, r0, r0); } while (0)

/* Leaving the rule with an answer. The frame was taken from the arena and
   has to go back before the answer does, so every way out says this rather
   than saying return.

   LEAVE is the same for a rule that took no frame from the arena. A rule
   needs one because it hands the machine the address of something in it, and
   an address the machine can hold is thirty-two bits, so it can only be the
   arena's; a rule that hands over no such address -- which is every wrapper,
   2,335 of the 3,377 -- keeps its few words of working memory on the stack
   like any other C function and has nothing to give back. */
#define RETURN(x) \
    do { int32_t out_ = (x); evv_frame_pop(frame); return out_; } while (0)

#define LEAVE(x)  return (x)

/* How a decompiled rule writes a call. The arguments are already on that
   stack, which is why they are not named here: what a call says is which
   entry it is and how many of them it takes. */
/* One of the arguments the rule was called with. They sit at the bottom of
   the frame, one word each, in the order they were handed over, and a rule
   reads them as often as anything else it has. Which offset that is depends
   on the rule, so the rule works it out once and these count from there. */
#define PARAM(t, k)  (*(t *)(param + 4 * (k)))
#define PARAMAT(k)   ((int32_t)(intptr_t)(param + 4 * (k)))

/* Part of a register. The machine had a sixteen-bit half and two eight-bit
   quarters of each of its registers, and a rule reads and writes them as
   freely as the whole. Spelling the masks out at every one of them buried the
   line the mask was on; these say which part, and the shifts stay here where
   they can be read once. Written this way rather than by pointing at the
   bytes, so that the port does not quietly depend on which end they are
   stored from. */
#define LOW(r)      ((int32_t)((uint32_t)(r) & 0xffffu))
#define BYTE0(r)    ((int32_t)((uint32_t)(r) & 0xffu))
#define BYTE1(r)    ((int32_t)(((uint32_t)(r) >> 8) & 0xffu))

#define SETLOW(r, x)   ((r) = (int32_t)(((uint32_t)(r) & 0xffff0000u) \
                                        | ((uint32_t)(x) & 0xffffu)))
#define SETBYTE0(r, x) ((r) = (int32_t)(((uint32_t)(r) & 0xffffff00u) \
                                        | ((uint32_t)(x) & 0xffu)))
#define SETBYTE1(r, x) ((r) = (int32_t)(((uint32_t)(r) & 0xffff00ffu) \
                                        | (((uint32_t)(x) & 0xffu) << 8)))

/* A global variable of the language, reached through the state pointer a
   rule is holding. delta_new lays the variables out in the tail of the state
   in declaration order and numbers each kind as it goes, and these are those
   numbers: w for a word, l for a long, s for a short, c for a compound. Two
   rules touching the same variable now say the same thing rather than two
   different byte offsets, and the offsets themselves are worked out the same
   way delta_new works them out, which is what makes the names true. */
#define GLOBAL(t, p, v) (*(t *)((unsigned char *)(intptr_t)(p) + DG_##v))

/* Both are the arity said out loud, because the arity is known where the
   call is written and working it out again at run time was a fifth of a run.
   src/delta_rules.c has one small function per arity and says why.

   CALL is a call the machine made: the arguments are on its argument area and
   the entry takes them the other way round from the order they were pushed.
   CALLW is a wrapper written out where it stood: its arguments are named
   here, in the order the entry takes them, and the site's own pushes stay
   above them untouched, because a call does not pop what it was given. */
#define EVV_ARITY(a1,a2,a3,a4,a5,a6,a7,a8,N,...) N
#define EVV_COUNT(...)  EVV_ARITY(__VA_ARGS__, 8, 7, 6, 5, 4, 3, 2, 1)
#define EVV_JOIN_(a, b) a##b
#define EVV_JOIN(a, b)  EVV_JOIN_(a, b)

#define CALLW(entry, ...) \
    EVV_JOIN(delta_direct_, EVV_COUNT(__VA_ARGS__)) \
        (DELTA_ENTRY_##entry, __VA_ARGS__)

#define CALL(entry, want) \
    EVV_JOIN(delta_call_, want)(DELTA_ENTRY_##entry, (int32_t *)arg, argn)

int32_t delta_call_0(int which, const int32_t *stack, int argn);
int32_t delta_call_1(int which, const int32_t *stack, int argn);
int32_t delta_call_2(int which, const int32_t *stack, int argn);
int32_t delta_call_3(int which, const int32_t *stack, int argn);
int32_t delta_call_4(int which, const int32_t *stack, int argn);
int32_t delta_call_5(int which, const int32_t *stack, int argn);
int32_t delta_call_6(int which, const int32_t *stack, int argn);
int32_t delta_call_7(int which, const int32_t *stack, int argn);
int32_t delta_call_8(int which, const int32_t *stack, int argn);
int32_t delta_call_9(int which, const int32_t *stack, int argn);
int32_t delta_call_10(int which, const int32_t *stack, int argn);
int32_t delta_call_11(int which, const int32_t *stack, int argn);
int32_t delta_call_12(int which, const int32_t *stack, int argn);
int32_t delta_call_13(int which, const int32_t *stack, int argn);
int32_t delta_call_14(int which, const int32_t *stack, int argn);
int32_t delta_call_15(int which, const int32_t *stack, int argn);
int32_t delta_call_16(int which, const int32_t *stack, int argn);
int32_t delta_call_17(int which, const int32_t *stack, int argn);
int32_t delta_call_18(int which, const int32_t *stack, int argn);
int32_t delta_call_19(int which, const int32_t *stack, int argn);
int32_t delta_call_20(int which, const int32_t *stack, int argn);
int32_t delta_call_21(int which, const int32_t *stack, int argn);
int32_t delta_call_22(int which, const int32_t *stack, int argn);
int32_t delta_call_23(int which, const int32_t *stack, int argn);
int32_t delta_call_24(int which, const int32_t *stack, int argn);
int32_t delta_call_25(int which, const int32_t *stack, int argn);

int32_t delta_direct_1(int which, int32_t a0);
int32_t delta_direct_2(int which, int32_t a0, int32_t a1);
int32_t delta_direct_3(int which, int32_t a0, int32_t a1, int32_t a2);
int32_t delta_direct_4(int which, int32_t a0, int32_t a1, int32_t a2,
                       int32_t a3);
int32_t delta_direct_5(int which, int32_t a0, int32_t a1, int32_t a2,
                       int32_t a3, int32_t a4);
int32_t delta_direct_6(int which, int32_t a0, int32_t a1, int32_t a2,
                       int32_t a3, int32_t a4, int32_t a5);
int32_t delta_direct_7(int which, int32_t a0, int32_t a1, int32_t a2,
                       int32_t a3, int32_t a4, int32_t a5, int32_t a6);
int32_t delta_direct_8(int which, int32_t a0, int32_t a1, int32_t a2,
                       int32_t a3, int32_t a4, int32_t a5, int32_t a6,
                       int32_t a7);

#endif
