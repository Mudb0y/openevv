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
#include "delta_rules_c.h"

/* How big the stack block is. */
#define STACK_BYTES 0x664

/* Two of the fields that do not start at nought have no name in the struct
   and are still reached by the byte they sit at. That is safe in this block,
   and safe because of how it is built: everything in it that points is a
   four-byte reference rather than a pointer, so the block has the same layout
   whatever a pointer is worth. */
#define STACK_E0(s)     (*(int32_t *)((char *)(s) + 0x0e0))
#define STACK_604(s)    (*(int32_t *)((char *)(s) + 0x604))

/* What a value named "undefined" reads back as. The machine is handed this as
   a value, so it has to name somewhere in the arena; the string itself is in
   the program, which no value can reach, so what is kept is a copy made low.
   Writing the program's own address here is what this did before, and going
   through a byte offset is what let it past evv_ref_checked. */
static const char UNDEFINED_TEXT[] = "---";

/* What the owner keeps that says the spine moved. */
#define OWNER_MOVED(d) (EVV_AT(delta_owner *, (d)->owner)->changed)

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
    delta_stack *s;

    d->stack = EVV_REF((delta_stack *)malloc(STACK_BYTES));
    if (!EVV_AT(delta_stack *, d->stack))
        return -2;

    s = EVV_AT(delta_stack *, d->stack);
    memset(s, 0, STACK_BYTES);

    s->undefined_text = EVV_REF(delta_low_copy(UNDEFINED_TEXT,
                                               sizeof UNDEFINED_TEXT));
    STACK_E0(s) = 1;
    s->left_stamp = -1;
    s->left_next = -1;
    STACK_604(s) = 0;

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
