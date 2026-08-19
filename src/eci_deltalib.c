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
#define OWNER_MOVED(d) (*(int32_t *)(EVV_AT(uint8_t *, (d)->owner) + 0x1b8))

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
    d->stack = EVV_REF((delta_stack *)malloc(STACK_BYTES));
    if (!EVV_AT(delta_stack *, d->stack))
        return -2;

    memset(EVV_AT(delta_stack *, d->stack), 0, STACK_BYTES);

    STACK_DASHES(EVV_AT(delta_stack *, d->stack)) = "---";
    STACK_E0(EVV_AT(delta_stack *, d->stack)) = 1;
    STACK_1D0(EVV_AT(delta_stack *, d->stack)) = -1;
    STACK_1D4(EVV_AT(delta_stack *, d->stack)) = -1;
    STACK_604(EVV_AT(delta_stack *, d->stack)) = 0;

    return 0;
}

void delta_lib_delete(delta_state *d)
{
    if (!d || !EVV_AT(delta_stack *, d->stack))
        return;

    memset(EVV_AT(delta_stack *, d->stack), 0, STACK_BYTES);
    free(EVV_AT(delta_stack *, d->stack));
    d->stack = EVV_REF(0);
}

/* One byte per statement type in each of two small tables: which fields are
   marked non-sequential, and which of them decide the flags. The second
   starts with all its bits set rather than clear. */
int32_t vdelinit(delta_state *d)
{
    int32_t i;

    EVV_AT(delta_vars *, d->vars)->nsq_marks = EVV_REF((int8_t *)malloc((size_t)d->nstmts));
    EVV_AT(delta_stack *, d->stack)->nsq_fields = EVV_REF((int8_t *)malloc((size_t)d->nstmts));

    if (!EVV_AT(int8_t *, EVV_AT(delta_vars *, d->vars)->nsq_marks) || !EVV_AT(int8_t *, EVV_AT(delta_stack *, d->stack)->nsq_fields))
        return 0;

    for (i = 0; i < d->nstmts; i++)
        EVV_AT(int8_t *, EVV_AT(delta_vars *, d->vars)->nsq_marks)[i] = 0;

    EVV_AT(int8_t *, EVV_AT(delta_stack *, d->stack)->nsq_fields)[0] = -1;
    return 1;
}

void vdelCleanup(delta_state *d)
{
    if (EVV_AT(int8_t *, EVV_AT(delta_stack *, d->stack)->nsq_fields)) {
        free((void *)EVV_AT(int8_t *, EVV_AT(delta_stack *, d->stack)->nsq_fields));
        EVV_AT(delta_stack *, d->stack)->nsq_fields = EVV_REF(0);
    }

    if (EVV_AT(int8_t *, EVV_AT(delta_vars *, d->vars)->nsq_marks)) {
        free((void *)EVV_AT(int8_t *, EVV_AT(delta_vars *, d->vars)->nsq_marks));
        EVV_AT(delta_vars *, d->vars)->nsq_marks = EVV_REF(0);
    }
}

/* Make two nodes into one. Whichever is kept, every field the other carries
   is projected onto it and then deleted; a field the kept node already has
   is only deleted. Answers false if any of that failed. */
int32_t vmerge(delta_state *d, int32_t left, int32_t right)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);
    int32_t     keep;
    int32_t     drop;
    int32_t     joined = 0;
    int8_t      f;

    if (left == right)
        return 1;

    OWNER_MOVED(d) = 1;

    /* The spine's own ends are never the one dropped, and neither is the
       left one while the machine is relinking a non-sequential node. */
    if (right == EVV_AT(delta_stack *, d->stack)->spine_l
     || right == EVV_AT(delta_stack *, d->stack)->spine_r
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
