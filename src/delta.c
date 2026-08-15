#include <stddef.h>
#include <string.h>
#include <stdint.h>

#include <setjmp.h>

#include "delta.h"

#define AT(field, offset) \
    typedef char field##_at_##offset[offsetof(delta_state, field) == offset ? 1 : -1]
#define AT_VARS(field, offset) \
    typedef char field##_at_##offset[offsetof(delta_vars, field) == offset ? 1 : -1]

AT(lpta, 0x0040);
AT(rpta, 0x0050);
AT(vars, 0x0068);
AT(stack, 0x006c);

typedef char delta_state_is_0x1088[sizeof(delta_state) == DELTA_STATE_BYTES ? 1 : -1];
typedef char delta_pta_is_16[sizeof(delta_pta) == 16 ? 1 : -1];
typedef char delta_stmt_is_0x40[sizeof(delta_stmt) == 0x40 ? 1 : -1];
typedef char delta_fielddesc_is_0x18[sizeof(delta_fielddesc) == 0x18 ? 1 : -1];

/* How long a language-declared record is. Two of the callers can arrive with
   a negative kind, which the original indexes the table with regardless, so
   this goes through a byte offset rather than letting the compiler decide it
   knows the subscript is out of range. */
static int32_t stmt_length(int32_t kind)
{
    uintptr_t p = (uintptr_t)vstmtbl
                  + (uintptr_t)(intptr_t)(kind * (int32_t)sizeof(delta_stmt));

    return *(const int32_t *)(p + offsetof(delta_stmt, length));
}

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
AT(fence_marks, 0x0094);
AT_VARS(err_jmp, 0x0fac);

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
    memcpy(&save->value, &d->vars->scan_ptr, 8);
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
            int32_t len = stmt_length(a->kind);

            /* The original keeps only the low byte of what memcmp returns,
               so the exact value matters and not just its sign. That was
               already true of IBM's builds; using memcmp keeps it so. */
            d->vars->compared_equal =
                (int8_t)memcmp(a->ptr, b->ptr, (size_t)len);
        }
        break;
    }
}

/* Accessors the compiler emitted as calls rather than inlining. The spine's
   flags live in the spare bits of its own link words, so reading one is a
   mask and writing one is a read, modify and write back. */
int16_t STMTYP(int8_t kind)
{
    return vstmtbl[kind].fields[0].kind;
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
        size = stmt_length(v->kind);
    else if (v->kind <= DK_SHORT)
        size = 4;
    else if (v->kind == DK_UBYTE)
        size = 1;
    else
        size = stmt_length(v->kind);

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

/* Move the scan on by one node in whichever direction is set, refusing to
   cross a fenced character it has not already been let past. Each fenced
   character is marked once so a second attempt at the same one succeeds. */
int vscanadv(delta_state *d, int32_t step, int32_t usefence)
{
    delta_vars *v = d->vars;
    int32_t cur = v->scan_ptr;
    int32_t field = v->scan_field;
    int32_t next;
    int32_t i = 0;

    if (v->fence_count != 0 && usefence != 0 && v->scan_held == 0) {
        for (; i < v->fence_count; i++) {
            uint8_t c = d->fence_chars[i];

            if ((*(int32_t *)(cur + (v->fence_base + c) * 4) & 1) != 0)
                return 0;

            if (FENCED(d, (const int32_t *)cur, (int8_t)d->fence_chars[i])
                && field != d->fence_chars[i]
                && d->fence_marks[i] == 0)
                return 0;

            d->fence_marks[i] = 0;
        }
    }

    if (cur == 0)
        return 0;

    if (v->scan_rev != 0)
        next = *(int32_t *)(cur + (v->fence_base + field) * 4) & ~3;
    else
        next = *(int32_t *)(cur + 0xc + field * 4) & ~3;

    if (next == 0)
        return 0;

    /* A node that is not itself a sync needs one more step, and only if the
       caller asked to keep going. */
    if ((*(int32_t *)next & 2) == 0) {
        if (step == 0)
            return 0;
        if (v->scan_rev != 0)
            next = *(int32_t *)(next + 4) & ~3;
        else
            next = *(int32_t *)next & ~3;
    }

    v->scan_ptr = next;
    v->scan_held = 0;

    /* Carrying on from where the fence loop stopped, not from zero: a full
       pass has already cleared every mark, and a skipped one leaves i at
       zero so the whole array still gets cleared. */
    for (; i < v->fence_count; i++)
        d->fence_marks[i] = 0;

    return 1;
}

/* The right-hand spine link has no fixed offset: it sits one word before the
   sync array's end, so how many fields the language declares decides where. */
static int32_t *rlink(delta_state *d, int32_t p)
{
    return (int32_t *)((char *)(intptr_t)p + d->vars->fence_base * 4 - 8);
}

/* Present so the caller need not know whether deletion is deferred; on this
   build nothing is. */
void flushDeletedDeltaObjects(delta_state *d)
{
    (void)d;
}

void SETSPINEL(delta_node *t, int32_t v)
{
    t->link = (t->link & 3) | v;
}

void SETSPINER(delta_state *d, int32_t *t, int32_t v)
{
    int32_t *r = rlink(d, (int32_t)(intptr_t)t);

    *r = (*r & 3) | v;
}

void bspush_ca_boa(delta_state *d, int16_t tag)
{
    bspush_boa(d);
    bspush_ca(d, tag);
}

void bspush_ca_scan_boa(delta_state *d, int16_t tag)
{
    bspush_boa(d);
    bspush_ca_scan(d, tag);
}

void forceErrorBacktrack(delta_state *d)
{
    throwDeltaErrorNow(d);
    longjmp(*(jmp_buf *)d->vars->err_jmp, 1);
}

void push_ptr_init(delta_state *d, delta_ptrvar *p)
{
    p->value = 0;
    p->kind = DK_SYNC;
    push_ptr(d, (int32_t)(intptr_t)p);
}

/* The two-byte and one-byte name pushes. Each builds an operand pointing at
   its own argument slot, which is why the value is taken by copy. */
void npush_i(delta_state *d, int32_t x)
{
    delta_operand v;

    v.ptr = &x;
    v.kind = DK_SHORT2;
    v.pad_06 = 0;
    vnspush(d, &v);
}

void npush_s(delta_state *d, int32_t x)
{
    delta_operand v;

    v.ptr = &x;
    v.kind = DK_UBYTE;
    v.pad_06 = 0;
    vnspush(d, &v);
}

void vscaninit(delta_state *d)
{
    delta_vars *v = d->vars;

    v->scan_ptr = 0;
    v->scan_field = 0;
    v->scan_rev = 1;
    v->scan_held = 1;
}

/* Follow a field's sync chain leftward as far as it keeps landing on syncs. */
delta_node *vmovel(delta_node *t, uint8_t f)
{
    for (;;) {
        int32_t next = t->syncs[f] & ~3;

        if (next == 0)
            return t;
        if ((*(int32_t *)(intptr_t)next & 2) == 0)
            return t;
        t = (delta_node *)(intptr_t)next;
    }
}

/* The same walk rightward, where the field's link is past the sync array. */
int32_t *vmover(delta_state *d, int32_t *t, uint8_t f)
{
    for (;;) {
        int32_t next = t[d->vars->fence_base + f] & ~3;

        if (next == 0)
            return t;
        if ((*(int32_t *)(intptr_t)next & 2) == 0)
            return t;
        t = (int32_t *)(intptr_t)next;
    }
}

/* Splice n into the spine on t's left, then on t's right. Both keep the tag
   bits of whichever link they overwrite. */
void INSSPINEL(delta_state *d, delta_node *n, delta_node *t)
{
    int32_t old = t->link & ~3;
    int32_t *r;

    n->link = (n->link & 3) | old;

    r = rlink(d, old);
    *r = (*r & 3) | (int32_t)(intptr_t)n;

    t->link = (t->link & 3) | (int32_t)(intptr_t)n;

    r = rlink(d, (int32_t)(intptr_t)n);
    *r = (*r & 3) | (int32_t)(intptr_t)t;

    spine_changed++;
}

void INSSPINER(delta_state *d, delta_node *n, delta_node *t)
{
    int32_t old = *rlink(d, (int32_t)(intptr_t)t) & ~3;
    int32_t *r;

    r = rlink(d, (int32_t)(intptr_t)n);
    *r = (*r & 3) | old;

    ((delta_node *)(intptr_t)old)->link =
        (((delta_node *)(intptr_t)old)->link & 3) | (int32_t)(intptr_t)n;

    r = rlink(d, (int32_t)(intptr_t)t);
    *r = (*r & 3) | (int32_t)(intptr_t)n;

    n->link = (n->link & 3) | (int32_t)(intptr_t)t;

    spine_changed++;
}

/* The leftmost node of a field's run. Walk the field's sync chain while it
   keeps landing on syncs, then keep going through nodes whose first field
   reads as zero, which is how the language marks a continuation. */
delta_node *lmost(delta_state *d, int8_t f, delta_node *t)
{
    const delta_stmt *e = &vstmtbl[f];
    void *(*get)(void *) = e->get[0];
    uint8_t walkable = e->walkable;
    int16_t kind = e->fields[0].kind;
    int32_t next = *(int32_t *)((char *)t + 0xc + f * 4) & ~3;
    /* The original never assigns this in the default case, so a statement
       type of any other kind reads whatever the frame held. None of the ten
       English types does. */
    int32_t keep = 0;

    (void)d;

    for (;;) {
        if (next != 0 && (*(int32_t *)(intptr_t)next & 2) != 0) {
            t = (delta_node *)(intptr_t)next;
            next = *(int32_t *)((char *)t + 0xc + f * 4) & ~3;
            continue;
        }

        if (kind == DK_SHORT2)
            keep = next != 0 && walkable != 0
                && *(int16_t *)get(TFLDS((void *)(intptr_t)next)) == 0;
        else if (kind == DK_LONG)
            keep = next != 0 && walkable != 0
                && *(int32_t *)get(TFLDS((void *)(intptr_t)next)) == 0;

        if (!keep)
            return t;

        next = *(int32_t *)(intptr_t)next & ~3;
    }
}

/* The same walk the other way, over the links past the sync array. */
int32_t *rmost(delta_state *d, int8_t f, int32_t *t)
{
    const delta_stmt *e = &vstmtbl[f];
    void *(*get)(void *) = e->get[0];
    uint8_t walkable = e->walkable;
    int16_t kind = e->fields[0].kind;
    int32_t next = t[d->vars->fence_base + f] & ~3;
    int32_t keep = 0;

    for (;;) {
        if (next != 0 && (*(int32_t *)(intptr_t)next & 2) != 0) {
            t = (int32_t *)(intptr_t)next;
            next = t[d->vars->fence_base + f] & ~3;
            continue;
        }

        if (kind == DK_SHORT2)
            keep = next != 0 && walkable != 0
                && *(int16_t *)get(TFLDS((void *)(intptr_t)next)) == 0;
        else if (kind == DK_LONG)
            keep = next != 0 && walkable != 0
                && *(int32_t *)get(TFLDS((void *)(intptr_t)next)) == 0;

        if (!keep)
            return t;

        next = *(int32_t *)((char *)(intptr_t)next + 4) & ~3;
    }
}

/* Copy one value onto another. The narrowing and widening cases are spelled
   out; anything the language declares is copied whole by length. */
void vassign(delta_state *d, const delta_operand *dst, const delta_operand *src)
{
    (void)d;

    switch (dst->kind) {
    case DK_UBYTE:
        *(int8_t *)dst->ptr = *(const int8_t *)src->ptr;
        break;
    case DK_SHORT:
        *(int16_t *)dst->ptr = *(const int16_t *)src->ptr;
        break;
    case DK_LONG:
        if (src->kind == DK_LONG)
            *(int32_t *)dst->ptr = *(const int32_t *)src->ptr;
        else if (src->kind == DK_SHORT2)
            *(int32_t *)dst->ptr = *(const int16_t *)src->ptr;
        break;
    case DK_SHORT2:
        if (src->kind == DK_LONG || src->kind == DK_SHORT2)
            *(int16_t *)dst->ptr = *(const int16_t *)src->ptr;
        break;
    case DK_SYNC:
        memcpy(dst->ptr, src->ptr, 4);
        break;
    default:
        memcpy(dst->ptr, src->ptr, (size_t)stmt_length(dst->kind));
        break;
    }
}

/* Push the named field of the statement the scan is sitting on. Returns
   nonzero when there was nothing there to push. */
int npush_fld(delta_state *d, uint8_t st, uint8_t fld)
{
    delta_vars *v = d->vars;
    const delta_stmt *e = &vstmtbl[st];
    delta_operand out;
    int32_t p;

    out.kind = e->fields[fld].kind;
    out.pad_06 = e->fields[fld].flag;

    if (v->scan_rev == 0)
        p = *(int32_t *)((char *)(intptr_t)v->scan_ptr
                         + 0xc + v->scan_field * 4) & ~3;
    else
        p = *(int32_t *)((char *)(intptr_t)v->scan_ptr
                         + (v->fence_base + v->scan_field) * 4) & ~3;

    if (p == 0)
        return 1;

    while (*(int32_t *)(intptr_t)p & 2) {
        if (v->scan_rev == 0)
            p = *(int32_t *)((char *)(intptr_t)p
                             + 0xc + v->scan_field * 4) & ~3;
        else
            p = *(int32_t *)((char *)(intptr_t)p
                             + (v->fence_base + v->scan_field) * 4) & ~3;
        if (p == 0)
            return 1;
    }

    out.ptr = e->get[fld](TFLDS((void *)(intptr_t)p));
    vnspush(d, &out);
    return 0;
}
