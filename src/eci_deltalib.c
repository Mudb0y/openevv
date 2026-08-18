/* The machine's stack block, and merging two nodes into one.
 *
 * Making the block is one allocation wiped clean, with five fields that do
 * not start at nought. Alongside it is the pair of small tables the
 * non-sequential check works through, one byte per statement type.
 *
 * Merging is the interesting one. Two nodes that name the same place have to
 * become one: whichever is kept, every field the other carries has to be
 * projected onto it and then dropped. Which of the two is kept is not
 * arbitrary -- the spine's own ends are always kept, and so is the left one
 * when the machine is relinking and the left is non-sequential.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "delta.h"

/* How big the stack block is. */
#define STACK_BYTES 0x664

/* The five fields in it that do not start at nought. */
#define STACK_DASHES(s) (*(const char **)((char *)(s) + 0x0dc))
#define STACK_E0(s)     (*(int32_t *)((char *)(s) + 0x0e0))
#define STACK_1D0(s)    (*(int32_t *)((char *)(s) + 0x1d0))
#define STACK_1D4(s)    (*(int32_t *)((char *)(s) + 0x1d4))
#define STACK_604(s)    (*(int32_t *)((char *)(s) + 0x604))

/* What the owner keeps that says the spine moved. */
#define OWNER_MOVED(d) (*(int32_t *)((d)->owner + 0x1b8))

/* One node's field, by statement type. */
#define FIELD(n, f)  (((int32_t *)(intptr_t)(n))[(f)])
#define FENCED       1
#define LINK_MASK    (~3)

/* Where a node keeps the six words of its own before the fields start. */
#define OWN_WORDS 3

void delta_lib_delete(delta_state *d);

/* One block, wiped, and the handful that start at something else. */
int32_t delta_lib_new(delta_state *d)
{
    d->stack = (delta_stack *)malloc(STACK_BYTES);
    if (!d->stack)
        return -2;

    memset(d->stack, 0, STACK_BYTES);

    STACK_DASHES(d->stack) = "---";
    STACK_E0(d->stack) = 1;
    STACK_1D0(d->stack) = -1;
    STACK_1D4(d->stack) = -1;
    STACK_604(d->stack) = 0;

    return 0;
}

void delta_lib_delete(delta_state *d)
{
    if (!d || !d->stack)
        return;

    memset(d->stack, 0, STACK_BYTES);
    free(d->stack);
    d->stack = 0;
}

/* One byte per statement type in each of two small tables: which fields are
   marked non-sequential, and which of them decide the flags. The second
   starts with all its bits set rather than clear. */
int32_t vdelinit(delta_state *d)
{
    int32_t i;

    d->vars->nsq_marks = (const int8_t *)malloc((size_t)d->nstmts);
    d->stack->nsq_fields = (const int8_t *)malloc((size_t)d->nstmts);

    if (!d->vars->nsq_marks || !d->stack->nsq_fields)
        return 0;

    for (i = 0; i < d->nstmts; i++)
        ((int8_t *)d->vars->nsq_marks)[i] = 0;

    *(int8_t *)d->stack->nsq_fields = -1;
    return 1;
}

void vdelCleanup(delta_state *d)
{
    if (d->stack->nsq_fields) {
        free((void *)d->stack->nsq_fields);
        d->stack->nsq_fields = 0;
    }

    if (d->vars->nsq_marks) {
        free((void *)d->vars->nsq_marks);
        d->vars->nsq_marks = 0;
    }
}

/* Make two nodes into one. Whichever is kept, every field the other carries
   is projected onto it and then deleted; a field the kept node already has
   is only deleted. Answers false if any of that failed. */
int32_t vmerge(delta_state *d, int32_t left, int32_t right)
{
    delta_vars *v = d->vars;
    int32_t     keep;
    int32_t     drop;
    int32_t     joined = 0;
    int8_t      f;

    if (left == right)
        return 1;

    OWNER_MOVED(d) = 1;

    /* The spine's own ends are never the one dropped, and neither is the
       left one while the machine is relinking a non-sequential node. */
    if (right == d->stack->spine_l
     || right == d->stack->spine_r
     || (v->relink != 0 && NONSEQ((const delta_node *)(intptr_t)left))) {
        keep = left;
        drop = right;
    } else {
        keep = right;
        drop = left;
    }

    /* Are they already joined? The first field both of them carry answers
       it: if the kept one's link there is the one being dropped, the two
       are already next to each other. */
    for (f = 0; f < (int8_t)d->nstmts; f++) {
        if (!(FIELD(drop, v->fence_base + f) & FENCED))
            continue;
        if (!(FIELD(keep, v->fence_base + f) & FENCED))
            continue;
        joined = (FIELD(keep, OWN_WORDS + f) & LINK_MASK) == drop;
        break;
    }

    for (f = 0; f < (int8_t)d->nstmts; f++) {
        if (!(FIELD(keep, v->fence_base + f) & FENCED))
            continue;

        /* A field the kept node has and the dropped one does not has to be
           carried across first, both ways, before it can go. */
        if (!(FIELD(drop, v->fence_base + f) & FENCED) && joined) {
            if (!vproj_l(d, (delta_node *)(intptr_t)drop,
                         (delta_node *)(intptr_t)keep, (uint8_t)f))
                return 0;
            if (!vproj_r(d, (delta_node *)(intptr_t)drop,
                         (delta_node *)(intptr_t)keep, (uint8_t)f))
                return 0;
        }

        if (!vdel_1pt(d, (uint8_t)f, keep, drop))
            return 0;
    }

    return 1;
}
