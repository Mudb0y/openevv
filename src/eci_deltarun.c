/* Making the machine's variable block, and putting it back to the start.
 *
 * The block is one allocation of 0x11f0 bytes, wiped clean, and everything
 * in it starts at nought. Putting it back to the start again between runs is
 * the same idea with three extras: the fenced-character tables are cleared,
 * the spine's two ends are pointed at from the two places that name them,
 * and every compound global gets its opening value back.
 *
 * The machine is rebuilt from scratch rather than merely reset if the last
 * C helper answered with either of two particular values -- one meaning it
 * never ran, the other meaning it failed -- because in that case whatever
 * the spine held cannot be trusted.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "delta.h"

/* How big the variable block is. */
#define VARS_BYTES 0x11f0

/* The two places that hold a copy of where the spine starts and ends.
   Neither has ever needed naming anywhere else. */
/* The two word variables something wants a direct handle on. The handle is
   on the cell, tag and all, so the value is four bytes further in. */
#define SPINE_L_HOLDER(d) EVV_AT(char *, (d)->direct_a)
#define SPINE_R_HOLDER(d) EVV_AT(char *, (d)->direct_b)

/* Three words of the variable block cleared before a run. */
#define VARS_FF0(v)   (*(uint8_t *)((char *)(v) + 0xff0))
#define VARS_1000(v)  (*(uint8_t *)((char *)(v) + 0x1000))

/* What the owner keeps that says the spine moved. */
#define OWNER_MOVED(d) (EVV_AT(delta_owner *, (d)->owner)->changed)

/* What the last C helper answered with, when it means the machine cannot be
   trusted to still hold a spine: never ran, or ran and failed. */
#define NEVER_RAN 0xf9
#define FAILED    0xff

extern void    ccode_misc_new(delta_state *d);
extern int32_t vdltinit(delta_state *d, int32_t initStatements);

/* One block, wiped, and the one field that does not start at nought. */
void ccode_new(delta_state *d)
{
    d->vars = EVV_REF((delta_vars *)malloc(VARS_BYTES));
    memset(EVV_AT(delta_vars *, d->vars), 0, VARS_BYTES);
    ccode_misc_new(d);
}

/* Wiped again before it goes back, so nothing of the run is left in it. */
void ccode_delete(delta_state *d)
{
    if (!d || !EVV_AT(delta_vars *, d->vars))
        return;

    memset(EVV_AT(delta_vars *, d->vars), 0, VARS_BYTES);
    free(EVV_AT(delta_vars *, d->vars));
    d->vars = EVV_REF(0);
}

/* Back to the start of a run. */
int32_t vinitrun(delta_state *d)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);
    int32_t     i;

    v->fence_count = 0;

    /* No character is fenced, every field's index says "not fenced", and no
       field carries a mark. The index uses the statement count itself as
       the value meaning nothing, which is why it goes one past the end. */
    for (i = 0; i < d->nstmts; i++) {
        EVV_AT(uint8_t *, d->fence_chars)[i] = 0;
        EVV_AT(uint8_t *, d->fence_index)[i] = d->nstmts;
        EVV_AT(uint8_t *, d->fence_marks)[i] = 0;
    }
    EVV_AT(uint8_t *, d->fence_marks)[d->nstmts] = 0;

    *(int32_t *)(SPINE_L_HOLDER(d) + 4) = EVV_AT(delta_stack *, d->stack)->spine_l;
    *(int32_t *)(SPINE_R_HOLDER(d) + 4) = EVV_AT(delta_stack *, d->stack)->spine_r;

    VARS_FF0(v) = 0;
    VARS_1000(v) = 0;
    v->unknown_11e8 = 0;

    /* A machine whose last helper never ran, or failed, is built again
       rather than merely reset -- and the spine's ends move, so the two
       places that name them are pointed at the new ones. */
    if (v->return_code == NEVER_RAN || v->return_code == FAILED) {
        if (!vdltinit(d, 1))
            return 0;
        *(int32_t *)(SPINE_L_HOLDER(d) + 4) = EVV_AT(delta_stack *, d->stack)->spine_l;
        *(int32_t *)(SPINE_R_HOLDER(d) + 4) = EVV_AT(delta_stack *, d->stack)->spine_r;
    }

    OWNER_MOVED(d) = 0;

    for (i = 0; i < d->ncompound; i++) {
        unsigned char *at = EVV_AT(delta_compound *, d->compound)[i].at;

        *(int16_t *)at = (int16_t)EVV_AT(delta_compound *, d->compound)[i].init;
        *(int16_t *)(at + 2) |= (int16_t)-1;
        memset(at + 4, 0, (size_t)EVV_AT(delta_compound *, d->compound)[i].bytes);
    }

    return 1;
}
