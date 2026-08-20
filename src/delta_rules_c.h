/* Rules written as C rather than left as bytecode, and what they need of the
   machine they were written for.
 *
 * This is not generated. delta_rules.h is, and anything put there is lost the
 * next time the lifter runs.
 */

#ifndef DELTA_RULES_C_H
#define DELTA_RULES_C_H

#include <stdint.h>

/* The four flags the machine keeps. A rule written as C keeps them the same
   way, and works them with the same code, or a comparison after an operation
   would part company with the original over what it says. */
typedef struct {
    int zf, sf, cf, of;
} delta_flags;

int32_t delta_rule_alu(delta_flags *f, int kind, int32_t a, int32_t b);
void    delta_rule_cmp(delta_flags *f, int kind, int32_t a, int32_t b);
int     delta_condition(const delta_flags *f, int cond);

/* One rule written as C, and the table of them. The interpreter looks there
   once it has said what it is about to run, so a rule can be swapped between
   the two without anything that calls it knowing, and the two can be set
   against each other by speaking the same text twice. */
typedef int32_t (*delta_rule_cfn)(void *state, const int32_t *args, int nargs);

typedef struct {
    int            rule;
    delta_rule_cfn fn;
} delta_rule_c;

extern const delta_rule_c delta_rule_native[];

/* The call a rule makes, whichever way it is being run. Both go through here
   so that what a run says it did is the same either way, which is what a rule
   written as C is held against. */
int32_t delta_rule_direct(int which, const int32_t *a, int n);
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
   written out in full they were a fifth of the decompiled C. */
#define DELTA_RULE_ARGS 64

/* Taking one back off. The machine pops an argument into a register after a
   call, which is how it reads what the call left behind. */
#define POP(r)  do { if (argn > 0) { argn--; \
                     if (argn < DELTA_RULE_ARGS) (r) = arg[argn]; } } while (0)

/* An expression rather than a statement, so that the pushes a call needs can
   sit inside the call itself. They stay in the order the machine made them,
   which is the reverse of the order the entry takes them: the last thing
   pushed is the first argument. */
#define ARG(x)  (((argn < DELTA_RULE_ARGS) \
                  ? (void)(arg[argn] = (int32_t)(x)) : (void)0), \
                 (void)argn++)
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
           r0 = setjmp(*(jmp_buf *)(intptr_t)buf); \
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

/* How a decompiled rule writes a call. The arguments are already on that
   stack, which is why they are not named here: what a call says is which
   entry it is and how many of them it takes. */
/* An inlined wrapper: the primitive it stood for, with the numbers it had
   baked in and the caller's values in the places it read them from. The site's
   own pushes stay above it untouched, because a call does not pop them. */
#define CALLW(entry, ...) \
    delta_rule_direct(DELTA_ENTRY_##entry, \
                      (const int32_t[]){__VA_ARGS__}, \
                      (int)(sizeof (const int32_t[]){__VA_ARGS__} \
                            / sizeof(int32_t)))

#define CALL(entry, want) \
    delta_rule_called(DELTA_ENTRY_##entry, (int32_t *)arg, argn, (want))

#endif
