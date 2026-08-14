#include <stddef.h>

#include "delta.h"

#define AT(field, offset) \
    typedef char field##_at_##offset[offsetof(delta_state, field) == offset ? 1 : -1]

AT(lpta, 0x0040);
AT(rpta, 0x0050);
AT(vars, 0x0068);
AT(stack, 0x006c);

typedef char delta_state_is_0x1088[sizeof(delta_state) == DELTA_STATE_BYTES ? 1 : -1];
typedef char delta_pta_is_16[sizeof(delta_pta) == 16 ? 1 : -1];

/* Point the left register at a token. The flag says a load happened and the
   cleared word is whatever the previous load left behind. */
void lpta_loadp(delta_state *d, const delta_token *p)
{
    d->lpta.loaded = 1;
    d->lpta.value = p->value;
    d->lpta.unknown_08 = 0;
}

/* Byte for byte the same as lpta_loadp in the original. */
void lpta_loadpn(delta_state *d, const delta_token *p)
{
    d->lpta.loaded = 1;
    d->lpta.value = p->value;
    d->lpta.unknown_08 = 0;
}

/* Loading the right register clears the left register's word rather than its
   own. Both spellings of it in the original do this, so it is reproduced
   rather than corrected; lpta_rpta_loadp clears both. */
void rpta_loadp(delta_state *d, const delta_token *p)
{
    d->rpta.loaded = 1;
    d->rpta.value = p->value;
    d->lpta.unknown_08 = 0;
}

void rpta_loadpn(delta_state *d, const delta_token *p)
{
    d->rpta.loaded = 1;
    d->rpta.value = p->value;
    d->lpta.unknown_08 = 0;
}

/* Both registers at once, which is what a rule matching across a span wants
   and why it is the second most common operation in the whole language. */
void lpta_rpta_loadp(delta_state *d, const delta_token *lp,
                     const delta_token *rp)
{
    d->rpta.loaded = 1;
    d->lpta.loaded = 1;
    d->lpta.value = lp->value;
    d->rpta.value = rp->value;
    d->rpta.unknown_08 = 0;
    d->lpta.unknown_08 = 0;
}

/* The stack grows downward, and both pointers move together by whatever the
   record kind costs. */
static delta_frame *bs_push(delta_stack *s, int32_t size)
{
    delta_frame *slot;

    s->top -= size;
    slot = (delta_frame *)s->top;
    s->limit -= size;
    return slot;
}

/* A context record, carrying the tag the rule is testing against. */
void bspush_ca(delta_state *d, int16_t tag)
{
    delta_frame *slot = bs_push(d->stack, d->stack->ca_size);

    slot->kind = 0;
    slot->value = tag;
}

/* The two markers a rule leaves where an alternative begins. */
void bspush_boa(delta_state *d)
{
    bs_push(d->stack, d->stack->boa_size)->kind = 4;
}

void bspush_nboa(delta_state *d)
{
    bs_push(d->stack, d->stack->boa_size)->kind = 6;
}

int testeq(delta_state *d)
{
    return d->vars->compared_equal != 0;
}

int testneq(delta_state *d)
{
    return d->vars->compared_equal == 0;
}
