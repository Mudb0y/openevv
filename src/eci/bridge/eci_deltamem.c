/* Giving the Delta machine its memory, and giving it back.
 *
 * Three things happen here. The sizes of the records the machine pushes on
 * its stack are settled -- they depend on how many statement types the
 * language declares, so they cannot be constants. The stack and the heap are
 * built. And the spine, which is the pair of nodes every rule works between,
 * is created and pointed at itself.
 *
 * The middle one is also what a restart calls, which is why it is separate:
 * everything it does can be done again over a machine that has already run.
 */

#include <stdint.h>
#include <stdlib.h>
#include "delta.h"
#include "evv_arena.h"

/* The name stack: eight bytes an entry, and room for this many. */
#define NAME_STACK_BYTES 0x28

/* How big the stack and the heap start out. */
#define STACK_BYTES 0x2000
#define HEAP_BYTES  0x1000


/* Set up the machine's memory once. The first record size is rounded up to
   an even number because the machine steps through that stack two words at a
   time; the rest are fixed. */
int32_t vmeminit(delta_state *d)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);
    int32_t      fenced = d->fence_room;

    s->size_a8 = (((fenced - 1) & ~1) | 1) + 1;
    s->size_ac = 0xc;
    s->size_b0 = 0xc;
    s->ca_size = 8;
    s->size_b8 = 8;
    s->boa_size = 2;

    if (!initializeDeltaStack(d, STACK_BYTES))
        return 0;
    if (!initializeDeltaHeap(d, HEAP_BYTES))
        return 0;

    /* An unwind with nothing to unwind to lands at the top of the stack. */
    EVV_AT(delta_vars *, d->vars)->back = EVV_REF(EVV_AT(uint8_t *, s->top));

    s->unknown_9c = 0;
    s->unknown_98 = 0;

    s->names = EVV_REF((uint8_t *)malloc(NAME_STACK_BYTES));
    if (!EVV_AT(uint8_t *, s->names))
        return 0;
    s->names_depth = -1;

    delta_lang_of(d)->sizes();
    return 1;
}

/* The name stack is the only thing here that was taken from the C heap. */
void vnstackCleanup(delta_state *d)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);

    if (EVV_AT(uint8_t *, s->names)) {
        free(EVV_AT(uint8_t *, s->names));
        s->names = EVV_REF(0);
    }
}

/* Empty the machine and build a fresh spine: two sync nodes, each field of
   each pointing at the other one, so a walk from either end terminates.
   Called again on every restart, which is why the heap is reset first.

   The second argument asks for each statement type to be initialised as
   well; a restart that only wants the spine back leaves it out. */
int32_t vdltinit(delta_state *d, int32_t initStatements)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);
    delta_vars  *v = EVV_AT(delta_vars *, d->vars);
    int8_t       i;

    /* One sync node holds two words a statement type, plus a fixed six. */
    s->sync_size = (d->nstmts * 2 + 6) * 4;

    resetDeltaHeap(d);
    s->spine_l = EVV_REF(alloc_sync(d));
    s->spine_r = EVV_REF(alloc_sync(d));
    if (!s->spine_l || !s->spine_r)
        return 0;

    if (!recordDeltaHeapPos(d))
        return 0;

    CLRONESTM((delta_node *)(intptr_t)s->spine_l);
    CLRONESTM((delta_node *)(intptr_t)s->spine_r);

    for (i = 0; i < (int8_t)d->nstmts; i++) {
        int32_t *left  = (int32_t *)(intptr_t)s->spine_l;
        int32_t *right = (int32_t *)(intptr_t)s->spine_r;
        int32_t  f     = v->fence_base + i;

        /* Mark both ends of the field, then join them: the left end points
           at the right one and the right end back at the left, keeping the
           two flag bits each already carries. */
        left[f]  |= 1;
        right[f] |= 1;
        left[f]  = (left[f] & 3) | s->spine_r;
        right[3 + i] = (right[3 + i] & 3) | s->spine_l;

        if (initStatements && !vinit_stm(d, i))
            return 0;
    }

    SETSPINER(d, (int32_t *)(intptr_t)s->spine_l, s->spine_r);
    SETSPINEL((delta_node *)(intptr_t)s->spine_r, s->spine_l);
    vscaninit(d);
    v->unknown_1170 = 1;
    return 1;
}
