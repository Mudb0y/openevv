#include <stddef.h>
#include <string.h>

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

AT(fence_chars, 0x0084);
AT(fence_index, 0x008c);
AT(fence_fill, 0x0098);

/* A context record and a saved scan position together, which is what a rule
   pushes when it is about to try a match it may need to unwind. */
void bspush_ca_scan(delta_state *d, int16_t tag)
{
    delta_frame *ca = bs_push(d->stack, d->stack->ca_size);
    delta_frame *save;

    ca->kind = 0;
    ca->value = tag;

    save = bs_push(d->stack, d->stack->size_b0);
    save->kind = 1;
    memcpy(&save->value, d->vars->scan, 8);
}

/* Build the character fence: a set of characters the rules match against,
   held both ways round so either direction is a single lookup. */
void fence(delta_state *d, int8_t n, const uint8_t *chars)
{
    uint8_t i;

    d->vars->fence_count = n;
    memset(d->fence_index, d->fence_fill, d->fence_fill);

    for (i = 0; (int)i < (int)(uint8_t)n; i++) {
        d->fence_chars[i] = chars[i];
        d->fence_index[chars[i]] = i;
    }
}

/* The field block of a record sits eight bytes in. */
void *TFLDS(void *p)
{
    return (uint8_t *)p + 8;
}

void *getDeltaStackVBot(delta_state *d)
{
    return d->stack->vbot;
}

void setDeltaStackVBot(delta_state *d, void *v)
{
    d->stack->vbot = v;
}

/* Undo the topmost record. What it cost depends on what kind it was, and a
   kind outside the eight the original knows about leaves the size it moves by
   uninitialised, so callers never produce one. */
void *popDeltaStackTop(delta_state *d)
{
    delta_frame *slot = (delta_frame *)d->stack->top;
    int32_t kind = slot->kind;
    int32_t size = 0;

    switch (kind) {
    case 0: size = d->stack->ca_size;  break;
    case 1: size = d->stack->size_b0;  break;
    case 2: size = (((slot->length - 1) & ~1) | 1) + d->stack->size_ac + 1; break;
    case 3: size = d->stack->ca_size;  break;
    case 4: size = d->stack->boa_size; break;
    case 5: size = d->stack->size_b8;  break;
    case 6: size = d->stack->boa_size; break;
    case 7: size = d->stack->size_a8;  break;
    default: return slot;
    }

    d->stack->top += size;
    d->stack->limit += size;
    return slot;
}

/* Is the character at this offset from the fence base one of the fenced set. */
int FENCED(delta_state *d, const int32_t *table, int8_t idx)
{
    return (table[d->vars->fence_base + idx] & 2) != 0;
}

/* A sync number is kept in the low bits of a pointer, so reading the pointer
   back means masking them off. A null one is not a pointer at all. */
int32_t absoluteSyncNumPtr(int32_t p)
{
    if (p == 0)
        return -1;
    return p & ~3;
}

/* Drop everything the stack has above a mark. The limit is recomputed from
   the allocation rather than moved, so it stays right however far this goes. */
void freeDeltaStackTo(delta_state *d, uint8_t *to)
{
    delta_stack *s = d->stack;
    int32_t used;

    if (s->block == NULL)
        return;

    s->top = to;
    used = (int32_t)(*(uint8_t **)((char *)s->block + 0x10) - s->top);
    s->limit = s->base - used;
}

/* Unwind to whichever mark applies: a record of kind eight at the bottom
   means the rule wants the saved one instead. */
void clearDeltaStackBack(delta_state *d)
{
    if (*d->stack->vbot == 8)
        freeDeltaStackTo(d, d->vars->back);
    else
        freeDeltaStackTo(d, d->stack->vbot);
}

/* Open a test: remember what it is matching, clear anything a previous one
   left, and push the context record it will unwind to. */
void starttest(delta_state *d, int16_t tag)
{
    delta_frame *slot;

    d->vars->test_tag = tag;
    clearDeltaStackBack(d);

    slot = bs_push(d->stack, d->stack->ca_size);
    slot->kind = 0;
    slot->value = d->vars->test_tag;

    d->vars->testing = 1;
}
