#include <stddef.h>
#include <string.h>
#include <stdint.h>

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

static void set3(delta_state *d, int32_t x, int32_t y)
{
    d->vars->compared_equal = (int8_t)(x < y ? -1 : (x == y ? 0 : 1));
}

/* Three-way comparison, dispatched on the type of the left operand. Only the
   two integer widths look at the right operand's type as well; the rest
   assume it matches. */
void vcompare(delta_state *d, const delta_operand *a, const delta_operand *b)
{
    switch (a->kind) {
    case DK_UBYTE:
        set3(d, *(uint8_t *)a->ptr, *(uint8_t *)b->ptr);
        break;

    case DK_SHORT:
        set3(d, *(int16_t *)a->ptr, *(int16_t *)b->ptr);
        break;

    case DK_LONG:
        if (b->kind == DK_LONG)
            set3(d, *(int32_t *)a->ptr, *(int32_t *)b->ptr);
        else if (b->kind == DK_SHORT2)
            set3(d, *(int32_t *)a->ptr, *(int16_t *)b->ptr);
        break;

    case DK_SHORT2:
        if (b->kind == DK_LONG)
            set3(d, *(int16_t *)a->ptr, *(int32_t *)b->ptr);
        else if (b->kind == DK_SHORT2)
            set3(d, *(int16_t *)a->ptr, *(int16_t *)b->ptr);
        break;

    case DK_SYNC:
        /* A sync number has no ordering, only same or different. */
        d->vars->compared_equal =
            absoluteSyncNumPtr(*(int32_t *)a->ptr)
            == absoluteSyncNumPtr(*(int32_t *)b->ptr) ? 0 : 1;
        break;

    default:
        if (b->kind != a->kind) {
            d->vars->compared_equal = 1;
        } else {
            const int32_t *len = (const int32_t *)
                (vstmtbl + (int32_t)a->kind * VSTMTBL_ENTRY + VSTMTBL_LEN);

            /* The original keeps only the low byte of what memcmp returns,
               so the exact value matters and not just its sign. That was
               already true of IBM's builds; using memcmp keeps it so. */
            d->vars->compared_equal =
                (int8_t)memcmp(a->ptr, b->ptr, (size_t)*len);
        }
        break;
    }
}

/* Accessors the compiler emitted as calls rather than inlining. The spine's
   flags live in the spare bits of its own link words, so reading one is a
   mask and writing one is a read, modify and write back. */
int16_t STMTYP(int8_t kind)
{
    const uint8_t *desc = *(const uint8_t *const *)
        (vstmtbl + (int32_t)kind * VSTMTBL_ENTRY + VSTMTBL_DESC);

    return *(const int16_t *)(desc + 0x12);
}

int ONESTM(const delta_node *t)   { return (t->link & 1) != 0; }
int ALLNSQ(const delta_node *t)   { return (t->link & 2) != 0; }
int NONSEQ(const delta_node *t)   { return (t->flags8 & 2) != 0; }

void SETONESTM(delta_node *t)     { t->link |= 1; }
void SETALLNSQ(delta_node *t)     { t->link |= 2; }
void SETNONSEQ(delta_node *t)     { t->flags8 |= 2; }
void CLRONESTM(delta_node *t)     { t->link &= ~1; }
void CLRALLNSQ(delta_node *t)     { t->link &= ~2; }

void bsclear(delta_state *d)
{
    clearDeltaStackBack(d);
}

/* Take the alternative marker off the stack, handing back where it was. */
void *bspop_boa(delta_state *d)
{
    void *slot = d->stack->top;

    popDeltaStackTop(d);
    return slot;
}

/* The original carries three separate entry points for opening a test and
   compiles the same body into each. They are kept apart here because the
   generated rules call all three by name. */
void starttest_e(delta_state *d, int16_t tag) { starttest(d, tag); }
void starttest_l(delta_state *d, int16_t tag) { starttest(d, tag); }

/* Fencing is one bit in the same word FENCED reads. */
void SETFENCE(delta_state *d, int32_t *table, int8_t idx)
{
    table[d->vars->fence_base + idx] |= 2;
}

void UNSETFENCE(delta_state *d, int32_t *table, int8_t idx)
{
    table[d->vars->fence_base + idx] &= ~2;
}

/* And the rules fence through whatever the left pointer register holds. */
void addfence(delta_state *d, int8_t idx)
{
    SETFENCE(d, (int32_t *)d->lpta.value, idx);
}

void remfence(delta_state *d, int8_t idx)
{
    UNSETFENCE(d, (int32_t *)d->lpta.value, idx);
}

int32_t deltaErrorThrown(delta_state *d)
{
    return d->vars->error_thrown;
}

/* Nothing is left once the record at the unwind point is the bottom marker. */
int emptyDeltaStack(delta_state *d)
{
    return d->vars->back[d->stack->size_a8] == 8;
}

void *popDeltaStackFrame(delta_state *d, uint8_t *to)
{
    freeDeltaStackTo(d, to);
    return to;
}

/* Push a value onto the name stack, keeping its type alongside it so whatever
   pops it knows how wide it was. */
void vnspush(delta_state *d, const delta_operand *v)
{
    delta_stack *s = d->stack;
    uint8_t *slot;

    s->names_depth = (int8_t)(s->names_depth + 1);
    slot = s->names + (int32_t)s->names_depth * 8;

    *(int16_t *)(slot + 4) = v->kind;

    switch (v->kind) {
    case DK_UBYTE:
        *(int8_t *)slot = *(int8_t *)v->ptr;
        break;
    case DK_LONG:
        *(int32_t *)slot = *(int32_t *)v->ptr;
        break;
    case DK_SHORT:
    case DK_SHORT2:
        *(int16_t *)slot = *(int16_t *)v->ptr;
        break;
    default:
        break;
    }
}

/* Add the right operand into the left, in whichever width the left is. The
   state is passed but never touched. */
void vadd(delta_state *d, const delta_operand *a, const delta_operand *b)
{
    (void)d;

    if (a->kind == DK_LONG) {
        if (b->kind == DK_LONG)
            *(int32_t *)a->ptr = *(int32_t *)a->ptr + *(int32_t *)b->ptr;
        else if (b->kind == DK_SHORT2)
            *(int32_t *)a->ptr = *(int16_t *)b->ptr + *(int32_t *)a->ptr;
    } else if (a->kind == DK_SHORT2) {
        if (b->kind == DK_LONG)
            *(int16_t *)a->ptr =
                (int16_t)(*(int16_t *)a->ptr + *(int32_t *)b->ptr);
        else if (b->kind == DK_SHORT2)
            *(int16_t *)a->ptr =
                (int16_t)(*(int16_t *)a->ptr + *(int16_t *)b->ptr);
    }
}

/* Follow a field's left sync link. A link that is itself marked as a sync is
   the answer; otherwise the answer is one step further on. */
int32_t VLSYNC(const delta_node *t, int8_t i)
{
    int32_t p = t->syncs[i] & ~3;

    if (p == 0)
        return p;
    if ((*(int32_t *)p & 2) != 0)
        return p;
    return *(int32_t *)p & ~3;
}

/* The right sync link, reached through the fence base rather than a fixed
   offset, and one step further on if the link is not itself a sync. */
int32_t VRSYNC(delta_state *d, const int32_t *t, int8_t i)
{
    int32_t p = t[d->vars->fence_base + i] & ~3;

    if (p == 0)
        return p;
    if ((*(int32_t *)p & 2) != 0)
        return p;
    return *(int32_t *)(p + 4) & ~3;
}

void reset_field(delta_field *f)
{
    if (f->a >= 0)
        f->b = -1;
}

/* Remember an active record. The stack is fixed at 999 and a push past that
   fails rather than growing it. */
int push_ptr(delta_state *d, int32_t p)
{
    delta_vars *v = d->vars;

    if (v->ptr_count >= 999)
        return 0;

    v->ptr_stack[v->ptr_count] = p;
    v->ptr_count++;
    return 1;
}

/* And take one back. The count is reloaded from the saved slot before being
   stepped back, which is what the original does rather than simply popping. */
int ret_ptr_active_record(delta_state *d)
{
    delta_vars *v = d->vars;

    if (v->ptr_count <= 0)
        return 0;

    v->ptr_count = v->active_record;
    v->ptr_count--;
    v->active_record = v->ptr_stack[v->ptr_count];
    return 1;
}

void throwDeltaErrorNow(delta_state *d)
{
    d->vars->error_thrown = 1;
}

/* Take the top of the name stack, handing back where it sits rather than
   copying it out. Only the four sized types get a pointer. */
void vnspop(delta_state *d, delta_operand *out)
{
    delta_stack *s = d->stack;
    uint8_t *slot = s->names + (int32_t)s->names_depth * 8;

    out->kind = *(int16_t *)(slot + 4);
    *((int8_t *)out + 6) = 0;

    switch (out->kind) {
    case DK_UBYTE:
    case DK_SHORT:
    case DK_LONG:
    case DK_SHORT2:
        out->ptr = slot;
        break;
    default:
        break;
    }

    s->names_depth = (int8_t)(s->names_depth - 1);
}

/* Save a variable on the backtracking stack so an unwind can put it back.
   The record is variable length, which is what popDeltaStackTop's second kind
   is measuring when it reads the length back out of offset eight. */
void vpush_var(delta_state *d, const delta_operand *v)
{
    delta_stack *s = d->stack;
    int32_t size;
    int32_t pad;
    int32_t step;
    uint8_t *slot;

    if (v->kind == DK_SYNC)
        size = 4;
    else if (v->kind == DK_SHORT2)
        size = 2;
    else if (v->kind <= DK_SHORT2)
        size = *(const int32_t *)
            (vstmtbl + (int32_t)v->kind * VSTMTBL_ENTRY + VSTMTBL_LEN);
    else if (v->kind <= DK_SHORT)
        size = 4;
    else if (v->kind == DK_UBYTE)
        size = 1;
    else
        size = *(const int32_t *)
            (vstmtbl + (int32_t)v->kind * VSTMTBL_ENTRY + VSTMTBL_LEN);

    pad = ((size - 1) & ~1) | 1;
    step = s->size_ac + pad + 1;

    s->top -= step;
    slot = s->top;
    s->limit -= step;

    slot[0] = 2;
    *(int16_t *)(slot + 2) = v->kind;
    *(int32_t *)(slot + 8) = size;
    *(int32_t *)(slot + 4) = (int32_t)(intptr_t)v->ptr;

    memcpy(slot + s->size_ac, v->ptr, (size_t)(pad + 1));
}

/* Bumped whenever the spine is relinked. */
int32_t spine_changed;

/* Unlink a node from the spine, keeping the tag bits that ride in the low two
   bits of each link. */
void DELSPINE(delta_state *d, delta_node *t)
{
    int32_t base = d->vars->fence_base;
    int32_t next = t->link & ~3;
    int32_t prev = *(int32_t *)((char *)t + base * 4 - 8) & ~3;
    int32_t *back = (int32_t *)((char *)(intptr_t)next + base * 4 - 8);
    int32_t *fwd = (int32_t *)((char *)(intptr_t)prev + 4);

    *back = (*back & 3) | prev;
    *fwd = (*fwd & 3) | next;
    spine_changed++;
}
