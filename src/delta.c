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
typedef char delta_tpos_is_16[sizeof(delta_tpos) == 16 ? 1 : -1];
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
    d->lpta.flags = 1;
    d->lpta.node = p->value;
    d->lpta.offset = 0;
}

/* Byte for byte the same as lpta_loadp in the original. */
void lpta_loadpn(delta_state *d, const delta_token *p)
{
    d->lpta.flags = 1;
    d->lpta.node = p->value;
    d->lpta.offset = 0;
}

/* Loading the right register clears the left register's word rather than its
   own. Both spellings of it in the original do this, so it is reproduced
   rather than corrected; lpta_rpta_loadp clears both. */
void rpta_loadp(delta_state *d, const delta_token *p)
{
    d->rpta.flags = 1;
    d->rpta.node = p->value;
    d->lpta.offset = 0;
}

void rpta_loadpn(delta_state *d, const delta_token *p)
{
    d->rpta.flags = 1;
    d->rpta.node = p->value;
    d->lpta.offset = 0;
}

/* Both registers at once, which is what a rule matching across a span wants
   and why it is the second most common operation in the whole language. */
void lpta_rpta_loadp(delta_state *d, const delta_token *lp,
                     const delta_token *rp)
{
    d->rpta.flags = 1;
    d->lpta.flags = 1;
    d->lpta.node = lp->value;
    d->rpta.node = rp->value;
    d->rpta.offset = 0;
    d->lpta.offset = 0;
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
AT(owner, 0x0064);
AT(fence_marks, 0x0094);
AT_VARS(err_jmp, 0x0fac);
AT_VARS(loop_tag, 0x0fc0);
AT_VARS(relink, 0x1124);
AT_VARS(nsq_marks, 0x116c);
AT_VARS(fence_base, 0x1174);

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
    SETFENCE(d, (int32_t *)d->lpta.node, idx);
}

void remfence(delta_state *d, int8_t idx)
{
    UNSETFENCE(d, (int32_t *)d->lpta.node, idx);
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

void reset_field(delta_loc *f)
{
    if (f->kind >= 0)
        f->field = -1;
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

/* The fence check vscanadv and its two siblings share. Returns nonzero when
   the scan may not pass, and leaves the loop counter where it stopped so the
   caller can finish clearing the marks from there. */
static int scan_fenced(delta_state *d, int32_t cur, int32_t field,
                       int32_t usefence, int32_t *at)
{
    delta_vars *v = d->vars;
    int32_t i = 0;

    if (v->fence_count != 0 && usefence != 0 && v->scan_held == 0) {
        for (; i < v->fence_count; i++) {
            uint8_t ch = d->fence_chars[i];

            if ((*(int32_t *)(intptr_t)(cur + (v->fence_base + ch) * 4) & 1)
                != 0) {
                *at = i;
                return 1;
            }

            if (FENCED(d, (const int32_t *)(intptr_t)cur,
                       (int8_t)d->fence_chars[i])
                && field != d->fence_chars[i]
                && d->fence_marks[i] == 0) {
                *at = i;
                return 1;
            }

            d->fence_marks[i] = 0;
        }
    }

    *at = i;
    return 0;
}

/* Where the scan's next node is, in whichever direction is set. */
static int32_t scan_step(delta_state *d, int32_t cur, int32_t field)
{
    delta_vars *v = d->vars;

    if (v->scan_rev != 0)
        return *(int32_t *)(intptr_t)
            (cur + (v->fence_base + field) * 4) & ~3;
    return *(int32_t *)(intptr_t)(cur + 0xc + field * 4) & ~3;
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
    int32_t i;

    if (scan_fenced(d, cur, field, usefence, &i))
        return 0;

    if (cur == 0)
        return 0;

    next = scan_step(d, cur, field);
    if (next == 0)
        return 0;

    /* A node that is not itself a sync needs one more step, and only if the
       caller asked to keep going. */
    if ((*(int32_t *)(intptr_t)next & 2) == 0) {
        if (step == 0)
            return 0;
        if (v->scan_rev != 0)
            next = *(int32_t *)(intptr_t)(next + 4) & ~3;
        else
            next = *(int32_t *)(intptr_t)next & ~3;
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

/* A node's context link, the second of the two trailer words that sit between
   its left and right sync arrays. The first is the right-hand spine link. */
static int32_t *clink(delta_state *d, int32_t p)
{
    return (int32_t *)((char *)(intptr_t)p + d->vars->fence_base * 4 - 4);
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

void push_ptr_init(delta_state *d, delta_loc *p)
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
    v.flag = 0;
    vnspush(d, &v);
}

void npush_s(delta_state *d, int32_t x)
{
    delta_operand v;

    v.ptr = &x;
    v.kind = DK_UBYTE;
    v.flag = 0;
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
    out.flag = e->fields[fld].flag;

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

/* Step along the spine until a node both carries the wanted field and is
   sequential. Which way it steps is the caller's choice. */
int32_t *ctxspine(delta_state *d, int32_t *t, uint8_t f, int32_t back)
{
    for (;;) {
        if ((t[d->vars->fence_base + f] & 1) != 0
            && !NONSEQ((const delta_node *)t))
            return t;

        if (back != 0)
            t = (int32_t *)(intptr_t)(*(int32_t *)((char *)t + 4) & ~3);
        else
            t = (int32_t *)(intptr_t)
                (*(int32_t *)((char *)t + d->vars->fence_base * 4 - 8) & ~3);
    }
}

/* Recompute a node's one-statement and all-nonsequential flags from which of
   its fields are present. The first pass counts the fields the language
   nominates, the second sweeps the fenced ones from the top down. */
void vnsqflags(delta_state *d, int32_t *t)
{
    int32_t i = 0;
    int32_t count = 0;
    int32_t all = 0;

    while (d->stack->nsq_fields[i] > -1) {
        if ((t[d->vars->fence_base + d->stack->nsq_fields[i]] & 1) != 0) {
            count++;
            all = 1;
        }
        i++;
    }

    for (i = (int32_t)d->fence_fill - 1; i >= 0; i--) {
        if ((t[d->vars->fence_base + i] & 1) == 0)
            continue;

        if (d->vars->nsq_marks[i] == 0) {
            all = 0;
            count++;
        }
        if (count > 1 && all == 0)
            break;
    }

    if (count == 1)
        SETONESTM((delta_node *)t);
    else
        CLRONESTM((delta_node *)t);

    if (all)
        SETALLNSQ((delta_node *)t);
    else
        CLRALLNSQ((delta_node *)t);
}

/* Turn a compiled location into an operand. A negative first half names one
   of the sized kinds and the value follows inline; otherwise it names a
   statement type and the second half a field, which the language's own
   reader locates. */
void vinitloc_new(delta_state *d, delta_operand *out, const delta_loc *loc)
{
    (void)d;

    if (loc->kind < 0) {
        out->kind = loc->kind;
        switch (out->kind) {
        case DK_LONG:
            out->ptr = (char *)(intptr_t)loc + 4;
            break;
        case DK_SHORT2:
            out->ptr = (char *)(intptr_t)loc + 2;
            break;
        case DK_SYNC:
            out->ptr = (char *)(intptr_t)loc + 4;
            break;
        default:
            break;
        }
        out->flag = 0;
        return;
    }

    if (loc->field == -1) {
        out->kind = loc->kind;
        out->ptr = (char *)(intptr_t)loc + 4;
        out->flag = 0;
        return;
    }

    {
        const delta_stmt *e = &vstmtbl[loc->kind];
        int32_t f = loc->field;

        out->ptr = e->get[f]((char *)(intptr_t)loc + 4);
        out->kind = e->fields[f].kind;
        out->flag = e->fields[f].flag;
    }
}

void startloop(delta_state *d, int16_t tag)
{
    d->vars->test_tag = tag;
    clearDeltaStackBack(d);
    d->vars->testing = 0;
}

void save_var(delta_state *d, const delta_loc *loc)
{
    delta_operand v;

    vinitloc_new(d, &v, loc);
    vpush_var(d, &v);
}

/* Whether the field of the next real statement holds a given byte. The
   field's declared kind does not come into it: the comparison is always one
   byte wide. */
int testFldeq(delta_state *d, uint8_t st, uint8_t fld, uint8_t val)
{
    delta_vars *v = d->vars;
    int32_t p = v->scan_ptr;
    const uint8_t *q;

    for (;;) {
        if (v->scan_rev == 0)
            p = *(int32_t *)((char *)(intptr_t)p
                             + 0xc + v->scan_field * 4) & ~3;
        else
            p = *(int32_t *)((char *)(intptr_t)p
                             + (v->fence_base + v->scan_field) * 4) & ~3;

        if (p == 0)
            return 1;
        if ((*(int32_t *)(intptr_t)p & 2) == 0)
            break;
    }

    q = vstmtbl[st].get[fld](TFLDS((void *)(intptr_t)p));
    return *q == val ? 0 : 1;
}

/* Lay down a fresh statement: the language's default record, then its first
   field set from the caller's value, then whichever variant that value
   selects for the types that declare any. */
void vinitflds(delta_state *d, uint8_t st, void *dst, const void *src)
{
    const delta_stmt *e = &vstmtbl[st];

    (void)d;

    memmove(dst, e->deflt, (size_t)e->length);
    e->put[0](dst, src);

    if (e->variants == NULL)
        return;

    if (e->fields[0].kind == DK_SHORT)
        memmove(dst, e->variants + *(const int16_t *)src * e->stride,
                (size_t)e->varlen);
    else if (e->fields[0].kind == DK_UBYTE)
        memmove(dst, e->variants + *(const uint8_t *)src * e->stride,
                (size_t)e->varlen);
}

/* Advance the scan past a whole token: keep going while each node it reaches
   is a sync, and stop on the first that is not. */
int vscanadvOverToken(delta_state *d, int32_t usefence)
{
    delta_vars *v = d->vars;
    int32_t cur = v->scan_ptr;
    int32_t field = v->scan_field;

    for (;;) {
        int32_t next, i;

        if (cur == 0)
            return 0;

        if (scan_fenced(d, cur, field, usefence, &i))
            return 0;

        next = scan_step(d, cur, field);
        if (next == 0)
            return 0;

        v->scan_ptr = next;
        v->scan_held = 0;
        for (; i < v->fence_count; i++)
            d->fence_marks[i] = 0;

        if ((*(int32_t *)(intptr_t)next & 2) != 0) {
            cur = next;
            continue;
        }

        if (v->scan_rev != 0)
            v->scan_ptr = *(int32_t *)(intptr_t)(next + 4) & ~3;
        else
            v->scan_ptr = *(int32_t *)(intptr_t)next & ~3;
        return 1;
    }
}

/* The same walk, but stopping either at a node that is not a sync or at a
   named one, whichever comes first. */
int vscanadvUptoTokenOrMarker(delta_state *d, int32_t target, int32_t usefence)
{
    delta_vars *v = d->vars;
    int32_t cur = v->scan_ptr;
    int32_t field = v->scan_field;

    for (;;) {
        int32_t next, i;

        if (cur == 0)
            return 0;

        if (scan_fenced(d, cur, field, usefence, &i))
            return 0;

        next = scan_step(d, cur, field);
        if (next == 0)
            return 0;
        if ((*(int32_t *)(intptr_t)next & 2) == 0)
            return 1;

        cur = next;
        v->scan_ptr = next;
        v->scan_held = 0;
        for (; i < v->fence_count; i++)
            d->fence_marks[i] = 0;

        if (next == target)
            return 1;
    }
}

/* Walk a run of statements, stopping at the first that carries one of the
   fields the node behind the start declares. */
void seqscan(delta_state *d, delta_seqctl *c)
{
    int32_t base = d->vars->fence_base;
    int32_t back = c->kind == 1;
    int32_t t = c->start;
    int32_t peer;
    /* The original's own array, sized by what its frame gave it. */
    uint8_t fields[104];
    uint8_t n = 0;
    uint8_t i;

    c->cur = c->start;

    if (back)
        peer = *(int32_t *)(intptr_t)(t + 4) & ~3;
    else
        peer = *(int32_t *)(intptr_t)(t + base * 4 - 8) & ~3;

    for (i = 0; i < d->fence_fill; i++)
        if ((*(int32_t *)(intptr_t)(peer + (base + i) * 4) & 1) != 0)
            fields[n++] = i;

    for (;;) {
        for (i = 0; i < n; i++)
            if ((*(int32_t *)(intptr_t)(t + (base + fields[i]) * 4) & 1) != 0)
                return;

        if (!ONESTM((const delta_node *)(intptr_t)t)
            || !ALLNSQ((const delta_node *)(intptr_t)t))
            c->flag = 1;

        c->cur = t;

        if (back)
            t = *(int32_t *)(intptr_t)(t + base * 4 - 8) & ~3;
        else
            t = *(int32_t *)(intptr_t)(t + 4) & ~3;
    }
}

int advance_tok(delta_state *d)
{
    return vscanadvOverToken(d, 1) ? 0 : 1;
}

/* Restart a forall from a given value: assign the source to the loop
   variable, note what is being iterated, and drop the backtracking the
   previous pass left. */
int forall_cont_from(delta_state *d, int16_t tag, int16_t loop,
                     int32_t unused, delta_loc *dst, const delta_loc *src)
{
    delta_operand dv, sv;

    (void)unused;

    if (d->vars->testing)
        save_var(d, dst);

    vinitloc_new(d, &dv, dst);
    vinitloc_new(d, &sv, src);
    vassign(d, &dv, &sv);

    d->vars->loop_tag = loop;
    d->vars->test_tag = tag;
    clearDeltaStackBack(d);
    d->stack->unknown_9c = 0;

    reset_field(dst);
    reset_field((delta_loc *)(intptr_t)src);
    return 2;
}

/* A context record naming what is being tried, then a copy of where the scan
   had got to. Anything that may have to be unwound pushes this pair. */
static void push_ca_and_scan(delta_state *d, int16_t tag)
{
    delta_stack *s = d->stack;
    uint8_t *ca;
    uint8_t *save;

    s->top -= s->ca_size;
    ca = s->top;
    s->limit -= s->ca_size;
    ca[0] = 3;
    *(int32_t *)(ca + 4) = tag;

    s->top -= s->size_b0;
    save = s->top;
    s->limit -= s->size_b0;
    save[0] = 1;
    memcpy(save + 4, &d->vars->scan_ptr, 8);
}

/* Remember where the scan is, both in the caller's variable and on the
   backtracking stack, so an unwind can put it back. */
void savescptr(delta_state *d, int16_t tag, delta_loc *v)
{

    if (d->vars->testing)
        save_var(d, v);

    v->value = d->vars->scan_ptr;
    push_ca_and_scan(d, tag);
}

/* Fetch a rule's parameter into a cell of the wanted kind, narrowing or
   widening as the source needs. A short parameter lands in the cell's field
   half and a long one in its value. */
int get_parm(delta_state *d, delta_loc *out, delta_loc *loc, int16_t kind)
{
    int32_t err = 0;
    delta_operand v;

    out->kind = kind;

    switch (out->kind) {
    case DK_SYNC:
        out->value = loc->value;
        if (!push_ptr(d, (int32_t)(intptr_t)out))
            err = 1;
        break;

    case DK_LONG:
        if (loc->kind == DK_LONG) {
            out->value = loc->value;
        } else if (loc->kind == DK_SHORT2) {
            out->value = loc->field;
        } else if (loc->kind >= 0) {
            vinitloc_new(d, &v, loc);
            out->value = *(const int16_t *)v.ptr;
            reset_field(loc);
        } else {
            err = 1;
        }
        break;

    case DK_SHORT2:
        if (loc->kind == DK_LONG) {
            out->field = (int16_t)loc->value;
        } else if (loc->kind == DK_SHORT2) {
            out->field = loc->field;
        } else if (loc->kind >= 0) {
            vinitloc_new(d, &v, loc);
            out->field = *(const int16_t *)v.ptr;
            reset_field(loc);
        } else {
            err = 1;
        }
        break;

    default:
        err = 1;
        break;
    }

    return err;
}

/* Walk the scan forward until every one of the named fields is present at
   once, then mark them so the fence lets the rule past them. */
int test_synch(delta_state *d, int16_t tag, uint8_t n, const uint8_t *list)
{
    delta_vars *v = d->vars;
    int32_t ok = 0;
    int32_t i;

    while (ok == 0) {
        ok = 1;
        for (i = 0; i < n && ok != 0; i++) {
            if ((*(int32_t *)(intptr_t)
                 (v->scan_ptr + (v->fence_base + list[i]) * 4) & 1) != 0)
                continue;

            ok = 0;
            if (!vscanadv(d, 0, 1))
                return 1;
        }
    }

    push_ca_and_scan(d, tag);

    for (i = 0; i < n; i++)
        d->fence_marks[d->fence_index[list[i]]] = 1;

    return 0;
}

/* Where the scan's field points from a given node, which the string tests
   need before they have moved the scan. */
static int32_t scan_peek(delta_state *d)
{
    delta_vars *v = d->vars;

    if (v->scan_rev != 0)
        return *(int32_t *)(intptr_t)
            (v->scan_ptr + (v->fence_base + v->scan_field) * 4) & ~3;
    return *(int32_t *)(intptr_t)
        (v->scan_ptr + 0xc + v->scan_field * 4) & ~3;
}

/* Match a run of statements against a string of sixteen-bit values, each
   stored big end first with the sign in the top bit of the first byte. */
int test_string_i(delta_state *d, uint8_t st, uint8_t n, const uint8_t *str)
{
    const delta_stmt *e = &vstmtbl[st];
    const uint8_t *p = str;
    const uint8_t *end = str + n;
    int16_t want = 0;
    delta_operand a, b;

    a.ptr = &want;
    a.kind = DK_SHORT2;
    a.flag = e->fields[0].flag;
    b.kind = e->fields[0].kind;
    b.flag = e->fields[0].flag;

    while (p < end) {
        int32_t node = scan_peek(d);

        if (node == 0)
            return 1;

        if ((*(int32_t *)(intptr_t)node & 2) == 0) {
            want = (int16_t)(((p[0] & 0x7f) << 8) | p[1]);
            if ((p[0] & 0x80) != 0)
                want = (int16_t)(-want);
            p += 2;

            b.ptr = e->get[0](TFLDS((void *)(intptr_t)node));
            vcompare(d, &a, &b);
            if (d->vars->compared_equal != 0)
                return 1;
        }

        if (!vscanadv(d, 1, 1))
            return 1;
    }

    return 0;
}

/* The same against a string of bytes. When the language already declares the
   field as a byte the comparison is direct; otherwise it goes through the
   general one. */
int test_string_s(delta_state *d, uint8_t st, uint8_t n, const uint8_t *str)
{
    const delta_stmt *e = &vstmtbl[st];
    const uint8_t *p = str;
    const uint8_t *end = str + n;

    if (e->fields[0].kind == DK_UBYTE) {
        while (p < end) {
            int32_t node = scan_peek(d);

            if (node == 0)
                return 1;

            if ((*(int32_t *)(intptr_t)node & 2) == 0) {
                if (*(const uint8_t *)
                    e->get[0](TFLDS((void *)(intptr_t)node)) != *p)
                    return 1;
                p++;
            }

            if (!vscanadv(d, 1, 1))
                return 1;
        }

        return 0;
    }

    {
        delta_operand a, b;

        a.kind = DK_UBYTE;
        a.flag = e->fields[0].flag;
        b.kind = e->fields[0].kind;
        b.flag = a.flag;

        while (p < end) {
            int32_t node = scan_peek(d);

            if (node == 0)
                return 1;

            if ((*(int32_t *)(intptr_t)node & 2) == 0) {
                a.ptr = (void *)(intptr_t)p;
                p++;

                b.ptr = e->get[0](TFLDS((void *)(intptr_t)node));
                vcompare(d, &a, &b);
                if (d->vars->compared_equal != 0)
                    return 1;
            }

            if (!vscanadv(d, 1, 1))
                return 1;
        }
    }

    return 0;
}

/* Find the statement that governs a context. Three passes: thread every node
   that carries the field onto a chain through the context links, walk that
   chain following each node's nonsequential link until one reaches the end of
   the spine, then unpick the chain and return whichever node survived.

   Every link it borrows is put back before it returns, which is why the last
   pass masks the same words twice: it clears the pointer, then the two flag
   bits, in the order the original does. */
int32_t ctxlook(delta_state *d, int32_t t, uint8_t f, int32_t right)
{
    int32_t base = d->vars->fence_base;
    int32_t first = t;
    int32_t anchor = t;
    int32_t cur = t;
    int32_t marked = 0;
    int32_t depth = 1;
    int32_t next = 0;
    int32_t limit;
    int32_t result = 0;
    uint8_t i;

    while (depth > 0) {
        while (cur != 0
               && (*(int32_t *)(intptr_t)(cur + (base + f) * 4) & 1) != 0) {
            marked = cur;
            cur = *clink(d, cur) & ~3;
        }

        if (cur == 0)
            break;

        for (i = 0; i < d->fence_fill; i++) {
            int32_t sync;

            if ((*(int32_t *)(intptr_t)(cur + (base + i) * 4) & 1) == 0)
                continue;

            if (right)
                sync = VLSYNC((const delta_node *)(intptr_t)cur, (int8_t)i);
            else
                sync = VRSYNC(d, (const int32_t *)(intptr_t)cur, (int8_t)i);

            if (sync == 0)
                continue;
            if ((*clink(d, sync) & ~3) != 0)
                continue;
            if (sync == anchor)
                continue;

            *clink(d, anchor) = (*clink(d, anchor) & 3) | sync;
            anchor = sync;
            depth++;
        }

        next = *clink(d, cur) & ~3;
        *clink(d, cur) &= 3;

        if (marked != 0)
            *clink(d, marked) = (*clink(d, marked) & 3) | next;
        else
            first = next;

        cur = next;
        depth--;
    }

    limit = right ? d->stack->spine_r : d->stack->spine_l;
    result = 0;

    while (depth > 1) {
        cur = first;

        while (cur != 0) {
            delta_node *n = (delta_node *)(intptr_t)cur;
            int32_t nx;
            int32_t sync;
            int32_t from;

            next = *clink(d, cur) & ~3;

            if ((n->flags8 & 1) != 0) {
                cur = next;
                continue;
            }

            nx = n->flags8 & ~3;
            from = nx ? nx : cur;

            if (right)
                sync = VRSYNC(d, (const int32_t *)(intptr_t)from, (int8_t)f);
            else
                sync = VLSYNC((const delta_node *)(intptr_t)from, (int8_t)f);

            if (sync == limit) {
                depth = 1;
                result = cur;
                break;
            }

            if ((*clink(d, sync) & ~3) != 0 || sync == anchor) {
                n->flags8 |= 1;
                depth--;
            } else {
                n->flags8 = (n->flags8 & 3) | sync;
            }

            cur = next;
        }
    }

    cur = first;
    while (cur != 0) {
        delta_node *n = (delta_node *)(intptr_t)cur;

        if (result == 0 && (n->flags8 & 1) == 0)
            result = cur;

        next = *clink(d, cur) & ~3;

        /* The immediate is 0xfffffffe: it clears bit zero, not bit one. */
        *clink(d, cur) &= 3;
        n->flags8 &= 3;
        n->flags8 &= ~1;
        *clink(d, cur) &= ~1;

        cur = next;
    }

    return result;
}

/* Read a field's first value out of a node, in whichever width the language
   declares it. The short form has its own spelling of "no value". */
static int32_t tfield(const delta_stmt *e, void *(*get)(void *), int32_t node,
                      int32_t previous)
{
    int16_t kind = e->fields[0].kind;
    int32_t value = previous;

    if (kind == DK_LONG) {
        value = *(int32_t *)get(TFLDS((void *)(intptr_t)node));
    } else if (kind == DK_SHORT2) {
        value = *(int16_t *)get(TFLDS((void *)(intptr_t)node));
        if (value == (int32_t)0xffff8001)
            value = (int32_t)0x80000001;
    }

    return value;
}

/* Put a timing position back in range: spend its offset walking the field's
   run until it fits inside one statement, then snap to a boundary if the
   caller asked for one. The return says what was found. One means the walk
   ran off the end of the spine, two that an offset is left over, three that
   the next statement holds nothing, four that the position is exact.

   The original leaves the value it reads uninitialised for any statement type
   whose first field is neither a long nor a short, and no shipped type is. */
int vnormalize(delta_state *d, delta_tpos *p)
{
    const delta_stmt *e;
    void *(*get)(void *);
    int32_t node = p->node;
    int8_t f = p->field;
    int32_t off = p->offset;
    int32_t base = d->vars->fence_base;
    int32_t next;
    int32_t value = 0;
    uint8_t went_right;
    uint8_t adjusted;

    e = &vstmtbl[f];
    get = e->get[0];

    if (off < 0) {
        went_right = 0;
        next = *(int32_t *)(intptr_t)(node + 0xc + f * 4) & ~3;

        while (node != d->stack->spine_l) {
            if (next != 0 && (*(int32_t *)(intptr_t)next & 2) != 0) {
                node = next;
                next = *(int32_t *)(intptr_t)(node + 0xc + f * 4) & ~3;
                continue;
            }

            value = tfield(e, get, next, value);
            if (value == (int32_t)0x80000001)
                break;
            if (off + value > 0)
                break;

            off += value;
            next = *(int32_t *)(intptr_t)next & ~3;
        }
    } else {
        went_right = 1;
        next = *(int32_t *)(intptr_t)(node + (base + f) * 4) & ~3;

        while (node != d->stack->spine_r) {
            if (next != 0 && (*(int32_t *)(intptr_t)next & 2) != 0) {
                node = next;
                next = *(int32_t *)(intptr_t)(node + (base + f) * 4) & ~3;
                continue;
            }

            value = tfield(e, get, next, value);
            if (value == (int32_t)0x80000001)
                break;
            if (off - value < 0)
                break;

            off -= value;
            next = *(int32_t *)(intptr_t)(next + 4) & ~3;
        }
    }

    if ((p->flags & 4) != 0) {
        if (off < 0) {
            next = *(int32_t *)(intptr_t)(node + 0xc + f * 4) & ~3;
            if (next == 0 || (*(int32_t *)(intptr_t)next & 2) == 0)
                node = *(int32_t *)(intptr_t)next & ~3;
        } else if (off == 0) {
            node = (int32_t)(intptr_t)lmost(d, f,
                                            (delta_node *)(intptr_t)node);
        }
        off = 0;
        went_right = 0;
        p->flags ^= 4;
        adjusted = 1;
    } else if ((p->flags & 8) != 0) {
        if (off > 0) {
            next = *(int32_t *)(intptr_t)(node + (base + f) * 4) & ~3;
            if (next == 0 || (*(int32_t *)(intptr_t)next & 2) == 0)
                node = *(int32_t *)(intptr_t)(next + 4) & ~3;
        } else if (off == 0) {
            node = (int32_t)(intptr_t)rmost(d, f,
                                            (int32_t *)(intptr_t)node);
        }
        off = 0;
        went_right = 1;
        p->flags ^= 8;
        adjusted = 1;
    } else {
        adjusted = 0;
    }

    p->node = node;
    p->offset = off;

    if ((node == d->stack->spine_l && off < 0)
        || (node == d->stack->spine_r && off > 0))
        return 1;
    if (off != 0)
        return 2;
    if (adjusted)
        return 4;

    /* Look the other way from the one it travelled: an exact position is at
       the start of a run only if what precedes it holds nothing. */
    if (went_right)
        next = *(int32_t *)(intptr_t)(node + 0xc + f * 4) & ~3;
    else
        next = *(int32_t *)(intptr_t)(node + (base + f) * 4) & ~3;

    if (e->fields[0].kind == DK_LONG) {
        if (next != 0 && (*(int32_t *)(intptr_t)next & 2) != 0)
            return 3;
        if (next == 0)
            return 4;
        if (*(int32_t *)get(TFLDS((void *)(intptr_t)next)) == 0)
            return 3;
    } else if (e->fields[0].kind == DK_SHORT2) {
        if (next != 0 && (*(int32_t *)(intptr_t)next & 2) != 0)
            return 3;
        if (next == 0)
            return 4;
        if (*(int16_t *)get(TFLDS((void *)(intptr_t)next)) == 0)
            return 3;
    }

    return 4;
}

/* Give a statement a place in a field's chain, between the two neighbours the
   caller found. Whether both, one or neither of them is a sync decides which
   of the three splices happens; none of them and there is nothing to do.

   The two locals the original computes for the first case are never read
   again, so only their reads survive here, in the branch that would have
   made them. */
int vproject(delta_state *d, int32_t t, int32_t left, int32_t right, uint8_t f)
{
    int32_t base = d->vars->fence_base;
    int32_t l = left;
    int32_t r = right;

    if ((*(int32_t *)(intptr_t)(t + (base + f) * 4) & 1) != 0)
        return 1;

    if (left != 0 && (*(int32_t *)(intptr_t)left & 2) != 0
        && right != 0 && (*(int32_t *)(intptr_t)right & 2) != 0) {
        *(int32_t *)(d->owner + DELTA_OWNER_CHANGED) = 1;
        *(int32_t *)(intptr_t)(t + (base + f) * 4) |= 1;

        CLRONESTM((delta_node *)(intptr_t)t);
        if (ALLNSQ((const delta_node *)(intptr_t)t)
            && d->vars->nsq_marks[f] == 0)
            CLRALLNSQ((delta_node *)(intptr_t)t);

        *(int32_t *)(intptr_t)(l + (base + f) * 4) =
            (*(int32_t *)(intptr_t)(l + (base + f) * 4) & 3) | t;
        *(int32_t *)(intptr_t)(r + 0xc + f * 4) =
            (*(int32_t *)(intptr_t)(r + 0xc + f * 4) & 3) | t;
        *(int32_t *)(intptr_t)(t + (base + f) * 4) =
            (*(int32_t *)(intptr_t)(t + (base + f) * 4) & 3) | r;
        *(int32_t *)(intptr_t)(t + 0xc + f * 4) =
            (*(int32_t *)(intptr_t)(t + 0xc + f * 4) & 3) | l;
    } else if (right != 0 && (*(int32_t *)(intptr_t)right & 2) != 0) {
        *(int32_t *)(d->owner + DELTA_OWNER_CHANGED) = 1;
        *(int32_t *)(intptr_t)(t + (base + f) * 4) |= 1;

        CLRONESTM((delta_node *)(intptr_t)t);
        if (ALLNSQ((const delta_node *)(intptr_t)t)
            && d->vars->nsq_marks[f] == 0)
            CLRALLNSQ((delta_node *)(intptr_t)t);

        ((delta_node *)(intptr_t)left)->link = t;
        *(int32_t *)(intptr_t)(t + (base + f) * 4) =
            (*(int32_t *)(intptr_t)(t + (base + f) * 4) & 3) | right;
        *(int32_t *)(intptr_t)(r + 0xc + f * 4) =
            (*(int32_t *)(intptr_t)(r + 0xc + f * 4) & 3) | t;
        *(int32_t *)(intptr_t)(t + 0xc + f * 4) =
            (*(int32_t *)(intptr_t)(t + 0xc + f * 4) & 3) | left;
    } else if (left != 0 && (*(int32_t *)(intptr_t)left & 2) != 0) {
        *(int32_t *)(d->owner + DELTA_OWNER_CHANGED) = 1;
        *(int32_t *)(intptr_t)(t + (base + f) * 4) |= 1;

        CLRONESTM((delta_node *)(intptr_t)t);
        if (ALLNSQ((const delta_node *)(intptr_t)t)
            && d->vars->nsq_marks[f] == 0)
            CLRALLNSQ((delta_node *)(intptr_t)t);

        *(int32_t *)(intptr_t)(l + (base + f) * 4) =
            (*(int32_t *)(intptr_t)(l + (base + f) * 4) & 3) | t;
        *(int32_t *)(intptr_t)(t + (base + f) * 4) =
            (*(int32_t *)(intptr_t)(t + (base + f) * 4) & 3) | right;
        *(int32_t *)(intptr_t)right = t;
        *(int32_t *)(intptr_t)(t + 0xc + f * 4) =
            (*(int32_t *)(intptr_t)(t + 0xc + f * 4) & 3) | left;
    } else {
        return 0;
    }

    if (NONSEQ((const delta_node *)(intptr_t)t) && d->vars->relink != 0) {
        DELSPINE(d, (delta_node *)(intptr_t)t);
        INSSPINEL(d, (delta_node *)(intptr_t)t, (delta_node *)(intptr_t)r);
    }

    return 1;
}

/* Settle a timing position and leave it settled. */
int vmove_tv(delta_state *d, delta_tpos *p)
{
    if ((p->flags & 1) != 0)
        return 1;

    vnormalize(d, p);
    p->flags = 1;
    return 1;
}

/* Whether a position lands on a sync. Anything vnormalize could not place
   settles the position and fails. */
int vtstsnc_tv(delta_state *d, delta_tpos *p)
{
    int32_t r;

    if ((p->flags & 1) != 0)
        return 0;

    r = vnormalize(d, p);
    if (r >= 0 && (r <= 1 || r == 2))
        return 1;

    p->flags = 1;
    return 0;
}

/* Whether a position lands on a timing mark. A position that fell just short
   of one is dragged to the end of the run it is in first. */
int vtsttmark_tv(delta_state *d, delta_tpos *p, uint8_t back)
{
    int32_t r;

    if ((p->flags & 1) != 0)
        return 0;

    r = vnormalize(d, p);

    if (r >= 0) {
        if (r <= 1 || r == 2)
            return 1;
        if (r == 3) {
            if (back == 0)
                p->node = (int32_t)(intptr_t)
                    rmost(d, p->field, (int32_t *)(intptr_t)p->node);
            else
                p->node = (int32_t)(intptr_t)
                    lmost(d, p->field, (delta_node *)(intptr_t)p->node);
        }
    }

    p->flags = 1;
    return 0;
}

/* Whether the scan has reached where the left register points. */
int test_ptr(delta_state *d)
{
    if (d->lpta.node == 0)
        return 1;

    if ((d->lpta.flags & 2) != 0)
        vnormalize(d, &d->lpta);

    for (;;) {
        if (d->vars->scan_ptr == d->lpta.node)
            return 0;
        if (!vscanadv(d, 0, 1))
            return 1;
    }
}
