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
int32_t delta_rule_called(int which, const int32_t *stack, int argn,
                          int want);

#endif
