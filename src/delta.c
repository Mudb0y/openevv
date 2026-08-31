#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "evv_land.h"

#include "delta.h"
#include "evv_arena.h"
#include "eci_eloqc.h"

#define AT(field, offset) \
    typedef char field##_at_##offset[offsetof(delta_state, field) == offset ? 1 : -1]
#define AT_VARS(field, offset) \
    typedef char field##_at_##offset[offsetof(delta_vars, field) == offset ? 1 : -1]

AT(sets, 0x0028);
AT(unknown_3c, 0x003c);
AT(lpta, 0x0040);
AT(rpta, 0x0050);
AT(vars, 0x0068);
AT(stack, 0x006c);

/* The named fields stop where the language's own cells start. How far the
   cells run is the language's to say and is not a number this file can
   know, so what is held here is the boundary rather than the whole. */
typedef char delta_state_ends_at_the_cells[sizeof(delta_state) == DG_BASE
                                           ? 1 : -1];
typedef char delta_pta_is_16[sizeof(delta_pta) == 16 ? 1 : -1];
typedef char delta_tpos_is_16[sizeof(delta_tpos) == 16 ? 1 : -1];
/* The language's own description of itself. Nothing compiled from a rule
   reaches into either table -- a rule names a statement type by number and
   asks the runtime -- so where a pointer is eight bytes these two are allowed
   to grow. Everything above holds whatever the host is, because the rules do
   reach into all of it. */
#if !defined(EVV_ARENA) || !EVV_ARENA
typedef char delta_stmt_is_0x40[sizeof(delta_stmt) == 0x40 ? 1 : -1];
typedef char delta_fielddesc_is_0x18[sizeof(delta_fielddesc) == 0x18 ? 1 : -1];
#endif

/* A walk that is not going to finish.
 *
 * Most of what is below walks the delta: it steps from one node to the next
 * along a link and stops when the chain ends or when it reaches what it was
 * told to look for. That is sound while the links go somewhere. Links that
 * have come round on themselves take the walk round with them, and the engine
 * stops answering instead of saying anything -- which, in something a screen
 * reader has loaded into itself, is worse than a fault, because a fault at
 * least ends.
 *
 * The machine keeps no count of its nodes to hold a walk against, so this is
 * a ceiling and not a measure. What a walk over a delta that is whole costs
 * was measured rather than reasoned about, over every case in the suite and
 * over the strings the NVDA driver keeps a list of because they kill the
 * original: the deepest was EVV_MEASURED. The ceiling is far above that and
 * far below what a person would sit through, and reaching it is treated the
 * way the machine treats everything else it is asked to do and cannot -- the
 * rule backtracks, and the utterance goes rather than the process.
 *
 * No text reaches it. It is here for a delta that is already wrong.
 */
#define EVV_WALK_MAX  200000

#define EVV_WALK(n)        int32_t n = EVV_WALK_MAX
#define EVV_WALKED(d, n)   do { if (--(n) < 0) forceErrorBacktrack(d); } while (0)

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
    slot = (delta_frame *)EVV_AT(uint8_t *, s->top);
    s->limit -= size;
    return slot;
}

/* A context record, carrying the tag the rule is testing against. */
void bspush_ca(delta_state *d, int16_t tag)
{
    delta_frame *slot = bs_push(EVV_AT(delta_stack *, d->stack), EVV_AT(delta_stack *, d->stack)->ca_size);

    slot->kind = 0;
    slot->value = tag;
}

/* Clearing back to the mark and pushing a context record in one, which is
   what starttest does the long way round. */
void bsclr_pushca(delta_state *d, int16_t tag)
{
    delta_stack *s;
    delta_frame *slot;

    clearDeltaStackBack(d);

    s = EVV_AT(delta_stack *, d->stack);
    slot = bs_push(s, s->ca_size);
    slot->kind = 0;
    slot->value = tag;
}

/* Where the variable bottom was, kept on the stack so that a backtrack can
   put it back. The record carries the old bottom and becomes the new one. */
void bspush_vbot(delta_state *d)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);
    delta_frame *slot = bs_push(s, s->size_b8);

    slot->kind = 5;
    slot->value = EVV_REF(getDeltaStackVBot(d));
    setDeltaStackVBot(d, slot);
}

/* And taking it off again: the bottom goes back to what the record says
   before the record itself is dropped. */
void bspop_vbot(delta_state *d)
{
    const delta_frame *slot =
        EVV_AT(const delta_frame *, EVV_AT(delta_stack *, d->stack)->top);

    setDeltaStackVBot(d, EVV_AT(void *, slot->value));
    popDeltaStackTop(d);
}

/* The two markers a rule leaves where an alternative begins. */
void bspush_boa(delta_state *d)
{
    bs_push(EVV_AT(delta_stack *, d->stack), EVV_AT(delta_stack *, d->stack)->boa_size)->kind = 4;
}

void bspush_nboa(delta_state *d)
{
    bs_push(EVV_AT(delta_stack *, d->stack), EVV_AT(delta_stack *, d->stack)->boa_size)->kind = 6;
}

int testeq(delta_state *d)
{
    return EVV_AT(delta_vars *, d->vars)->compared_equal != 0;
}

int testneq(delta_state *d)
{
    return EVV_AT(delta_vars *, d->vars)->compared_equal == 0;
}

/* The four orderings, read off the same byte the last comparison left. They
   answer the way every test here answers: nought when the test held and one
   when it did not, which is what the caller backtracks on. The `if_' forms
   compare first and then read; these only read, and no rule in the languages
   IBM shipped calls one, which is why they arrive now. */
int testgt(delta_state *d)
{
    return EVV_AT(delta_vars *, d->vars)->compared_equal != 1;
}

int testge(delta_state *d)
{
    return EVV_AT(delta_vars *, d->vars)->compared_equal == -1;
}

int testlt(delta_state *d)
{
    return EVV_AT(delta_vars *, d->vars)->compared_equal != -1;
}

int testle(delta_state *d)
{
    return EVV_AT(delta_vars *, d->vars)->compared_equal == 1;
}

/* A timing mark on the spine, and the record a rule leaves so a backtrack
 * can put the scan back.
 *
 * The mark test is asked of a copy of the left register rather than of the
 * register, so a rule that asks does not move. What follows walks the scan
 * forward until it reaches the position the register names, comparing the
 * two as it goes; a comparison that comes out unequal advances and asks
 * again, and a comparison that will not be made at all is a failure.
 *
 * When it arrives, two records go on the stack: the tag the rule is testing
 * against, and the scan's own eight bytes, so that a backtrack restores where
 * the scan had got to. The last line arms the fence for the register's own
 * field, which is what keeps the scan inside the run the rule is looking at.
 */
int test_time(delta_state *d, int16_t tag)
{
    delta_vars  *v = EVV_AT(delta_vars *, d->vars);
    delta_stack *s;
    delta_frame *slot;
    delta_tpos   p;

    memcpy(&p, &d->lpta, sizeof p);
    if (vtsttmark_tv(d, &p, 0))
        return 1;

    d->rpta.flags = 1;
    d->rpta.node = v->scan_ptr;

    EVV_WALK(w);

    for (;;) {
        EVV_WALKED(d, w);

        if (vcomp_pta(d, &d->lpta, &d->rpta))
            return 1;
        if (v->compared_equal == 0)
            break;
        if (!vscanadv(d, 0, 1))
            return 1;
        d->rpta.node = v->scan_ptr;
    }

    s = EVV_AT(delta_stack *, d->stack);

    slot = bs_push(s, s->ca_size);
    slot->kind = 3;
    slot->value = tag;

    slot = bs_push(s, s->size_b0);
    slot->kind = 1;
    memcpy((uint8_t *)slot + 4, &v->scan_ptr, 8);

    EVV_AT(uint8_t *, d->fence_marks)
        [EVV_AT(uint8_t *, d->fence_index)[(uint8_t)d->lpta.field]] = 1;

    return 0;
}

/* Walk the scan forward until every field asked about is fenced where it
 * stands, and then hold it there.
 *
 * With no characters named it is every field the language declares except
 * the one the scan is walking, and only those the fence table does not
 * already carry; with characters named it is those, and the scan's own field
 * is not exempt. Either way a field that is not fenced at the scan's position
 * advances the scan and starts the sweep again, because moving may unfence
 * one that was fenced a moment ago. A scan that will not advance is a
 * failure.
 *
 * The two records left behind are test_time's, and what the tail does with
 * them is where the two forms part: with no characters the scan is simply
 * held, and with characters each of their fences is armed instead.
 */
int test_fence(delta_state *d, int16_t tag, uint8_t n, const uint8_t *chars)
{
    delta_vars  *v = EVV_AT(delta_vars *, d->vars);
    delta_stack *s;
    delta_frame *slot;
    int32_t settled = 0;
    int8_t  i;
    EVV_WALK(w);

    if (n == 0) {
        while (settled == 0) {
            EVV_WALKED(d, w);
            settled = 1;
            for (i = 0; i < d->nstmts && settled != 0; i++) {
                if (i == (int8_t)v->scan_field)
                    continue;
                if (EVV_AT(uint8_t *, d->fence_index)[i] != d->nstmts)
                    continue;
                if (FENCED(d, EVV_AT(const int32_t *, v->scan_ptr), i))
                    continue;

                settled = 0;
                if (!vscanadv(d, 0, 1))
                    return 1;
            }
        }
    } else {
        while (settled == 0) {
            EVV_WALKED(d, w);
            settled = 1;

            for (i = 0; i < n && settled != 0; i++) {
                if (EVV_AT(uint8_t *, d->fence_index)[chars[i]] != d->nstmts)
                    continue;
                if (FENCED(d, EVV_AT(const int32_t *, v->scan_ptr),
                           (int8_t)chars[i]))
                    continue;

                settled = 0;
                if (!vscanadv(d, 0, 1))
                    return 1;
            }
        }
    }

    s = EVV_AT(delta_stack *, d->stack);

    slot = bs_push(s, s->ca_size);
    slot->kind = 3;
    slot->value = tag;

    slot = bs_push(s, s->size_b0);
    slot->kind = 1;
    memcpy((uint8_t *)slot + 4, &v->scan_ptr, 8);

    if (n == 0) {
        v->scan_held = 1;
        return 0;
    }

    for (i = 0; i < n; i++)
        EVV_AT(uint8_t *, d->fence_marks)
            [EVV_AT(uint8_t *, d->fence_index)[chars[i]]] = 1;

    return 0;
}

/* Whether a logical file has run out. */
int test_eof(delta_state *d, int32_t lf)
{
    return vf_eof(d, lf) == 0;
}

/* This one tests nothing. It clears the two fields in the owner block that
   the save layer clears and reports that the test did not hold, whatever was
   asked; the answer to "has a value" is arrived at elsewhere. Kept because a
   rule may call it and because what it writes is observable. */
int test_hasval(delta_state *d)
{
    delta_owner *o = EVV_AT(delta_owner *, d->owner);

    o->unknown_1a8 = 0;
    o->unknown_14 = 0;
    return 1;
}

AT(fence_chars, 0x0084);
AT(fence_index, 0x008c);
AT(nstmts, 0x0098);
AT(owner, 0x0064);
AT(fence_marks, 0x0094);
AT_VARS(err_jmp, 0x0fac);
AT_VARS(loop_tag, 0x0fc0);
AT_VARS(ctx_both, 0x1120);
AT_VARS(relink, 0x1124);
AT_VARS(nsq_marks, 0x116c);
AT_VARS(fence_base, 0x1174);
AT_VARS(gen_stmt, 0x0fb4);
AT_VARS(gen_now, 0x0fe4);
AT_VARS(gen_done, 0x0ff4);
AT_VARS(gen_len, 0x1004);
AT_VARS(gen_at, 0x1030);
AT_VARS(gen_src, 0x106c);
AT_VARS(gen_dst, 0x1074);

/* A context record and a saved scan position together, which is what a rule
   pushes when it is about to try a match it may need to unwind. */
void bspush_ca_scan(delta_state *d, int16_t tag)
{
    delta_frame *ca = bs_push(EVV_AT(delta_stack *, d->stack), EVV_AT(delta_stack *, d->stack)->ca_size);
    delta_frame *save;

    ca->kind = 0;
    ca->value = tag;

    save = bs_push(EVV_AT(delta_stack *, d->stack), EVV_AT(delta_stack *, d->stack)->size_b0);
    save->kind = 1;
    memcpy(&save->value, &EVV_AT(delta_vars *, d->vars)->scan_ptr, 8);
}

/* Build the character fence: a set of characters the rules match against,
   held both ways round so either direction is a single lookup. */
void fence(delta_state *d, int8_t n, const uint8_t *chars)
{
    uint8_t i;

    EVV_AT(delta_vars *, d->vars)->fence_count = n;
    memset(EVV_AT(uint8_t *, d->fence_index), d->nstmts, d->nstmts);

    for (i = 0; (int)i < (int)(uint8_t)n; i++) {
        EVV_AT(uint8_t *, d->fence_chars)[i] = chars[i];
        EVV_AT(uint8_t *, d->fence_index)[chars[i]] = i;
    }
}

/* The field block of a record sits eight bytes in. */
void *TFLDS(void *p)
{
    return (uint8_t *)p + 8;
}

void *getDeltaStackVBot(delta_state *d)
{
    return EVV_AT(uint8_t *, EVV_AT(delta_stack *, d->stack)->vbot);
}

void setDeltaStackVBot(delta_state *d, void *v)
{
    EVV_AT(delta_stack *, d->stack)->vbot = EVV_REF(v);
}

/* Undo the topmost record. What it cost depends on what kind it was, and a
   kind outside the eight the original knows about leaves the size it moves by
   uninitialised, so callers never produce one. */
void *popDeltaStackTop(delta_state *d)
{
    delta_frame *slot = (delta_frame *)EVV_AT(uint8_t *, EVV_AT(delta_stack *, d->stack)->top);
    int32_t kind = slot->kind;
    int32_t size = 0;

    switch (kind) {
    case 0: size = EVV_AT(delta_stack *, d->stack)->ca_size;  break;
    case 1: size = EVV_AT(delta_stack *, d->stack)->size_b0;  break;
    case 2: size = (((slot->length - 1) & ~1) | 1) + EVV_AT(delta_stack *, d->stack)->size_ac + 1; break;
    case 3: size = EVV_AT(delta_stack *, d->stack)->ca_size;  break;
    case 4: size = EVV_AT(delta_stack *, d->stack)->boa_size; break;
    case 5: size = EVV_AT(delta_stack *, d->stack)->size_b8;  break;
    case 6: size = EVV_AT(delta_stack *, d->stack)->boa_size; break;
    case 7: size = EVV_AT(delta_stack *, d->stack)->size_a8;  break;
    default: return slot;
    }

    EVV_AT(delta_stack *, d->stack)->top += size;
    EVV_AT(delta_stack *, d->stack)->limit += size;
    return slot;
}

/* Unwind until something says stop.
 *
 * The same eight kinds popDeltaStackTop knows the size of, and what each
 * one means on the way back: a context record answers with the tag it was
 * pushed under, a saved scan position is put back, a copied span is copied
 * back, an alternative marker moves the count of how deep we are, and the
 * floor marker restores how far an unwind may go.
 *
 * The count is what the caller is looking for. Above nought it is walking
 * out of alternatives it does not want; at nought the next context record
 * is the one to answer with.
 */
int32_t vback(delta_state *d, int32_t depth)
{
    delta_frame *slot;
    int32_t      flag;
    int32_t      size;

    if (EVV_AT(delta_vars *, d->vars)->error_thrown)
        return -1;

    /* Told once to skip an unwind. The flag is spent rather than tested,
       and it is the answer that time. */
    flag = d->unknown_3c;
    if (flag != 0) {
        d->unknown_3c = 0;
        return flag;
    }

    EVV_WALK(w);

    for (;;) {
        slot = (delta_frame *)EVV_AT(uint8_t *, EVV_AT(delta_stack *, d->stack)->top);

        EVV_WALKED(d, w);

        switch ((int32_t)slot->kind) {
        case 0:
            size = EVV_AT(delta_stack *, d->stack)->ca_size;
            EVV_AT(delta_stack *, d->stack)->limit += size;
            EVV_AT(delta_stack *, d->stack)->top   += size;
            if (depth == 0)
                return slot->value;
            break;

        case 1:
            size = EVV_AT(delta_stack *, d->stack)->size_b0;
            EVV_AT(delta_stack *, d->stack)->limit += size;
            EVV_AT(delta_stack *, d->stack)->top   += size;
            memcpy(&EVV_AT(delta_vars *, d->vars)->scan_ptr, &slot->value, 8);
            break;

        case 2:
            size = (((slot->length - 1) & ~1) | 1) + EVV_AT(delta_stack *, d->stack)->size_ac + 1;
            EVV_AT(delta_stack *, d->stack)->limit += size;
            EVV_AT(delta_stack *, d->stack)->top   += size;
            memcpy((void *)(intptr_t)slot->value,
                   (char *)slot + EVV_AT(delta_stack *, d->stack)->size_ac,
                   (size_t)slot->length);
            break;

        case 3:
            size = EVV_AT(delta_stack *, d->stack)->ca_size;
            EVV_AT(delta_stack *, d->stack)->limit += size;
            EVV_AT(delta_stack *, d->stack)->top   += size;
            if (depth == 0 && vscanadv(d, 0, 1))
                return slot->value;
            break;

        case 4:
            size = EVV_AT(delta_stack *, d->stack)->boa_size;
            EVV_AT(delta_stack *, d->stack)->limit += size;
            EVV_AT(delta_stack *, d->stack)->top   += size;
            if (depth > 0)
                depth--;
            break;

        case 5:
            size = EVV_AT(delta_stack *, d->stack)->size_b8;
            EVV_AT(delta_stack *, d->stack)->limit += size;
            EVV_AT(delta_stack *, d->stack)->top   += size;
            setDeltaStackVBot(d, (void *)(intptr_t)slot->value);
            break;

        case 6:
            size = EVV_AT(delta_stack *, d->stack)->boa_size;
            EVV_AT(delta_stack *, d->stack)->limit += size;
            EVV_AT(delta_stack *, d->stack)->top   += size;
            depth++;
            break;

        case 7:
        default:
            return -1;
        }
    }
}

/* Is the character at this offset from the fence base one of the fenced set. */
int FENCED(delta_state *d, const int32_t *table, int8_t idx)
{
    return (table[EVV_AT(delta_vars *, d->vars)->fence_base + idx] & 2) != 0;
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
    delta_stack *s = EVV_AT(delta_stack *, d->stack);
    int32_t used;

    if (EVV_AT(delta_seg *, s->seg) == NULL)
        return;

    s->top = EVV_REF(to);
    used = (int32_t)(EVV_AT(delta_seg *, s->seg)->end - s->top);
    s->limit = EVV_REF(EVV_AT(uint8_t *, s->base) - used);
}

/* Unwind to whichever mark applies: a record of kind eight at the bottom
   means the rule wants the saved one instead. */
void clearDeltaStackBack(delta_state *d)
{
    if (*EVV_AT(uint8_t *, EVV_AT(delta_stack *, d->stack)->vbot) == 8)
        freeDeltaStackTo(d, EVV_AT(uint8_t *, EVV_AT(delta_vars *, d->vars)->back));
    else
        freeDeltaStackTo(d, EVV_AT(uint8_t *, EVV_AT(delta_stack *, d->stack)->vbot));
}

/* Open a test: remember what it is matching, clear anything a previous one
   left, and push the context record it will unwind to. */
void starttest(delta_state *d, int16_t tag)
{
    delta_frame *slot;

    EVV_AT(delta_vars *, d->vars)->test_tag = tag;
    clearDeltaStackBack(d);

    slot = bs_push(EVV_AT(delta_stack *, d->stack), EVV_AT(delta_stack *, d->stack)->ca_size);
    slot->kind = 0;
    slot->value = EVV_AT(delta_vars *, d->vars)->test_tag;

    EVV_AT(delta_vars *, d->vars)->testing = 1;
}

static void set3(delta_state *d, int32_t x, int32_t y)
{
    EVV_AT(delta_vars *, d->vars)->compared_equal = (int8_t)(x < y ? -1 : (x == y ? 0 : 1));
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
        EVV_AT(delta_vars *, d->vars)->compared_equal =
            absoluteSyncNumPtr(*(int32_t *)a->ptr)
            == absoluteSyncNumPtr(*(int32_t *)b->ptr) ? 0 : 1;
        break;

    default:
        if (b->kind != a->kind) {
            EVV_AT(delta_vars *, d->vars)->compared_equal = 1;
        } else {
            int32_t len = stmt_length(a->kind);

            /* The original keeps only the low byte of what memcmp returns,
               so the exact value matters and not just its sign. That was
               already true of IBM's builds; using memcmp keeps it so. */
            EVV_AT(delta_vars *, d->vars)->compared_equal =
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

/* Two more of the same, and one that answers what a statement type is
   called. CLRNONSEQ clears the bit its neighbours set; TVFLDS hands back
   what it was given, which is what the original does -- the fields of a
   timing statement start where the statement does, where an ordinary one's
   start eight bytes in. */
void CLRNONSEQ(delta_node *t)     { t->flags8 &= ~2; }

/* Whether the run between two nodes is anything but a plain sequence.
 *
 * It is, if some field other than the one being worked in has a mark at both
 * ends and the left one's mark does not lead straight to the right one --
 * which means something else is threaded through the run and an insert has
 * to be told so. */
int visnonseq(delta_state *d, uint8_t f, int32_t l, int32_t r)
{
    int32_t base = EVV_AT(delta_vars *, d->vars)->fence_base;
    int8_t  i;

    for (i = 0; i < d->nstmts; i++) {
        if (i == (int8_t)f)
            continue;
        if ((*(const int32_t *)(intptr_t)(l + (base + i) * 4) & 1) == 0)
            continue;
        if ((*(const int32_t *)(intptr_t)(r + (base + i) * 4) & 1) == 0)
            continue;
        if (VRSYNC(d, (const int32_t *)(intptr_t)l, i) == r)
            continue;

        return 1;
    }

    return 0;
}

/* Whether two marks may be joined. Anything may when the machine is not
   looking both ways, and a mark may always be joined with itself; what may
   not is the pair that are the two ends of the spine, in either order,
   since joining those would leave nothing to hold. */
int vmergable(delta_state *d, int32_t l, int32_t r)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);

    if (EVV_AT(delta_vars *, d->vars)->ctx_both == 0 || l == r)
        return 1;

    if (l == s->spine_l && r == s->spine_r)
        return 0;
    if (l == s->spine_r && r == s->spine_l)
        return 0;

    return 1;
}
void *TVFLDS(void *p)             { return p; }
const char *streamName(int8_t st) { return vstmtbl[st].name; }
void CLRALLNSQ(delta_node *t)     { t->link &= ~2; }

void bsclear(delta_state *d)
{
    clearDeltaStackBack(d);
}

/* Take the alternative marker off the stack, handing back where it was. */
void *bspop_boa(delta_state *d)
{
    void *slot = EVV_AT(uint8_t *, EVV_AT(delta_stack *, d->stack)->top);

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
    table[EVV_AT(delta_vars *, d->vars)->fence_base + idx] |= 2;
}

void UNSETFENCE(delta_state *d, int32_t *table, int8_t idx)
{
    table[EVV_AT(delta_vars *, d->vars)->fence_base + idx] &= ~2;
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
    return EVV_AT(delta_vars *, d->vars)->error_thrown;
}

/* Nothing is left once the record at the unwind point is the bottom marker. */
int emptyDeltaStack(delta_state *d)
{
    return EVV_AT(uint8_t *, EVV_AT(delta_vars *, d->vars)->back)[EVV_AT(delta_stack *, d->stack)->size_a8] == 8;
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
    delta_stack *s = EVV_AT(delta_stack *, d->stack);
    uint8_t *slot;

    s->names_depth = (int8_t)(s->names_depth + 1);
    slot = EVV_AT(uint8_t *, s->names) + (int32_t)s->names_depth * 8;

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

/* Collecting a frame.
 *
 * A generate statement comes in three parts -- the frame, the moment it
 * covers, and the parameters that go with it -- and the machine reads them
 * one at a time, each call putting its part into the cell and setting its
 * own bit. The statement's first byte says which part it is, and the byte
 * decides which of the two cells is written: its own marker means the one
 * being filled, anything else means the one already finished. That is the
 * original's arrangement and it is what lets a rule generate one frame while
 * the last is still being written out.
 *
 * None of the four is called by any rule in the nine languages IBM shipped,
 * which is why they were missing. A language's acoustic rules are what would
 * want them.
 */

/* Which cell this part belongs in. */
static delta_gencell *gen_cell(delta_state *d, uint8_t marker)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);

    if (*EVV_AT(const uint8_t *, v->gen_stmt) == marker)
        return &v->gen_now;
    return &v->gen_done;
}

int32_t vgen_frame(delta_state *d)
{
    delta_vars    *v = EVV_AT(delta_vars *, d->vars);
    delta_gencell *cell = gen_cell(d, 0xc3);
    delta_operand  dst;
    delta_operand  src;

    v->gen_dst.ptr = EVV_REF(&cell->value);
    v->gen_dst.kind = DK_SHORT2;
    v->gen_dst.flag = 0;

    dst.ptr = EVV_AT(void *, v->gen_dst.ptr);
    dst.kind = v->gen_dst.kind;
    dst.flag = v->gen_dst.flag;

    src.ptr = EVV_AT(void *, v->gen_src.ptr);
    src.kind = v->gen_src.kind;
    src.flag = v->gen_src.flag;

    vassign(d, &dst, &src);

    cell->flags |= 1;
    return 0;
}

int32_t vgen_time(delta_state *d)
{
    delta_vars    *v = EVV_AT(delta_vars *, d->vars);
    delta_gencell *cell = gen_cell(d, 0xc4);

    cell->time = v->gen_len;
    cell->flags |= 2;
    return 0;
}

/* The parameters are copied a byte at a time out of the statement into a
   buffer the cell keeps, which is made the first time round and reset every
   time after. The count is the statement's, and the bytes come from where
   the read has got to, which this steps as it goes. */
int32_t vgen_params(delta_state *d)
{
    delta_vars    *v = EVV_AT(delta_vars *, d->vars);
    delta_gencell *cell = gen_cell(d, 0xc5);
    int32_t        i;

    cell->nparams = v->gen_len;

    if ((cell->flags & 4) == 0)
        cell->params = EVV_REF(dynaBufNew(v->gen_nparams));

    dynaBufReset(EVV_AT(DynaBuf *, cell->params));

    for (i = 1; i <= (int32_t)v->gen_nparams; i++) {
        char c = *EVV_AT(const char *, v->gen_at);

        v->gen_at = EVV_REF(EVV_AT(const char *, v->gen_at) + 1);
        dynaBufAddChar(EVV_AT(DynaBuf *, cell->params), c, 0);
    }

    cell->flags |= 4;
    return 0;
}

/* And moving the finished frame across, which is refused unless all three
   parts have arrived. The buffer is copied character by character rather
   than by taking the other cell's, so the two go on owning their own. */
int32_t vgen_copy(delta_state *d)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);
    int32_t     i;

    if ((v->gen_now.flags & 1) == 0)
        return 0xf5;
    if ((v->gen_now.flags & 2) == 0)
        return 0xf5;
    if ((v->gen_now.flags & 4) == 0)
        return 0xf5;

    v->gen_done.value = v->gen_now.value;
    v->gen_done.time = v->gen_now.time;
    v->gen_done.nparams = v->gen_now.nparams;

    v->gen_len = (uint8_t)dynaBufLength(EVV_AT(DynaBuf *, v->gen_now.params));

    /* Where the buffer would be made if it had none. It never is: the
       original tests `!flags & 4' where it means `!(flags & 4)', and the
       first of those is nought or one and never has the bit set, so the
       compiler folded the whole branch away. The reset below therefore runs
       on whatever the cell already holds -- which on a first call is
       nothing. Nothing reaches this: no rule in the nine languages calls
       vgen_copy at all. Written the way the original runs rather than the
       way it reads, with the branch left here in words so that the next
       person to read both does not think this an oversight. */

    dynaBufReset(EVV_AT(DynaBuf *, v->gen_done.params));

    for (i = 0; i < (int32_t)v->gen_len; i++)
        dynaBufAddChar(EVV_AT(DynaBuf *, v->gen_done.params),
                       dynaBufChar(EVV_AT(DynaBuf *, v->gen_now.params), i),
                       0);

    v->gen_done.flags |= 1;
    v->gen_done.flags |= 2;
    v->gen_done.flags |= 4;

    return 0;
}

/* The same three parts again, said the way a rule says them.
 *
 * Where the vgen_ calls read the generate statement and work out which cell
 * to fill, these are told outright: the `gendef' three fill the cell being
 * collected and the `gencur' three the one already finished. Otherwise they
 * are the same three parts -- a frame assigned from a location, a moment
 * taken as a byte, and a run of parameter bytes copied into the cell's own
 * buffer -- and each sets its own bit as it arrives.
 *
 * The frame comes in as a long here rather than a short, which is the only
 * difference in what is written. */
static void gen_framedur(delta_state *d, delta_gencell *cell, delta_loc *loc)
{
    delta_operand dst;
    delta_operand src;

    dst.ptr = &cell->value;
    dst.kind = DK_LONG;
    dst.flag = 0;

    vinitloc_new(d, &src, loc);
    vassign(d, &dst, &src);

    cell->flags |= 1;
    reset_field(loc);
}

static void gen_timestm(delta_gencell *cell, uint8_t when)
{
    cell->time = when;
    cell->flags |= 2;
}

static void gen_params(delta_gencell *cell, uint8_t count, uint8_t n,
                       const uint8_t *str)
{
    int32_t i;

    cell->nparams = count;

    if ((cell->flags & 4) == 0)
        cell->params = EVV_REF(dynaBufNew(n));

    dynaBufReset(EVV_AT(DynaBuf *, cell->params));

    for (i = 1; i <= (int32_t)n; i++) {
        dynaBufAddChar(EVV_AT(DynaBuf *, cell->params), (char)*str, 0);
        str++;
    }

    cell->flags |= 4;
}

void gendef_framedur(delta_state *d, delta_loc *loc)
{
    gen_framedur(d, &EVV_AT(delta_vars *, d->vars)->gen_now, loc);
}

void gendef_timestm(delta_state *d, uint8_t when)
{
    gen_timestm(&EVV_AT(delta_vars *, d->vars)->gen_now, when);
}

void gendef_params(delta_state *d, uint8_t count, uint8_t n,
                   const uint8_t *str)
{
    gen_params(&EVV_AT(delta_vars *, d->vars)->gen_now, count, n, str);
}

void gencur_framedur(delta_state *d, delta_loc *loc)
{
    gen_framedur(d, &EVV_AT(delta_vars *, d->vars)->gen_done, loc);
}

void gencur_timestm(delta_state *d, uint8_t when)
{
    gen_timestm(&EVV_AT(delta_vars *, d->vars)->gen_done, when);
}

void gencur_params(delta_state *d, uint8_t count, uint8_t n,
                   const uint8_t *str)
{
    gen_params(&EVV_AT(delta_vars *, d->vars)->gen_done, count, n, str);
}

/* And the name a rule calls the copy by. */
int32_t gen_copy(delta_state *d)
{
    return vgen_copy(d);
}

/* The other three arithmetic operations, in the same shape as vadd: the left
   operand says which width the answer is written in, the right is widened or
   narrowed to meet it, and any other pair of types is left alone. They are
   here after the rest of the machine because no rule in the nine languages
   IBM shipped calls one; a rule of ours that does arithmetic wants them. */
void vsub(delta_state *d, const delta_operand *a, const delta_operand *b)
{
    (void)d;

    if (a->kind == DK_LONG) {
        if (b->kind == DK_LONG)
            *(int32_t *)a->ptr = *(int32_t *)a->ptr - *(int32_t *)b->ptr;
        else if (b->kind == DK_SHORT2)
            *(int32_t *)a->ptr = *(int32_t *)a->ptr - *(int16_t *)b->ptr;
    } else if (a->kind == DK_SHORT2) {
        if (b->kind == DK_LONG)
            *(int16_t *)a->ptr =
                (int16_t)(*(int16_t *)a->ptr - *(int32_t *)b->ptr);
        else if (b->kind == DK_SHORT2)
            *(int16_t *)a->ptr =
                (int16_t)(*(int16_t *)a->ptr - *(int16_t *)b->ptr);
    }
}

void vmult(delta_state *d, const delta_operand *a, const delta_operand *b)
{
    (void)d;

    if (a->kind == DK_LONG) {
        if (b->kind == DK_LONG)
            *(int32_t *)a->ptr = *(int32_t *)a->ptr * *(int32_t *)b->ptr;
        else if (b->kind == DK_SHORT2)
            *(int32_t *)a->ptr = *(int16_t *)b->ptr * *(int32_t *)a->ptr;
    } else if (a->kind == DK_SHORT2) {
        if (b->kind == DK_LONG)
            *(int16_t *)a->ptr =
                (int16_t)(*(int16_t *)a->ptr * *(int32_t *)b->ptr);
        else if (b->kind == DK_SHORT2)
            *(int16_t *)a->ptr =
                (int16_t)(*(int16_t *)a->ptr * *(int16_t *)b->ptr);
    }
}

/* Where a division by nought goes. The original's is empty -- it announces
   nothing, stops nothing, and the division that follows it faults exactly as
   it would have without the call. It is kept because it is the one place a
   port could decide otherwise, and because vdiv calls it by name. */
void divzero(delta_state *d)
{
    (void)d;
}

void vdiv(delta_state *d, const delta_operand *a, const delta_operand *b)
{
    if (a->kind == DK_LONG) {
        if (b->kind == DK_LONG) {
            if (*(int32_t *)b->ptr == 0)
                divzero(d);
            *(int32_t *)a->ptr = *(int32_t *)a->ptr / *(int32_t *)b->ptr;
        } else if (b->kind == DK_SHORT2) {
            if (*(int16_t *)b->ptr == 0)
                divzero(d);
            *(int32_t *)a->ptr = *(int32_t *)a->ptr / *(int16_t *)b->ptr;
        }
    } else if (a->kind == DK_SHORT2) {
        if (b->kind == DK_LONG) {
            if (*(int32_t *)b->ptr == 0)
                divzero(d);
            *(int16_t *)a->ptr =
                (int16_t)(*(int16_t *)a->ptr / *(int32_t *)b->ptr);
        } else if (b->kind == DK_SHORT2) {
            if (*(int16_t *)b->ptr == 0)
                divzero(d);
            *(int16_t *)a->ptr =
                (int16_t)(*(int16_t *)a->ptr / *(int16_t *)b->ptr);
        }
    }
}

/* Negation is a multiplication by minus one in the original, which matters
   only at the one value where the two differ: the short case computes in
   thirty-two bits and keeps the low sixteen, so -32768 comes back as itself
   rather than saturating. */
void vnegate(delta_state *d, const delta_operand *a)
{
    (void)d;

    if (a->kind == DK_LONG)
        *(int32_t *)a->ptr = *(int32_t *)a->ptr * -1;
    else if (a->kind == DK_SHORT2)
        *(int16_t *)a->ptr = (int16_t)((int32_t)*(int16_t *)a->ptr * -1);
}

/* Whether two operands may be compared at all. The negative kinds have to
   match, except that the two integer widths are interchangeable with each
   other; a kind that is not negative is a statement type, and all that is
   asked of it is that the language declares it. The right operand is not
   looked at in that last case, which is the original's doing. */
int32_t vcompareTypeCheck(delta_state *d, const delta_operand *a,
                          const delta_operand *b)
{
    switch (a->kind) {
    case DK_UBYTE:
        return b->kind == DK_UBYTE;
    case DK_SHORT:
        return b->kind == DK_SHORT;
    case DK_LONG:
    case DK_SHORT2:
        return b->kind >= DK_SHORT2 && b->kind <= DK_LONG;
    case DK_SYNC:
        return b->kind == DK_SYNC;
    default:
        return a->kind >= 0 && a->kind < d->nstmts;
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
    int32_t p = t[EVV_AT(delta_vars *, d->vars)->fence_base + i] & ~3;

    if (p == 0)
        return p;
    if ((*(int32_t *)p & 2) != 0)
        return p;
    return *(int32_t *)(p + 4) & ~3;
}

/* Walk to the mark that carries a field, one link at a time.
 *
 * A position that already carries it is the answer; otherwise the walk
 * follows the sync links of another field -- the one named separately --
 * until it finds one that does. The two differ in which way they walk and
 * in nothing else. Only vgen calls them, which is why they arrive with it.
 */
int32_t gcql(delta_state *d, int32_t at, int8_t f, int8_t i)
{
    int32_t base = EVV_AT(delta_vars *, d->vars)->fence_base;

    while ((*(const int32_t *)(intptr_t)(at + (base + f) * 4) & 1) == 0)
        at = VLSYNC((const delta_node *)(intptr_t)at, i);

    return at;
}

int32_t gcqr(delta_state *d, int32_t at, int8_t f, int8_t i)
{
    int32_t base = EVV_AT(delta_vars *, d->vars)->fence_base;

    while ((*(const int32_t *)(intptr_t)(at + (base + f) * 4) & 1) == 0)
        at = VRSYNC(d, (const int32_t *)(intptr_t)at, i);

    return at;
}

/* A generate statement written out, which is the largest thing the machine
 * does and the only one that produces text rather than spine.
 *
 * The collected cell carries three things: the frame, whose value is how
 * long one step lasts; the field the moment is measured in, which arrives
 * as the cell's time; and the parameter bytes, which are a little program.
 * That program is a count of statement types, and for each one its number,
 * how many of its fields follow, and those field numbers.
 *
 * vgen walks the span the two registers mark, one frame at a time, and
 * writes a line for every frame: each field asked for, worked out at that
 * moment by val_expr2, with the frame's own duration put in at whichever
 * position of the line the cell names. A first pass over the program settles
 * where each statement type's value is to be read from, and those two ends
 * are kept in the stack's own arrays so that val_expr2 can find them; the
 * answers are cached across frames whenever neither end has moved, which is
 * what makes a long span affordable. checkInterrupt is asked between fields,
 * so a caller who has stopped listening stops the walk.
 *
 * Nothing in the nine languages IBM shipped generates frames, so no rule
 * reaches it. A language whose acoustic rules do would.
 *
 * Three things here are the original's and are kept rather than corrected.
 * The buffer is not given back on the two paths that fail before the working
 * table exists. The gap count is compared the wrong way round, so one to ten
 * gaps give up at once and the branch meant to let ten through can never be
 * taken, which makes the test after the loop unreachable as well. And the
 * fourth of the five arrays is allocated and never checked.
 */
int32_t vgen(delta_state *d, delta_tpos *l, delta_tpos *r,
             const delta_gencell *g, int32_t lf)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);
    int32_t      base = EVV_AT(delta_vars *, d->vars)->fence_base;
    DynaBuf     *buf;
    int32_t     *cache;
    const char  *p;
    char         line[20];
    int32_t      taken = 0, mode = 0, gaps = 0;
    int32_t      dur, left, first;
    int8_t       f, i, nstm, st;
    uint8_t      total = 0, nfld, k, j;

    if ((g->flags & 1) == 0)
        return 0;
    if ((g->flags & 2) == 0)
        return 0;
    if ((g->flags & 4) == 0)
        return 0;

    l->field  = (int8_t)g->time;
    f         = l->field;
    l->flags  = 2;
    l->offset = 0;

    buf = dynaBufNew(0x28);
    if (buf == 0)
        return 0;

    /* Five arrays a statement type wide, made once and kept on the stack:
       where each type's value is read from at the left and at the right,
       and three caches durcalc works in. */
    if (s->expr_l == 0) {
        s->expr_l      = EVV_REF(malloc((size_t)d->nstmts * 4));
        s->expr_r      = EVV_REF(malloc((size_t)d->nstmts * 4));
        s->dur_cache_b = EVV_REF(malloc((size_t)d->nstmts * 12));
        s->dur_cache_a = EVV_REF(malloc((size_t)d->nstmts * 12));
        s->dur_cache_c = EVV_REF(malloc((size_t)d->nstmts * 12));

        if (s->expr_l == 0 || s->expr_r == 0
            || s->dur_cache_b == 0 || s->dur_cache_c == 0) {
            if (s->expr_l != 0)
                free(EVV_AT(void *, s->expr_l));
            if (s->expr_r != 0)
                free(EVV_AT(void *, s->expr_r));
            if (s->dur_cache_b != 0)
                free(EVV_AT(void *, s->dur_cache_b));
            if (s->dur_cache_a != 0)
                free(EVV_AT(void *, s->dur_cache_a));
            if (s->dur_cache_c != 0)
                free(EVV_AT(void *, s->dur_cache_c));
            return 0;
        }
    }

    for (i = 0; i < (int32_t)d->nstmts; i++) {
        int32_t *a = EVV_AT(int32_t *, s->dur_cache_a);
        int32_t *b = EVV_AT(int32_t *, s->dur_cache_b);
        int32_t *c = EVV_AT(int32_t *, s->dur_cache_c);

        EVV_AT(int32_t *, s->expr_r)[i] = 0;
        EVV_AT(int32_t *, s->expr_l)[i] = 0;

        b[i * 3] = b[i * 3 + 1] = a[i * 3] = a[i * 3 + 1]
                 = c[i * 3] = c[i * 3 + 1] = s->spine_l;

        c[i * 3 + 2] = 0;
        a[i * 3 + 2] = 0;
        b[i * 3 + 2] = 0;
    }

    /* First pass: for every statement type the program names, find the two
       marks its value is to be read between, and count how many numbers a
       line will hold. */
    p    = dynaBufContents(EVV_AT(DynaBuf *, g->params));
    nstm = (int8_t)*p++;

    for (i = 0; i < nstm; i++) {
        int32_t at, at2, next, walk;

        st = (int8_t)*p++;

        at = vgetsc(d, 1, 1, l->node, (uint8_t)st);
        if (at != 0) {
            next = ((const int32_t *)(intptr_t)at)[3 + st] & ~3;
            EVV_WALK(wl);

            while (at != 0 && next != 0
                   && (*(const int32_t *)(intptr_t)next & 2) != 0) {
                EVV_WALKED(d, wl);
                at   = next;
                next = ((const int32_t *)(intptr_t)at)[3 + st] & ~3;
            }
        }

        at2 = vgetsc(d, 0, 1, r->node, (uint8_t)st);
        if (at2 != 0) {
            next = ((const int32_t *)(intptr_t)at2)[base + st] & ~3;
            EVV_WALK(wr);

            while (at2 != 0 && next != 0
                   && (*(const int32_t *)(intptr_t)next & 2) != 0) {
                EVV_WALKED(d, wr);
                at2  = next;
                next = ((const int32_t *)(intptr_t)at2)[base + st] & ~3;
            }
        }

        /* Every mark between the two has to carry the field being timed. */
        walk = at;
        EVV_WALK(ww);

        while (walk != 0) {
            EVV_WALKED(d, ww);

            if ((((const int32_t *)(intptr_t)walk)[base + f] & 1) == 0) {
                gaps++;
                if (gaps <= 10) {
                    dynaBufDelete(buf);
                    return 0;
                }
            }

            walk = VRSYNC(d, (const int32_t *)(intptr_t)walk, st);
            if (walk == 0 || walk == at2)
                break;
        }

        if (gaps == 0) {
            EVV_AT(int32_t *, s->expr_l)[st] = gcql(d, l->node, st, f);
            EVV_AT(int32_t *, s->expr_r)[st] = gcqr(d, l->node, st, f);
        }

        nfld = (uint8_t)*p++;
        for (j = 0; j < nfld; j++) {
            if (total == g->nparams)
                total++;
            total++;
            p++;
        }
    }

    if (gaps != 0) {
        dynaBufDelete(buf);
        return 0;
    }

    cache = malloc((size_t)(d->nstmts * 4) * total);
    if (cache == 0) {
        dynaBufDelete(buf);
        return 0;
    }

    dur   = vdur(d, l, r, (uint8_t)g->time);
    first = 1;

    for (left = dur; left > 0; left -= g->value) {
        int32_t skip = 0;
        int32_t stop = l->node;

        if (!first && mode == 3)
            stop = (int32_t)(intptr_t)
                   lmost(d, f, (delta_node *)(intptr_t)stop);

        mode = vnormalize(d, l);

        switch (mode) {
        case 2:
            if (l->offset > taken)
                skip = 1;
            break;

        case 3:
        case 4:
            break;

        default:
            dynaBufDelete(buf);
            free(cache);
            return 0;
        }

        taken = left < g->value ? left : g->value;

        buf  = dynaBufReset(buf);
        p    = dynaBufContents(EVV_AT(DynaBuf *, g->params));
        k    = 0;
        nstm = (int8_t)*p++;

        for (i = 0; i < nstm; i++) {
            int32_t saved_l, saved_r, at, at2, found, same;

            st   = (int8_t)*p++;
            nfld = (uint8_t)*p++;

            saved_l = EVV_AT(int32_t *, s->expr_l)[st];
            saved_r = EVV_AT(int32_t *, s->expr_r)[st];

            at  = l->node;
            at2 = at;

            switch (mode) {
            case 3:
                at2 = firstdefd(d, f, at, (uint8_t)st, 0);
                at  = firstdefd(d, f, (int32_t)(intptr_t)
                                lmost(d, f, (delta_node *)(intptr_t)at),
                                (uint8_t)st, 1);
                /* fall through */

            case 4:
                if ((((const int32_t *)(intptr_t)at)[base + st] & 1) != 0) {
                    EVV_AT(int32_t *, s->expr_r)[st] = at2;
                    EVV_AT(int32_t *, s->expr_l)[st] = at;
                    break;
                }
                /* fall through */

            case 2:
                found = 0;
                if (!skip) {
                    EVV_WALK(wf);

                    for (;;) {
                        EVV_WALKED(d, wf);

                        if ((((const int32_t *)(intptr_t)at)[base + st] & 1)
                                != 0) {
                            found = at;
                            break;
                        }
                        if (at == stop)
                            break;
                        at = VLSYNC((const delta_node *)(intptr_t)at, f);
                    }
                }

                if (found != 0) {
                    EVV_AT(int32_t *, s->expr_l)[st] = found;
                    EVV_AT(int32_t *, s->expr_r)[st] =
                        VRSYNC(d, (const int32_t *)(intptr_t)found, st);
                }
                break;

            default:
                dynaBufDelete(buf);
                free(cache);
                return 0;
            }

            /* Neither end moved since the last frame, so last frame's
               answers still stand. */
            same = mode == 2 && !first
                && saved_l == EVV_AT(int32_t *, s->expr_l)[st]
                && saved_r == EVV_AT(int32_t *, s->expr_r)[st];

            for (j = 0; j < nfld; j++) {
                int32_t sel, v;
                uint8_t fld;

                if (k == g->nparams) {
                    sprintf(line, "%d ", taken);
                    dynaBufAddString(buf, line, 0);
                    k++;
                }

                sel = left == dur ? vstmtbl[g->time].gen_sel : 0;
                fld = (uint8_t)*p++;

                if (same && cache[k] != (int32_t)0x80000000) {
                    v = cache[k];
                } else {
                    int32_t worked = 0;

                    v = val_expr2(d, l, st, fld, sel, mode, &worked);
                    cache[k] = worked ? (int32_t)0x80000000 : v;
                }

                if (v == (int32_t)0x80000001) {
                    dynaBufDelete(buf);
                    free(cache);
                    return 0;
                }

                sprintf(line, "%d ", v);
                dynaBufAddString(buf, line, 0);
                k++;

                if (checkInterrupt(d))
                    break;
            }

            if (checkInterrupt(d))
                break;
        }

        if (checkInterrupt(d))
            break;

        if (k == g->nparams) {
            sprintf(line, "%d ", taken);
            dynaBufAddString(buf, line, 0);
            k++;
        }

        dynaBufAddChar(buf, '\n', 0);
        dynaBufAddChar(buf, 0, 0);
        vf_puts(d, lf, dynaBufContents(buf), 1);

        l->flags  = 2;
        l->offset = l->offset + g->value;
        first     = 0;
    }

    dynaBufDelete(buf);
    free(cache);
    return 1;
}

/* The two names a rule calls it by, which differ only in where the count of
   numbers a line holds comes from and in what a failure costs. Both check
   first that the range the two registers mark is one that may be printed. */
int32_t vgenerate(delta_state *d)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);

    if (!vprt_range(d, &d->lpta, &d->rpta)
        || !vgen(d, &d->lpta, &d->rpta, &v->gen_done, v->gen_len))
        return 0xf5;

    return 0;
}

void generate(delta_state *d, int32_t lf)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);

    if (!vprt_range(d, &d->lpta, &d->rpta)
        || !vgen(d, &d->lpta, &d->rpta, &v->gen_done, lf))
        forceErrorBacktrack(d);
}

/* The check that a spine's context marks are consistent.
 *
 * This is the Delta debugger's, not the engine's: `vredoctxt' ends by saying
 * "The delta is correct." on the command layer's own stream. Nothing in this
 * engine calls any of the four, and the display they belong to is not here --
 * src/delta_trace.c says why. They are written because the machine is being
 * finished rather than because something wants them.
 *
 * vctxtinit takes the six tables the check works in, four of a word per
 * statement type and two of a byte, and gives them all back if any one of
 * them cannot be had. vclrctxt walks every field's chain from the spine's
 * right-hand end and clears the pointer out of every link whose field is not
 * marked, keeping the two flag bits and remembering that it did. mapsyncs
 * numbers the syncs it can reach, marking each so it is not counted twice.
 */
int vctxtinit(delta_state *d)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);

    if (d->nstmts == 0)
        return 1;

    s->ctxt_a = EVV_REF(malloc((size_t)d->nstmts * 4));
    s->ctxt_b = EVV_REF(malloc((size_t)d->nstmts * 4));
    s->ctxt_c = EVV_REF(malloc((size_t)d->nstmts * 4));
    s->ctxt_d = EVV_REF(malloc((size_t)d->nstmts * 4));
    s->ctxt_e = EVV_REF(malloc((size_t)d->nstmts));
    s->ctxt_f = EVV_REF(malloc((size_t)d->nstmts));

    if (s->ctxt_a != 0 && s->ctxt_b != 0 && s->ctxt_c != 0
        && s->ctxt_d != 0 && s->ctxt_e != 0 && s->ctxt_f != 0)
        return 1;

    if (s->ctxt_a != 0)
        free(EVV_AT(void *, s->ctxt_a));
    if (s->ctxt_b != 0)
        free(EVV_AT(void *, s->ctxt_b));
    if (s->ctxt_c != 0)
        free(EVV_AT(void *, s->ctxt_c));
    if (s->ctxt_d != 0)
        free(EVV_AT(void *, s->ctxt_d));
    if (s->ctxt_e != 0)
        free(EVV_AT(void *, s->ctxt_e));
    if (s->ctxt_f != 0)
        free(EVV_AT(void *, s->ctxt_f));

    return 0;
}


/* What the check looks at before the clearing: a node whose two link words
 * carry anything but their own two flag bits is inconsistent, and the first
 * one found stops the walk. Asking whether the caller has interrupted is
 * what lets a long spine be given up on; an interrupt turns the report off
 * and the walk goes on clearing.
 *
 * Then the clearing itself, which is the same five bits every time: the
 * sync bit and the mark bit out of both words, and whatever is above the
 * bottom two out of both.
 */
int chksyncsflags(delta_state *d)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);
    int32_t      base = EVV_AT(delta_vars *, d->vars)->fence_base;
    int8_t       i;

    for (i = 0; i < d->nstmts; i++) {
        int32_t t = s->spine_r;

        while (t != 0) {
            int32_t *node = (int32_t *)(intptr_t)t;

            if (s->ctxt_arg != 0
                && ((node[base - 3] & 2) != 0
                    || (node[0] & 1) != 0
                    || (node[base - 3] & 1) != 0
                    || (node[0] & ~3) != 0
                    || (node[base - 3] & ~3) != 0)) {
                if (checkInterrupt(d))
                    s->ctxt_arg = 0;

                if (s->ctxt_arg != 0) {
                    s->ctxt_cleared = 1;
                    return 0;
                }
            }

            node[base - 3] &= ~2;
            node[0] &= ~1;
            node[base - 3] &= ~1;
            node[0] &= 3;
            node[base - 3] &= 3;

            t = VLSYNC((const delta_node *)(intptr_t)t, i);
        }
    }

    return 1;
}
int vclrctxt(delta_state *d, int32_t unused)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);
    int32_t      base = EVV_AT(delta_vars *, d->vars)->fence_base;
    int8_t       i;

    (void)unused;

    for (i = 0; i < d->nstmts; i++) {
        int32_t t = s->spine_r;

        while (t != 0) {
            int8_t j;

            for (j = 0; j < d->nstmts; j++) {
                int32_t *node = (int32_t *)(intptr_t)t;

                if ((node[base + j] & 1) != 0)
                    continue;

                node[3 + j] &= 3;
                node[base + j] &= 3;
                s->ctxt_cleared = 1;
            }

            t = VLSYNC((const delta_node *)(intptr_t)t, i);
        }
    }

    return 1;
}

void mapsyncs(delta_state *d, int32_t t)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);
    int32_t      base = EVV_AT(delta_vars *, d->vars)->fence_base;
    int32_t      n = absoluteSyncNum(d, (uint8_t *)(intptr_t)t);
    int8_t       i;

    /* The word three before the fenced fields begin, which is what the
       original computes; marking it is how a sync already counted is known
       from one that is not. */
    ((int32_t *)(intptr_t)t)[base - 3] |= 2;

    EVV_AT(int16_t *, s->sync_map)[n] = (int16_t)s->sync_next;
    s->sync_next++;

    for (i = 0; i < d->nstmts; i++) {
        int32_t next;

        if ((((const int32_t *)(intptr_t)t)[base + i] & 1) == 0)
            continue;

        next = VRSYNC(d, (const int32_t *)(intptr_t)t, i);
        if (next == 0)
            continue;
        if ((((const int32_t *)(intptr_t)next)[base - 3] & 2) != 0)
            continue;

        mapsyncs(d, next);
    }
}

int vredoctxt(delta_state *d, int32_t arg)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);

    s->ctxt_done = 0;
    s->ctxt_cleared = 0;
    s->ctxt_arg = arg;

    chksyncsflags(d);

    if (!vclrctxt(d, arg))
        return 0;

    if (arg != 0 && s->ctxt_cleared == 0)
        vf_printf(d, *(const int8_t *)((const char *)
                      EVV_AT(const void *, d->logio) + 4),
                  1, "The delta is correct.\n");

    s->ctxt_done = 1;
    return 1;
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
    delta_vars *v = EVV_AT(delta_vars *, d->vars);

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
    delta_vars *v = EVV_AT(delta_vars *, d->vars);

    if (v->ptr_count <= 0)
        return 0;

    v->ptr_count = v->active_record;
    v->ptr_count--;
    v->active_record = v->ptr_stack[v->ptr_count];
    return 1;
}

void throwDeltaErrorNow(delta_state *d)
{
    EVV_AT(delta_vars *, d->vars)->error_thrown = 1;
}

/* Take the top of the name stack, handing back where it sits rather than
   copying it out. Only the four sized types get a pointer. */
void vnspop(delta_state *d, delta_operand *out)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);
    uint8_t *slot = EVV_AT(uint8_t *, s->names) + (int32_t)s->names_depth * 8;

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
    delta_stack *s = EVV_AT(delta_stack *, d->stack);
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
    slot = EVV_AT(uint8_t *, s->top);
    s->limit -= step;

    slot[0] = 2;
    *(int16_t *)(slot + 2) = v->kind;
    *(int32_t *)(slot + 8) = size;
    *(int32_t *)(slot + 4) = EVV_REF(v->ptr);

    memcpy(slot + s->size_ac, v->ptr, (size_t)(pad + 1));
}

/* Bumped whenever the spine is relinked. */
int32_t spine_changed;

/* Unlink a node from the spine, keeping the tag bits that ride in the low two
   bits of each link. */
void DELSPINE(delta_state *d, delta_node *t)
{
    int32_t base = EVV_AT(delta_vars *, d->vars)->fence_base;
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
    delta_vars *v = EVV_AT(delta_vars *, d->vars);
    int32_t i = 0;

    if (v->fence_count != 0 && usefence != 0 && v->scan_held == 0) {
        for (; i < v->fence_count; i++) {
            uint8_t ch = EVV_AT(uint8_t *, d->fence_chars)[i];

            if ((*(int32_t *)(intptr_t)(cur + (v->fence_base + ch) * 4) & 1)
                != 0) {
                *at = i;
                return 1;
            }

            if (FENCED(d, (const int32_t *)(intptr_t)cur,
                       (int8_t)EVV_AT(uint8_t *, d->fence_chars)[i])
                && field != EVV_AT(uint8_t *, d->fence_chars)[i]
                && EVV_AT(uint8_t *, d->fence_marks)[i] == 0) {
                *at = i;
                return 1;
            }

            EVV_AT(uint8_t *, d->fence_marks)[i] = 0;
        }
    }

    *at = i;
    return 0;
}

/* Where the scan's next node is, in whichever direction is set. */
static int32_t scan_step(delta_state *d, int32_t cur, int32_t field)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);

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
    delta_vars *v = EVV_AT(delta_vars *, d->vars);
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
        EVV_AT(uint8_t *, d->fence_marks)[i] = 0;

    return 1;
}

/* A node's context link, the second of the two trailer words that sit between
   its left and right sync arrays. The first is the right-hand spine link. */
static int32_t *clink(delta_state *d, int32_t p)
{
    return (int32_t *)((char *)(intptr_t)p + EVV_AT(delta_vars *, d->vars)->fence_base * 4 - 4);
}

/* The right-hand spine link has no fixed offset: it sits one word before the
   sync array's end, so how many fields the language declares decides where. */
static int32_t *rlink(delta_state *d, int32_t p)
{
    return (int32_t *)((char *)(intptr_t)p + EVV_AT(delta_vars *, d->vars)->fence_base * 4 - 8);
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
    int32_t *r = rlink(d, EVV_REF(t));

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
    EVV_LAND_JUMP(EVV_AT(void *, EVV_AT(delta_vars *, d->vars)->err_jmp), 1);
}

void push_ptr_init(delta_state *d, delta_loc *p)
{
    p->value = 0;
    p->kind = DK_SYNC;
    push_ptr(d, EVV_REF(p));
}

/* One position becomes another everywhere the machine has written it down.
   Two places hold one: a word variable, which holds a position as its whole
   value, and the pointers a rule has pushed, each of which is a location
   whose value word is the position. The pushed ones are walked a frame at a
   time -- from the active record up to the top, then back to the frame below
   it, whose extent the entry under each record says -- so that a rule's own
   pushes and its callers' are all seen.

   Nothing in this tree calls it. It is the save layer's, which reads a
   machine back from a file: every position in it is a pointer that will land
   somewhere else next time, so each has to be told where it went. */
void set_saved_ptrs(delta_state *d, int32_t was, int32_t now)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);
    int32_t i, at, end;

    for (i = 0; i < d->nword; i++) {
        int32_t *cell = EVV_AT(int32_t **, d->word)[i];

        if (*cell == was)
            *cell = now;
    }

    at  = v->active_record;
    end = v->ptr_count;

    while (end > 0) {
        for (i = at; i < end; i++) {
            delta_loc *p = EVV_AT(delta_loc *, v->ptr_stack[i]);

            if (p->value == was)
                p->value = now;
        }

        end = at - 2;
        at  = v->ptr_stack[at - 1];
    }
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
    delta_vars *v = EVV_AT(delta_vars *, d->vars);

    v->scan_ptr = 0;
    v->scan_field = 0;
    v->scan_rev = 1;
    v->scan_held = 1;
}

/* Follow a field's sync chain leftward as far as it keeps landing on syncs. */
delta_node *vmovel(delta_state *d, delta_node *t, uint8_t f)
{
    EVV_WALK(w);

    for (;;) {
        int32_t next = t->syncs[f] & ~3;

        EVV_WALKED(d, w);

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
    EVV_WALK(w);

    for (;;) {
        int32_t next = t[EVV_AT(delta_vars *, d->vars)->fence_base + f] & ~3;

        EVV_WALKED(d, w);

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
    *r = (*r & 3) | EVV_REF(n);

    t->link = (t->link & 3) | EVV_REF(n);

    r = rlink(d, EVV_REF(n));
    *r = (*r & 3) | EVV_REF(t);

    spine_changed++;
}

void INSSPINER(delta_state *d, delta_node *n, delta_node *t)
{
    int32_t old = *rlink(d, EVV_REF(t)) & ~3;
    int32_t *r;

    r = rlink(d, EVV_REF(n));
    *r = (*r & 3) | old;

    ((delta_node *)(intptr_t)old)->link =
        (((delta_node *)(intptr_t)old)->link & 3) | EVV_REF(n);

    r = rlink(d, EVV_REF(t));
    *r = (*r & 3) | EVV_REF(n);

    n->link = (n->link & 3) | EVV_REF(t);

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

    EVV_WALK(w);

    (void)d;

    for (;;) {
        EVV_WALKED(d, w);

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
    int32_t next = t[EVV_AT(delta_vars *, d->vars)->fence_base + f] & ~3;
    int32_t keep = 0;
    EVV_WALK(w);

    for (;;) {
        EVV_WALKED(d, w);

        if (next != 0 && (*(int32_t *)(intptr_t)next & 2) != 0) {
            t = (int32_t *)(intptr_t)next;
            next = t[EVV_AT(delta_vars *, d->vars)->fence_base + f] & ~3;
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
    delta_vars *v = EVV_AT(delta_vars *, d->vars);
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

    EVV_WALK(w);

    while (*(int32_t *)(intptr_t)p & 2) {
        EVV_WALKED(d, w);

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
    EVV_WALK(w);

    for (;;) {
        EVV_WALKED(d, w);

        if ((t[EVV_AT(delta_vars *, d->vars)->fence_base + f] & 1) != 0
            && !NONSEQ((const delta_node *)t))
            return t;

        if (back != 0)
            t = (int32_t *)(intptr_t)(*(int32_t *)((char *)t + 4) & ~3);
        else
            t = (int32_t *)(intptr_t)
                (*(int32_t *)((char *)t + EVV_AT(delta_vars *, d->vars)->fence_base * 4 - 8) & ~3);
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

    while (EVV_AT(int8_t *, EVV_AT(delta_stack *, d->stack)->nsq_fields)[i] > -1) {
        if ((t[EVV_AT(delta_vars *, d->vars)->fence_base + EVV_AT(int8_t *, EVV_AT(delta_stack *, d->stack)->nsq_fields)[i]] & 1) != 0) {
            count++;
            all = 1;
        }
        i++;
    }

    for (i = (int32_t)d->nstmts - 1; i >= 0; i--) {
        if ((t[EVV_AT(delta_vars *, d->vars)->fence_base + i] & 1) == 0)
            continue;

        if (EVV_AT(int8_t *, EVV_AT(delta_vars *, d->vars)->nsq_marks)[i] == 0) {
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
    EVV_AT(delta_vars *, d->vars)->test_tag = tag;
    clearDeltaStackBack(d);
    EVV_AT(delta_vars *, d->vars)->testing = 0;
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
    delta_vars *v = EVV_AT(delta_vars *, d->vars);
    int32_t p = v->scan_ptr;
    const uint8_t *q;
    EVV_WALK(w);

    for (;;) {
        EVV_WALKED(d, w);

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
    delta_vars *v = EVV_AT(delta_vars *, d->vars);
    int32_t cur = v->scan_ptr;
    int32_t field = v->scan_field;
    EVV_WALK(w);

    for (;;) {
        int32_t next, i;

        EVV_WALKED(d, w);

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
            EVV_AT(uint8_t *, d->fence_marks)[i] = 0;

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
    delta_vars *v = EVV_AT(delta_vars *, d->vars);
    int32_t cur = v->scan_ptr;
    int32_t field = v->scan_field;
    EVV_WALK(w);

    for (;;) {
        int32_t next, i;

        EVV_WALKED(d, w);

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
            EVV_AT(uint8_t *, d->fence_marks)[i] = 0;

        if (next == target)
            return 1;
    }
}

/* Walk a run of statements, stopping at the first that carries one of the
   fields the node behind the start declares. */
void seqscan(delta_state *d, delta_seqctl *c)
{
    int32_t base = EVV_AT(delta_vars *, d->vars)->fence_base;
    int32_t back = c->kind == 1;
    int32_t t = c->start;
    int32_t peer;
    /* The original's own array, sized by what its frame gave it. */
    uint8_t fields[104];
    uint8_t n = 0;
    uint8_t i;
    EVV_WALK(w);

    c->cur = c->start;

    if (back)
        peer = *(int32_t *)(intptr_t)(t + 4) & ~3;
    else
        peer = *(int32_t *)(intptr_t)(t + base * 4 - 8) & ~3;

    for (i = 0; i < d->nstmts; i++)
        if ((*(int32_t *)(intptr_t)(peer + (base + i) * 4) & 1) != 0)
            fields[n++] = i;

    for (;;) {
        EVV_WALKED(d, w);

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

    if (EVV_AT(delta_vars *, d->vars)->testing)
        save_var(d, dst);

    vinitloc_new(d, &dv, dst);
    vinitloc_new(d, &sv, src);
    vassign(d, &dv, &sv);

    EVV_AT(delta_vars *, d->vars)->loop_tag = loop;
    EVV_AT(delta_vars *, d->vars)->test_tag = tag;
    clearDeltaStackBack(d);
    EVV_AT(delta_stack *, d->stack)->unknown_9c = 0;

    reset_field(dst);
    reset_field((delta_loc *)(intptr_t)src);
    return 2;
}

/* And the same body under a second name, which is what the original has:
   for_cont_from and forall_cont_from are the same bytes. */
int for_cont_from(delta_state *d, int16_t tag, int16_t loop, int32_t unused,
                  delta_loc *dst, const delta_loc *src)
{
    return forall_cont_from(d, tag, loop, unused, dst, src);
}

/* A context record naming what is being tried, then a copy of where the scan
   had got to. Anything that may have to be unwound pushes this pair. */
static void push_ca_and_scan(delta_state *d, int16_t tag)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);
    uint8_t *ca;
    uint8_t *save;

    s->top -= s->ca_size;
    ca = EVV_AT(uint8_t *, s->top);
    s->limit -= s->ca_size;
    ca[0] = 3;
    *(int32_t *)(ca + 4) = tag;

    s->top -= s->size_b0;
    save = EVV_AT(uint8_t *, s->top);
    s->limit -= s->size_b0;
    save[0] = 1;
    memcpy(save + 4, &EVV_AT(delta_vars *, d->vars)->scan_ptr, 8);
}

/* Remember where the scan is, both in the caller's variable and on the
   backtracking stack, so an unwind can put it back. */
void savescptr(delta_state *d, int16_t tag, delta_loc *v)
{

    if (EVV_AT(delta_vars *, d->vars)->testing)
        save_var(d, v);

    v->value = EVV_AT(delta_vars *, d->vars)->scan_ptr;
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
        if (!push_ptr(d, EVV_REF(out)))
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
    delta_vars *v = EVV_AT(delta_vars *, d->vars);
    int32_t ok = 0;
    int32_t i;

    EVV_WALK(w);

    while (ok == 0) {
        EVV_WALKED(d, w);
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
        EVV_AT(uint8_t *, d->fence_marks)[EVV_AT(uint8_t *, d->fence_index)[list[i]]] = 1;

    return 0;
}

/* Where the scan's field points from a given node, which the string tests
   need before they have moved the scan. */
static int32_t scan_peek(delta_state *d)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);

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
            if (EVV_AT(delta_vars *, d->vars)->compared_equal != 0)
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
                if (EVV_AT(delta_vars *, d->vars)->compared_equal != 0)
                    return 1;
            }

            if (!vscanadv(d, 1, 1))
                return 1;
        }
    }

    return 0;
}

/* The same test where a token in the string is wider than a byte.
 *
 * A record holds one code per character of the alphabet its statement type
 * declares, and an alphabet larger than 256 does not fit in a byte, so the
 * string a rule carries spells each code across two bytes or four. The top
 * bit of the first byte is the sign and the rest is the value, most
 * significant part first, which is the one place in the machine where a
 * number is not laid down the way the host lays one down.
 *
 * Neither is called by any rule in the nine languages IBM shipped, all of
 * whose alphabets fit in a byte. A language of ours with a larger one would
 * want them, and so would a rule comparing against a field that is not a
 * character at all.
 *
 * Everything else is test_string_s: peek at the scan, skip a sync, compare
 * the code against what the record holds, and advance. */
int test_string_l(delta_state *d, uint8_t st, uint8_t n, const uint8_t *str)
{
    const delta_stmt *e = &vstmtbl[st];
    const uint8_t *p = str;
    const uint8_t *end = str + n;
    int16_t value = 0;
    delta_operand a, b;

    a.kind = DK_SHORT;
    a.ptr = &value;
    a.flag = e->fields[0].flag;
    b.kind = e->fields[0].kind;
    b.flag = e->fields[0].flag;

    while (p < end) {
        int32_t node = scan_peek(d);

        if (node == 0)
            return 1;

        if ((*(int32_t *)(intptr_t)node & 2) == 0) {
            value = (int16_t)(((p[0] & 0x7f) << 8) | p[1]);
            if ((p[0] & 0x80) != 0)
                value = (int16_t)(value * -1);
            p += 2;

            b.ptr = e->get[0](TFLDS((void *)(intptr_t)node));
            vcompare(d, &a, &b);
            if (EVV_AT(delta_vars *, d->vars)->compared_equal != 0)
                return 1;
        }

        if (!vscanadv(d, 1, 1))
            return 1;
    }

    return 0;
}

int test_string_lng(delta_state *d, uint8_t st, uint8_t n, const uint8_t *str)
{
    const delta_stmt *e = &vstmtbl[st];
    const uint8_t *p = str;
    const uint8_t *end = str + n;
    int32_t value = 0;
    delta_operand a, b;

    a.kind = DK_LONG;
    a.ptr = &value;
    a.flag = e->fields[0].flag;
    b.kind = e->fields[0].kind;
    b.flag = e->fields[0].flag;

    while (p < end) {
        int32_t node = scan_peek(d);

        if (node == 0)
            return 1;

        if ((*(int32_t *)(intptr_t)node & 2) == 0) {
            value = (int32_t)(((uint32_t)(p[0] & 0x7f) << 24)
                              | ((uint32_t)p[1] << 16)
                              | ((uint32_t)p[2] << 8)
                              | (uint32_t)p[3]);
            if ((p[0] & 0x80) != 0)
                value = value * -1;
            p += 4;

            b.ptr = e->get[0](TFLDS((void *)(intptr_t)node));
            vcompare(d, &a, &b);
            if (EVV_AT(delta_vars *, d->vars)->compared_equal != 0)
                return 1;
        }

        if (!vscanadv(d, 1, 1))
            return 1;
    }

    return 0;
}

/* The string test that carries its own width.
 *
 * Here the first byte of the string is a marker saying how wide the tokens
 * after it are: 199 a byte apiece, 202 two and 201 four, the wide ones sign
 * first as in the two forms above. A length of nothing is not a comparison
 * at all but a step -- the scan moves over one token and the answer is
 * whether it could.
 *
 * Marker 200 is a defect, and it is worth saying out loud because it cannot
 * be reproduced. Its head takes the two-byte cell for the operand and its
 * body decodes four bytes into the four-byte one, so what it compares is a
 * cell nothing has written; in the original that is whatever the stack
 * happened to hold. Ours holds nought there. Nothing reaches it: no rule in
 * the nine languages IBM shipped calls test_string at all, which is why it
 * was missing from this engine, and a rule of ours would use one of the
 * three markers that work.
 */
int test_string(delta_state *d, uint8_t st, uint8_t n, const uint8_t *str)
{
    const delta_stmt *e = &vstmtbl[st];
    const uint8_t *p = str;
    const uint8_t *end = str + n;
    int32_t marker;
    int16_t v16 = 0;
    int32_t v32 = 0;
    delta_operand a, b;

    if (n == 0)
        return vscanadvOverToken(d, 1) ? 0 : 1;

    marker = *p;
    p++;

    a.ptr = 0;
    a.kind = 0;
    switch (marker) {
    case 199:
        a.kind = DK_UBYTE;
        break;
    case 200:
        a.kind = DK_SHORT;
        a.ptr = &v16;
        break;
    case 201:
        a.kind = DK_LONG;
        a.ptr = &v32;
        break;
    case 202:
        a.kind = DK_SHORT2;
        a.ptr = &v16;
        break;
    default:
        break;
    }
    a.flag = e->fields[0].flag;
    b.kind = e->fields[0].kind;
    b.flag = e->fields[0].flag;

    while (p < end) {
        int32_t node = scan_peek(d);

        if (node == 0)
            return 1;

        if ((*(int32_t *)(intptr_t)node & 2) == 0) {
            switch (marker) {
            case 199:
                /* The operand points into the string itself rather than at
                   a cell, so a byte is compared where it lies. */
                a.ptr = (void *)(intptr_t)p;
                p++;
                break;

            case 200:
            case 201:
                v32 = (int32_t)(((uint32_t)(p[0] & 0x7f) << 24)
                                | ((uint32_t)p[1] << 16)
                                | ((uint32_t)p[2] << 8)
                                | (uint32_t)p[3]);
                if ((p[0] & 0x80) != 0)
                    v32 = v32 * -1;
                p += 4;
                break;

            case 202:
                v16 = (int16_t)(((p[0] & 0x7f) << 8) | p[1]);
                if ((p[0] & 0x80) != 0)
                    v16 = (int16_t)(v16 * -1);
                p += 2;
                break;

            default:
                break;
            }

            b.ptr = e->get[0](TFLDS((void *)(intptr_t)node));
            vcompare(d, &a, &b);
            if (EVV_AT(delta_vars *, d->vars)->compared_equal != 0)
                return 1;
        }

        if (!vscanadv(d, 1, 1))
            return 1;
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
    int32_t base = EVV_AT(delta_vars *, d->vars)->fence_base;
    int32_t first = t;
    int32_t anchor = t;
    int32_t cur = t;
    int32_t marked = 0;
    int32_t depth = 1;
    int32_t next = 0;
    int32_t limit;
    int32_t result = 0;
    uint8_t i;

    EVV_WALK(w1);

    while (depth > 0) {
        EVV_WALK(w0);

        EVV_WALKED(d, w1);

        while (cur != 0
               && (*(int32_t *)(intptr_t)(cur + (base + f) * 4) & 1) != 0) {
            EVV_WALKED(d, w0);
            marked = cur;
            cur = *clink(d, cur) & ~3;
        }

        if (cur == 0)
            break;

        for (i = 0; i < d->nstmts; i++) {
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

    limit = right ? EVV_AT(delta_stack *, d->stack)->spine_r : EVV_AT(delta_stack *, d->stack)->spine_l;
    result = 0;

    EVV_WALK(w2);

    while (depth > 1) {
        EVV_WALK(w3);

        EVV_WALKED(d, w2);
        cur = first;

        while (cur != 0) {
            delta_node *n = (delta_node *)(intptr_t)cur;

            EVV_WALKED(d, w3);

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
    EVV_WALK(w4);

    while (cur != 0) {
        delta_node *n = (delta_node *)(intptr_t)cur;

        EVV_WALKED(d, w4);

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
    int32_t base = EVV_AT(delta_vars *, d->vars)->fence_base;
    int32_t next;
    int32_t value = 0;
    uint8_t went_right;
    uint8_t adjusted;
    EVV_WALK(w);

    e = &vstmtbl[f];
    get = e->get[0];

    if (off < 0) {
        went_right = 0;
        next = *(int32_t *)(intptr_t)(node + 0xc + f * 4) & ~3;

        while (node != EVV_AT(delta_stack *, d->stack)->spine_l) {
            EVV_WALKED(d, w);

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

        while (node != EVV_AT(delta_stack *, d->stack)->spine_r) {
            EVV_WALKED(d, w);

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
            node = EVV_REF(lmost(d, f,
                                            (delta_node *)(intptr_t)node));
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
            node = EVV_REF(rmost(d, f,
                                            (int32_t *)(intptr_t)node));
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

    if ((node == EVV_AT(delta_stack *, d->stack)->spine_l && off < 0)
        || (node == EVV_AT(delta_stack *, d->stack)->spine_r && off > 0))
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
    int32_t base = EVV_AT(delta_vars *, d->vars)->fence_base;
    int32_t l = left;
    int32_t r = right;

    if ((*(int32_t *)(intptr_t)(t + (base + f) * 4) & 1) != 0)
        return 1;

    if (left != 0 && (*(int32_t *)(intptr_t)left & 2) != 0
        && right != 0 && (*(int32_t *)(intptr_t)right & 2) != 0) {
        EVV_AT(delta_owner *, d->owner)->changed = 1;
        *(int32_t *)(intptr_t)(t + (base + f) * 4) |= 1;

        CLRONESTM((delta_node *)(intptr_t)t);
        if (ALLNSQ((const delta_node *)(intptr_t)t)
            && EVV_AT(int8_t *, EVV_AT(delta_vars *, d->vars)->nsq_marks)[f] == 0)
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
        EVV_AT(delta_owner *, d->owner)->changed = 1;
        *(int32_t *)(intptr_t)(t + (base + f) * 4) |= 1;

        CLRONESTM((delta_node *)(intptr_t)t);
        if (ALLNSQ((const delta_node *)(intptr_t)t)
            && EVV_AT(int8_t *, EVV_AT(delta_vars *, d->vars)->nsq_marks)[f] == 0)
            CLRALLNSQ((delta_node *)(intptr_t)t);

        ((delta_node *)(intptr_t)left)->link = t;
        *(int32_t *)(intptr_t)(t + (base + f) * 4) =
            (*(int32_t *)(intptr_t)(t + (base + f) * 4) & 3) | right;
        *(int32_t *)(intptr_t)(r + 0xc + f * 4) =
            (*(int32_t *)(intptr_t)(r + 0xc + f * 4) & 3) | t;
        *(int32_t *)(intptr_t)(t + 0xc + f * 4) =
            (*(int32_t *)(intptr_t)(t + 0xc + f * 4) & 3) | left;
    } else if (left != 0 && (*(int32_t *)(intptr_t)left & 2) != 0) {
        EVV_AT(delta_owner *, d->owner)->changed = 1;
        *(int32_t *)(intptr_t)(t + (base + f) * 4) |= 1;

        CLRONESTM((delta_node *)(intptr_t)t);
        if (ALLNSQ((const delta_node *)(intptr_t)t)
            && EVV_AT(int8_t *, EVV_AT(delta_vars *, d->vars)->nsq_marks)[f] == 0)
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

    if (NONSEQ((const delta_node *)(intptr_t)t) && EVV_AT(delta_vars *, d->vars)->relink != 0) {
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
                p->node = EVV_REF(rmost(d, p->field, (int32_t *)(intptr_t)p->node));
            else
                p->node = EVV_REF(lmost(d, p->field, (delta_node *)(intptr_t)p->node));
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

    EVV_WALK(w);

    for (;;) {
        EVV_WALKED(d, w);

        if (EVV_AT(delta_vars *, d->vars)->scan_ptr == d->lpta.node)
            return 0;
        if (!vscanadv(d, 0, 1))
            return 1;
    }
}

/* Walk the left register to the end of a field's run. Settling the position
   first can fail, and a rule that asked to move somewhere it cannot go is
   backtracked rather than left half moved. */
void lpta_movel(delta_state *d, uint8_t f)
{
    if (!vmove_tv(d, &d->lpta) || d->lpta.node == 0)
        forceErrorBacktrack(d);

    d->lpta.node = EVV_REF(vmovel(d, (delta_node *)(intptr_t)d->lpta.node, f));
}

void lpta_mover(delta_state *d, uint8_t f)
{
    if (!vmove_tv(d, &d->lpta) || d->lpta.node == 0)
        forceErrorBacktrack(d);

    d->lpta.node = EVV_REF(vmover(d, (int32_t *)(intptr_t)d->lpta.node, f));
}

void rpta_mover(delta_state *d, uint8_t f)
{
    if (!vmove_tv(d, &d->rpta) || d->rpta.node == 0)
        forceErrorBacktrack(d);

    d->rpta.node = EVV_REF(vmover(d, (int32_t *)(intptr_t)d->rpta.node, f));
}

/* The same rightward walk, but as a test: it only moves if the position was
   already on a timing mark. */
int lpta_tstmover(delta_state *d, uint8_t f)
{
    if (vtsttmark_tv(d, &d->lpta, 0) != 0 || d->lpta.node == 0)
        return 1;

    d->lpta.node = EVV_REF(vmover(d, (int32_t *)(intptr_t)d->lpta.node, f));
    return 0;
}

/* The right register's two, where the third argument of the mark test says
   which register is being asked about rather than which way it walks: the
   left pair pass nought and these pass one. */
int rpta_tstmover(delta_state *d, uint8_t f)
{
    if (vtsttmark_tv(d, &d->rpta, 1) != 0 || d->rpta.node == 0)
        return 1;

    d->rpta.node = EVV_REF(vmover(d, (int32_t *)(intptr_t)d->rpta.node, f));
    return 0;
}

int rpta_tstmovel(delta_state *d, uint8_t f)
{
    if (vtsttmark_tv(d, &d->rpta, 1) != 0 || d->rpta.node == 0)
        return 1;

    d->rpta.node = EVV_REF(vmovel(d, (delta_node *)(intptr_t)d->rpta.node, f));
    return 0;
}

/* Put the scan where the left register points, following a given field. The
   four spellings differ only in which way the scan will walk and whether the
   fence is left armed. */
static int setscan(delta_state *d, uint8_t f, uint8_t rev, uint8_t held)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);

    if (vtstsnc_tv(d, &d->lpta) != 0)
        return 1;

    if (d->lpta.node == 0
        || (*(int32_t *)(intptr_t)(d->lpta.node + (v->fence_base + f) * 4)
            & 1) == 0)
        return 1;

    v->scan_ptr = d->lpta.node;
    v->scan_field = f;
    v->scan_rev = rev;
    v->scan_held = held;
    return 0;
}

int setscan_l(delta_state *d, uint8_t f)     { return setscan(d, f, 0, 1); }
int setscan_r(delta_state *d, uint8_t f)     { return setscan(d, f, 1, 1); }
int setscan_nof_l(delta_state *d, uint8_t f) { return setscan(d, f, 0, 0); }
int setscan_nof_r(delta_state *d, uint8_t f) { return setscan(d, f, 1, 0); }

/* Where a context starts. With no context wanted it is just the neighbour in
   the field; with one, either the cheap spine walk or the full lookup,
   depending on whether the node is sequential and the field is fenced. */
int32_t vgetsc(delta_state *d, int32_t back, int32_t ctx, int32_t t, uint8_t f)
{
    if (ctx != 0) {
        if (EVV_AT(delta_vars *, d->vars)->relink != 0
            && !NONSEQ((const delta_node *)(intptr_t)t)
            && EVV_AT(int8_t *, EVV_AT(delta_vars *, d->vars)->nsq_marks)[f] == 0)
            return EVV_REF(ctxspine(d, (int32_t *)(intptr_t)t, f, back));

        return ctxlook(d, t, f, back);
    }

    if (back != 0)
        return *(int32_t *)(intptr_t)(t + 0xc + f * 4) & ~3;

    return *(int32_t *)(intptr_t)(t + (EVV_AT(delta_vars *, d->vars)->fence_base + f) * 4) & ~3;
}

/* Whether a position sits on a timing point. Like vtsttmark_tv, but a
   position that fell short is dragged to the end of its run and still
   counts. */
int vtimept_tv(delta_state *d, delta_tpos *p, uint8_t back)
{
    int32_t r;

    if ((p->flags & 1) != 0)
        return 1;

    r = vnormalize(d, p);

    if (r == 2)
        return 1;

    if (r == 3) {
        if (back == 0)
            p->node = EVV_REF(rmost(d, p->field, (int32_t *)(intptr_t)p->node));
        else
            p->node = EVV_REF(lmost(d, p->field, (delta_node *)(intptr_t)p->node));
        p->flags = 1;
        return 1;
    }

    if (r == 4) {
        p->flags = 1;
        return 1;
    }

    return 0;
}

/* Set a forall going: note what it iterates, point the left register at the
   token it starts from, and put the scan there with the field fenced. */
int for_loop_preamble(delta_state *d, int32_t tag, int32_t loop, int32_t f,
                      const delta_token *tok)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);

    v->loop_tag = loop;
    v->test_tag = tag;
    v->testing = 0;

    d->lpta.flags = 1;
    d->lpta.node = tok->value;

    if (vtstsnc_tv(d, &d->lpta) != 0)
        return 0;

    if (d->lpta.node == 0
        || (*(int32_t *)(intptr_t)(d->lpta.node + (v->fence_base + f) * 4)
            & 1) == 0)
        return 0;

    v->scan_ptr = d->lpta.node;
    v->scan_field = (uint8_t)f;
    v->scan_held = 1;
    EVV_AT(uint8_t *, d->fence_marks)[EVV_AT(uint8_t *, d->fence_index)[f]] = 1;
    return 1;
}

/* Give a statement the same place in every field another one occupies. */
int dupsync(delta_state *d, int32_t t, int32_t src, uint8_t back)
{
    int32_t bs = EVV_AT(delta_vars *, d->vars)->fence_base;
    uint8_t i;

    for (i = 0; i < d->nstmts; i++) {
        int32_t here = *(int32_t *)(intptr_t)(src + (bs + i) * 4);

        if ((here & 1) == 0)
            continue;

        if (back != 0) {
            int32_t l = *(int32_t *)(intptr_t)(src + 0xc + i * 4) & ~3;

            if (!vproject(d, t, l, src, i))
                return 0;
        } else {
            if (!vproject(d, t, src, here & ~3, i))
                return 0;
        }
    }

    return 1;
}

/* Put a statement where the language says it belongs by default: find what
   is on each side of it in the field and splice it between them. */
int vdef_proj(delta_state *d, int32_t t, uint8_t f)
{
    int32_t l;
    int32_t r;

    if (t == 0)
        return 0;

    if ((*(int32_t *)(intptr_t)(t + (EVV_AT(delta_vars *, d->vars)->fence_base + f) * 4) & 1) != 0)
        return 1;

    l = vgetsc(d, 1, 1, t, f);

    if (l == 0)
        return 0;

    if (EVV_AT(delta_vars *, d->vars)->ctx_both != 0)
        r = vgetsc(d, 0, 1, t, f);
    else
        r = VRSYNC(d, (const int32_t *)(intptr_t)l, (int8_t)f);

    return vproject(d, t, l, r, f);
}

/* Settle both ends of a range, then pull each onto the sync it is next to if
   it is still leaning the wrong way. */
int vprt_range(delta_state *d, delta_tpos *a, delta_tpos *b)
{
    if (!vtimept_tv(d, a, 0))
        return 0;
    if (!vtimept_tv(d, b, 1))
        return 0;

    if ((a->flags & 2) != 0 && a->offset > 0)
        a->node = VRSYNC(d, (const int32_t *)(intptr_t)a->node, a->field);

    if ((b->flags & 2) != 0 && b->offset < 0)
        b->node = VLSYNC((const delta_node *)(intptr_t)b->node, b->field);

    return 1;
}

/* One step of a forto walking rightward. Two means the loop body should run,
   one that it never started, zero that it is finished. */
int forto_adv_r(delta_state *d, int16_t tag, int16_t loop, int16_t bound,
                uint8_t f, delta_token *tok, const delta_token *end)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);

    if (!for_loop_preamble(d, tag, loop, f, tok))
        return 1;

    v->scan_rev = 1;

    if (!vscanadv(d, 1, 0))
        return 0;
    if (v->scan_ptr == end->value)
        return 0;

    clearDeltaStackBack(d);
    EVV_AT(delta_stack *, d->stack)->unknown_9c = 0;
    v->testing = 1;
    d->unknown_3c = bound;
    tok->value = v->scan_ptr;
    return 2;
}

/* Where the scan's field points from where the scan is now. */
static int32_t scan_here(delta_state *d)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);

    return *(int32_t *)(intptr_t)
        (v->scan_ptr + (v->fence_base + v->scan_field) * 4) & ~3;
}

/* One step of a forto that must stop before a named token. It checks twice,
   once on arriving and once after stepping past the statement, that there is
   something real ahead that is not itself a sync. */
int forto_adv_upto_r(delta_state *d, int16_t tag, int16_t loop, int16_t bound,
                     uint8_t f, delta_token *tok, const delta_token *end)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);
    int32_t nx;

    if (!for_loop_preamble(d, tag, loop, f, tok))
        return 1;

    v->scan_rev = 1;

    vscanadvUptoTokenOrMarker(d, end->value, 0);
    if (v->scan_ptr == end->value)
        return 0;

    nx = scan_here(d);
    if (nx == 0)
        return 0;
    if ((*(int32_t *)(intptr_t)nx & 2) != 0)
        return 0;

    if (!vscanadv(d, 1, 0))
        return 0;
    if (v->scan_ptr == end->value)
        return 0;

    vscanadvUptoTokenOrMarker(d, end->value, 0);
    if (v->scan_ptr == end->value)
        return 0;

    nx = scan_here(d);
    if (nx == 0)
        return 0;
    if ((*(int32_t *)(intptr_t)nx & 2) != 0)
        return 0;

    clearDeltaStackBack(d);
    EVV_AT(delta_stack *, d->stack)->unknown_9c = 0;
    v->testing = 1;
    d->unknown_3c = bound;
    tok->value = v->scan_ptr;
    return 2;
}

/* Hand the span between the two registers to one of the language's lookup
   sets. A span that cannot be settled backtracks the rule. */
int setd_lookup(delta_state *d, int32_t arg, int16_t set)
{
    uint8_t *desc;

    if (d->lpta.node == 0 || d->rpta.node == 0)
        return 1;

    if (!vprt_range(d, &d->lpta, &d->rpta))
        forceErrorBacktrack(d);

    desc = EVV_AT(uint8_t *, d->sets) + (int32_t)set * 0x24;

    if (!setdlookup(d, d->lpta.node, d->rpta.node, desc, arg))
        return 1;

    return 0;
}

/* Write a value into one field of every statement in a run.

   The original parks the address of its own field argument in the stack
   block, which is the caller's frame and so stays valid for as long as the
   caller needs it. A copy is the closest C gets: it holds the same byte and
   dies at the same point in the call, but not afterwards.

   The copy is in the frame stack rather than a local, because the machine is
   handed its address as a value. A local would be on the C stack, and a C
   stack is somewhere a 32-bit value can name only if the host put it there --
   true of the process's first thread, and not true of a thread the system
   placed where it liked, which is every thread on Windows and now every thread
   here. The frame stack is in the arena and nests with the call, so it holds
   the byte for exactly as long as the argument would have. */
int vmark(delta_state *d, uint8_t st, uint8_t fld, int32_t t, int32_t stop,
          const void *value)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);
    int32_t base = EVV_AT(delta_vars *, d->vars)->fence_base;
    const delta_stmt *e = &vstmtbl[st];
    uint8_t *kept = evv_frame_push(sizeof fld);

    if (kept != 0)
        *kept = fld;

    s->mark_kind = -1;
    s->mark_fld = EVV_REF(kept != 0 ? kept : &fld);
    s->mark_flag = 0;

    EVV_WALK(w);

    while (t != s->spine_r && t != stop) {
        int32_t next = *(int32_t *)(intptr_t)(t + (base + st) * 4) & ~3;

        EVV_WALKED(d, w);

        if (next != 0 && (*(int32_t *)(intptr_t)next & 2) != 0) {
            t = next;
            continue;
        }

        e->put[fld](TFLDS((void *)(intptr_t)next), value);
        t = *(int32_t *)(intptr_t)(next + 4) & ~3;
    }

    EVV_AT(delta_owner *, d->owner)->changed = 1;
    if (kept != 0)
        evv_frame_pop(kept);
    return 1;
}

/* Whether one statement comes before another on the spine.

   With the spine kept in order this is a walk along the links, and the answer
   is worth remembering: the table of fifty is thrown away whenever the spine
   is relinked. Otherwise it has to be worked out field by field, and the slot
   guards on that path can never fire, because every route that sets a slot
   returns before reaching them. They are kept because the original has them. */
int visleft(delta_state *d, int32_t a, int32_t b)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);
    int32_t base = EVV_AT(delta_vars *, d->vars)->fence_base;
    int32_t slot = -1;
    int32_t p;
    int32_t i;
    int8_t j;
    int8_t fld = -10;
    int8_t alt = 0;

    if (a == b)
        return 0;

    if (EVV_AT(delta_vars *, d->vars)->relink != 0
        && !NONSEQ((const delta_node *)(intptr_t)a)
        && !NONSEQ((const delta_node *)(intptr_t)b)) {
        if (s->left_stamp == spine_changed) {
            for (i = 0; i < 50; i++) {
                if (s->left_a[i] != a || s->left_b[i] != b)
                    continue;
                s->left_hits[i]++;
                return s->left_ans[i];
            }

            s->left_next++;
            if (s->left_next >= 50)
                s->left_next = 0;

            slot = s->left_next;
            while (s->left_hits[slot] > 12) {
                slot++;
                if (slot >= 50)
                    slot = 0;
                if (slot == s->left_next)
                    break;
            }

            s->left_next = slot;
            s->left_a[slot] = a;
            s->left_b[slot] = b;
            s->left_hits[slot] = 1;
        } else {
            for (i = 0; i < 50; i++) {
                s->left_a[i] = 0;
                s->left_b[i] = 0;
                s->left_hits[i] = 0;
            }

            slot = 0;
            s->left_a[0] = a;
            s->left_b[0] = b;
            s->left_hits[0] = 1;
            s->left_next = 0;
            s->left_stamp = spine_changed;
        }

        p = ((const delta_node *)(intptr_t)b)->link & ~3;
        EVV_WALK(w1);

        for (i = 0; ; i++) {
            EVV_WALKED(d, w1);

            if (p == 0) {
                s->left_ans[slot] = 0;
                return 0;
            }
            if (p == a) {
                s->left_ans[slot] = 1;
                return 1;
            }
            p = ((const delta_node *)(intptr_t)p)->link & ~3;
        }
    }

    for (j = (int8_t)(d->nstmts - 1); j >= 0; j--) {
        if ((*(int32_t *)(intptr_t)(a + (base + j) * 4) & 1) == 0)
            continue;

        if ((*(int32_t *)(intptr_t)(b + (base + j) * 4) & 1) != 0) {
            fld = j;
            break;
        }

        alt = j;
    }

    if (fld != -10) {
        p = VLSYNC((const delta_node *)(intptr_t)b, fld);
        if (p == 0) {
            if (slot >= 0)
                s->left_ans[slot] = 0;
            return 0;
        }
    } else {
        fld = alt;
        p = vgetsc(d, 1, 1, b, (uint8_t)fld);
    }

    EVV_WALK(w2);

    while (p != a) {
        EVV_WALKED(d, w2);
        p = *(int32_t *)(intptr_t)(p + 0xc + fld * 4) & ~3;
        if (p == 0) {
            if (slot >= 0)
                s->left_ans[slot] = 0;
            return 0;
        }

        if ((*(int32_t *)(intptr_t)p & 2) == 0)
            p = *(int32_t *)(intptr_t)p & ~3;
    }

    if (slot >= 0)
        s->left_ans[slot] = 1;
    return 1;
}

int visright(delta_state *d, int32_t a, int32_t b)
{
    return visleft(d, b, a);
}

/* Compare two positions. Two settled ones compare by node alone. Two loose
   ones are settled and compared whole. One of each is the interesting case:
   the loose one is settled, and if it landed just short of a run it counts as
   equal when the other end sits inside the same run. */
int vcomp_pta(delta_state *d, delta_tpos *a, delta_tpos *b)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);
    delta_tpos *loose;
    int32_t other;
    int32_t r;

    if ((a->flags & 1) != 0 && (b->flags & 1) != 0) {
        v->compared_equal = (int8_t)(a->node != b->node);
        return 0;
    }

    if ((a->flags & 2) != 0 && (b->flags & 2) != 0) {
        vnormalize(d, a);
        vnormalize(d, b);

        if (a->field == b->field && a->node == b->node
            && a->offset == b->offset)
            v->compared_equal = 0;
        else
            v->compared_equal = 1;

        return 0;
    }

    if ((a->flags & 2) != 0) {
        loose = a;
        other = b->node;
    } else {
        loose = b;
        other = a->node;
    }

    r = vnormalize(d, loose);

    if (r == 0)
        return 1;

    if (r == 1 || r == 2) {
        v->compared_equal = 1;
        return 0;
    }

    if (r == 3) {
        int32_t lm = EVV_REF(lmost(d, loose->field, (delta_node *)(intptr_t)loose->node));
        int32_t rm = EVV_REF(rmost(d, loose->field, (int32_t *)(intptr_t)loose->node));
        int32_t lo;
        int32_t hi;

        if ((*(int32_t *)(intptr_t)
             (other + (v->fence_base + loose->field) * 4) & 1) != 0) {
            hi = other;
            lo = hi;
        } else {
            lo = *(int32_t *)(intptr_t)
                (other + 0xc + loose->field * 4) & ~3;
            hi = *(int32_t *)(intptr_t)
                (other + (v->fence_base + loose->field) * 4) & ~3;
        }

        if ((lo == lm || visleft(d, lm, lo))
            && (hi == rm || visright(d, rm, hi)))
            v->compared_equal = 0;
        else
            v->compared_equal = 1;

        return 0;
    }

    if (r == 4) {
        v->compared_equal = (int8_t)(loose->node != other);
        return 0;
    }

    return 0;
}

/* Name the kind of change that went wrong, for a message.

   The original picks the name into a local it never reads, so what it hands
   back is whatever was in the register; it was plainly meant to return the
   name and the call that printed it has been compiled out. Returning the name
   is the only sensible reading, and nothing can depend on the original's
   value because that value is indeterminate. */
const char *vseqbad(void *w, void *x, void *y, const char *what)
{
    (void)w;
    (void)x;
    (void)y;

    switch (*what) {
    case 'p': return "projection";
    case 'i': return "insertion";
    case 'd': return "deletion";
    default:  return "???";
    }
}

/* Deletion is not deferred on this build: the object goes back at once. */
void cacheDeletedDeltaObject(delta_state *d, void *p)
{
    free_heap(d, p);
}

int compare_ptas(delta_state *d)
{
    return vcomp_pta(d, &d->lpta, &d->rpta) != 0;
}

void delsync(delta_state *d, void *p)
{
    cacheDeletedDeltaObject(d, p);
}

/* Fold the statement on one side of a sync into the one on the other, then
   unlink the sync. Only a language that says the type is walkable allows it,
   and only when neither neighbour is itself a sync. */
int mashtoks(delta_state *d, uint8_t f, int32_t t)
{
    int32_t base = EVV_AT(delta_vars *, d->vars)->fence_base;
    int32_t l = *(int32_t *)(intptr_t)(t + 0xc + f * 4) & ~3;
    int32_t r = *(int32_t *)(intptr_t)(t + (base + f) * 4) & ~3;
    const delta_stmt *e = &vstmtbl[f];
    delta_operand a;
    delta_operand b;
    int32_t nx;

    if ((l != 0 && (*(int32_t *)(intptr_t)l & 2) != 0)
        || (r != 0 && (*(int32_t *)(intptr_t)r & 2) != 0))
        return 1;

    b.kind = STMTYP((int8_t)f);
    a.kind = b.kind;
    b.flag = e->fields[0].flag;
    a.flag = b.flag;

    a.ptr = e->get[0](TFLDS((void *)(intptr_t)l));
    b.ptr = e->get[0](TFLDS((void *)(intptr_t)r));

    if (e->walkable == 0)
        return 0;

    vadd(d, &b, &a);
    vinitflds(d, f, a.ptr, b.ptr);

    EVV_AT(delta_owner *, d->owner)->changed = 1;

    nx = *(int32_t *)(intptr_t)(r + 4) & ~3;

    *(int32_t *)(intptr_t)(t + (base + f) * 4) =
        (*(int32_t *)(intptr_t)(t + (base + f) * 4) & 3) | nx;
    *(int32_t *)(intptr_t)(nx + 0xc + f * 4) =
        (*(int32_t *)(intptr_t)(nx + 0xc + f * 4) & 3) | t;

    cacheDeletedDeltaObject(d, (void *)(intptr_t)r);
    return 1;
}

/* Whether a statement may take part in a change. One that is neither a lone
   statement nor wholly nonsequential may not; nor may one that carries more
   than one field with any of them unmarked. */
int vchkseqbad(delta_state *d, int32_t t, uint8_t f, const char *what)
{
    int32_t present = 0;
    int32_t marked = 0;
    uint8_t i;

    if (!ONESTM((const delta_node *)(intptr_t)t)
        && !ALLNSQ((const delta_node *)(intptr_t)t)) {
        vseqbad(d, (void *)(intptr_t)t, (void *)(intptr_t)(int32_t)f, what);
        return 0;
    }

    for (i = 0; i < d->nstmts; i++) {
        if ((*(int32_t *)(intptr_t)
             (t + (EVV_AT(delta_vars *, d->vars)->fence_base + i) * 4) & 1) == 0)
            continue;

        present++;
        if (EVV_AT(int8_t *, EVV_AT(delta_vars *, d->vars)->nsq_marks)[i] != 0)
            marked++;
    }

    if (marked < present && present > 1) {
        vseqbad(d, (void *)(intptr_t)t, (void *)(intptr_t)(int32_t)f, what);
        return 0;
    }

    return 1;
}

/* Put a fresh sync into a field between two nodes.

   The new sync carries the field, and joins the chain on both sides: a
   neighbour that is itself a sync is linked through the field, one that is
   not is linked through its own words. Then, if the spine is being kept in
   order, everything between the two neighbours is marked nonsequential,
   either because the field is fenced or because something in the span was
   not a lone statement. */
void *vins_sync(delta_state *d, uint8_t f, int32_t l, int32_t r)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);
    int32_t base = v->fence_base;
    delta_node *s = alloc_sync(d);
    int32_t sv;
    int32_t left;
    int32_t right;
    int32_t p;
    int32_t nonseq;

    if (s == NULL)
        return NULL;

    sv = EVV_REF(s);

    *(int32_t *)(intptr_t)(sv + (base + f) * 4) |= 1;

    if (EVV_AT(int8_t *, v->nsq_marks)[f] != 0)
        SETALLNSQ(s);

    EVV_AT(delta_owner *, d->owner)->changed = 1;

    if (l != 0 && (*(int32_t *)(intptr_t)l & 2) != 0) {
        left = l;
        *(int32_t *)(intptr_t)(left + (base + f) * 4) =
            (*(int32_t *)(intptr_t)(left + (base + f) * 4) & 3) | sv;
    } else {
        left = *(int32_t *)(intptr_t)l & ~3;
        ((delta_node *)(intptr_t)l)->link = sv;
    }

    *(int32_t *)(intptr_t)(sv + 0xc + f * 4) =
        (*(int32_t *)(intptr_t)(sv + 0xc + f * 4) & 3) | l;

    if (r != 0 && (*(int32_t *)(intptr_t)r & 2) != 0) {
        right = r;
        *(int32_t *)(intptr_t)(right + 0xc + f * 4) =
            (*(int32_t *)(intptr_t)(right + 0xc + f * 4) & 3) | sv;
    } else {
        right = *(int32_t *)(intptr_t)(r + 4) & ~3;
        *(int32_t *)(intptr_t)r = sv;
    }

    *(int32_t *)(intptr_t)(sv + (base + f) * 4) =
        (*(int32_t *)(intptr_t)(sv + (base + f) * 4) & 3) | r;

    if (v->relink != 0) {
        p = *rlink(d, left) & ~3;

        if (p != right) {
            nonseq = 0;

            if (EVV_AT(int8_t *, v->nsq_marks)[f] != 0) {
                nonseq = 1;
            } else {
                EVV_WALK(w1);

                while (p != right) {
                    EVV_WALKED(d, w1);

                    if (!ONESTM((const delta_node *)(intptr_t)p)
                        && !ALLNSQ((const delta_node *)(intptr_t)p)) {
                        nonseq = 1;
                        break;
                    }
                    p = *rlink(d, p) & ~3;
                }
            }

            if (nonseq != 0) {
                SETNONSEQ(s);
                if (v->ctx_both != 0 && !ONESTM(s)
                    && !vchkseqbad(d, sv, f, "i5"))
                    return NULL;
            } else {
                p = *rlink(d, left) & ~3;
                EVV_WALK(w2);

                while (p != right) {
                    EVV_WALKED(d, w2);
                    SETNONSEQ((delta_node *)(intptr_t)p);
                    if (v->ctx_both != 0
                        && !ONESTM((const delta_node *)(intptr_t)p)
                        && !vchkseqbad(d, p, f, "i1"))
                        return NULL;
                    p = *rlink(d, p) & ~3;
                }
            }
        }

        INSSPINER(d, s, (delta_node *)(intptr_t)left);
    }

    v->unknown_1170 = 0;
    return s;
}

/* Whether a statement may be deleted, and if so, mark the run it leaves
   behind as nonsequential.

   The two neighbours are the first sequential node on each side. Every field
   is then classified: carried by the statement and by the left neighbour,
   carried by the statement and the right one, or carried by both neighbours
   but not the statement. A field shared with both neighbours, or a mixture of
   left and right, means the deletion would break the ordering and the answer
   is no. Otherwise the three control blocks in the stack drive seqscan over
   the affected run, and the walk at the end marks it. */
int chkdelnonseq(delta_state *d, int32_t t, uint8_t f)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);
    delta_vars *v = EVV_AT(delta_vars *, d->vars);
    int32_t base = v->fence_base;
    int32_t nboth = 0;
    int32_t nleft = 0;
    int32_t nright = 0;
    int32_t l;
    int32_t r;
    int32_t i;
    delta_seqctl *ctl;
    int32_t kind;
    int32_t p;
    EVV_WALK(wl);
    EVV_WALK(wr);
    EVV_WALK(wm);

#define CARRIES(node, fld) \
    ((*(int32_t *)(intptr_t)((node) + (base + (fld)) * 4) & 1) != 0)

    if (v->relink == 0)
        return 1;

    l = ((const delta_node *)(intptr_t)t)->link & ~3;
    while (l != 0 && NONSEQ((const delta_node *)(intptr_t)l)) {
        EVV_WALKED(d, wl);
        l = ((const delta_node *)(intptr_t)l)->link & ~3;
    }

    r = *rlink(d, t) & ~3;
    while (r != 0 && NONSEQ((const delta_node *)(intptr_t)r)) {
        EVV_WALKED(d, wr);
        r = *rlink(d, r) & ~3;
    }

    if (l == 0 || r == 0)
        forceErrorBacktrack(d);

    for (i = (int32_t)d->nstmts - 1; i >= (int32_t)f; i--) {
        if (CARRIES(t, i)) {
            if (CARRIES(l, i)) {
                if (nright != 0)
                    return 0;
                if (CARRIES(r, i))
                    return 0;
                nleft++;
            } else if (nright == 0 && CARRIES(r, i)) {
                if (nleft != 0)
                    return 0;
                nright++;
            }
        } else if (nboth == 0 && CARRIES(l, i) && CARRIES(r, i)) {
            nboth++;
        }
    }

    for (i = 0; i < (int32_t)f; i++) {
        if (CARRIES(t, i)) {
            if (CARRIES(l, i)) {
                nleft++;
                if (nright != 0)
                    return 0;
                if (CARRIES(r, i))
                    return 0;
            } else if (nright == 0) {
                if (CARRIES(r, i)) {
                    if (nleft != 0)
                        return 0;
                    nright++;
                } else if (nleft != 0) {
                    return 0;
                }
            }
        } else if (nboth == 0 && CARRIES(l, i) && CARRIES(r, i)) {
            nboth++;
        }
    }
#undef CARRIES

    if (nright != 0 && nleft != 0)
        return 0;

    if (nboth != 0) {
        s->runs[0].kind = 0;
        s->runs[0].cur = t;
        s->runs[0].start = t;
        s->runs[0].flag = (!ALLNSQ((const delta_node *)(intptr_t)t)
                           && !ONESTM((const delta_node *)(intptr_t)t));

        if (nright != 0) {
            s->runs[1].start = l;
            s->runs[1].kind = -1;
        } else if (nleft != 0) {
            s->runs[1].start = r;
            s->runs[1].kind = 1;
        } else {
            s->runs[1].kind = 2;
        }
    } else if (nright != 0) {
        s->runs[0].start = l;
        s->runs[0].kind = -1;
        s->runs[1].start = t;
        s->runs[1].kind = 1;
    } else if (nleft != 0) {
        s->runs[0].start = r;
        s->runs[0].kind = 1;
        s->runs[1].start = t;
        s->runs[1].kind = -1;
    } else {
        return 0;
    }

    if (s->runs[0].kind != 0)
        seqscan(d, &s->runs[0]);

    kind = s->runs[1].kind;

    if (kind == -1 || kind == 1) {
        seqscan(d, &s->runs[1]);
    } else if (kind == 2) {
        if (s->runs[0].start != l || s->runs[0].kind != -1) {
            s->runs[2].start = l;
            s->runs[2].kind = -1;
            seqscan(d, &s->runs[2]);
        } else {
            s->runs[2].start = l;
            s->runs[2].kind = -1;
            s->runs[2].cur = s->runs[0].cur;
            s->runs[2].flag = s->runs[0].flag;
        }

        s->runs[1].kind = 1;
        s->runs[1].start = r;
        seqscan(d, &s->runs[1]);

        s->runs[1].start = s->runs[2].cur;
        s->runs[1].flag |= s->runs[2].flag;
    }

    ctl = &s->runs[s->runs[0].flag != 0 ? 1 : 0];
    kind = ctl->kind;
    p = ctl->start;

    for (;;) {
        EVV_WALKED(d, wm);
        SETNONSEQ((delta_node *)(intptr_t)p);

        if (v->ctx_both != 0
            && !ONESTM((const delta_node *)(intptr_t)p)
            && !vchkseqbad(d, p, f, "d8"))
            return 0;

        if (p == ctl->cur)
            return 1;

        if (kind >= 0)
            p = *rlink(d, p) & ~3;
        else
            p = ((const delta_node *)(intptr_t)p)->link & ~3;
    }
}

/* Delete a run of the spine, from one node up to and including another.

   Each sync in the run is unlinked from the field and either dropped from the
   spine outright, when it was the only statement there, or left for
   chkdelnonseq to work out what the removal did to the ordering. Anything
   that is not a sync goes straight back to the heap. What was on each side of
   the run is then joined up. */
int fdeldel(delta_state *d, int32_t from, int32_t to, int32_t arg)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);
    delta_vars *v = EVV_AT(delta_vars *, d->vars);
    int32_t base = v->fence_base;
    int32_t fd = s->del_field;
    int32_t before;
    int32_t p;
    int32_t next = 0;

    /* The original passes this on to delsync, which takes two arguments and
       ignores it. */
    (void)arg;

    EVV_AT(delta_owner *, d->owner)->changed = 1;

    if (from != 0 && (*(int32_t *)(intptr_t)from & 2) != 0)
        before = *(int32_t *)(intptr_t)(from + 0xc + fd * 4) & ~3;
    else
        before = *(int32_t *)(intptr_t)from & ~3;

    p = from;

    EVV_WALK(w);

    for (;;) {
        EVV_WALKED(d, w);

        if (p == 0)
            return 0;

        if ((*(int32_t *)(intptr_t)p & 2) != 0) {
            int32_t t = p;
            int32_t nonseq;

            next = *(int32_t *)(intptr_t)(t + (base + fd) * 4) & ~3;
            nonseq = ONESTM((const delta_node *)(intptr_t)t) ? 0 : 1;

            /* The immediate is 0xfffffffe, so this clears bit zero. */
            *(int32_t *)(intptr_t)(t + (base + fd) * 4) &= ~1;
            *(int32_t *)(intptr_t)(t + 0xc + fd * 4) &= 3;
            *(int32_t *)(intptr_t)(t + (base + fd) * 4) &= 3;

            if (nonseq == 0) {
                if (v->relink != 0)
                    DELSPINE(d, (delta_node *)(intptr_t)t);
                delsync(d, (void *)(intptr_t)t);
            } else {
                vnsqflags(d, (int32_t *)(intptr_t)t);
                chkdelnonseq(d, t, (uint8_t)fd);
            }
        } else {
            next = *(int32_t *)(intptr_t)(p + 4) & ~3;
            cacheDeletedDeltaObject(d, (void *)(intptr_t)p);
        }

        if (p == to)
            break;

        p = next;
    }

    if (before != 0 && (*(int32_t *)(intptr_t)before & 2) != 0) {
        *(int32_t *)(intptr_t)(before + (base + fd) * 4) =
            (*(int32_t *)(intptr_t)(before + (base + fd) * 4) & 3) | next;
    } else {
        if (next == 0 || (*(int32_t *)(intptr_t)next & 2) == 0)
            return 0;
        ((delta_node *)(intptr_t)before)->link = next;
    }

    if (next != 0 && (*(int32_t *)(intptr_t)next & 2) != 0) {
        *(int32_t *)(intptr_t)(next + 0xc + fd * 4) =
            (*(int32_t *)(intptr_t)(next + 0xc + fd * 4) & 3) | before;
    } else {
        *(int32_t *)(intptr_t)next = before;
    }

    flushDeletedDeltaObjects(d);
    return 1;
}

/* Delete what the stack block says to. A whole delete takes the run between
   the two nodes it recorded; a partial one takes what lies between the two
   ends, one step in from each. */
void fdel(delta_state *d, int32_t whole, int32_t arg)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);
    int32_t fd = s->del_field;

    if (whole != 0) {
        fdeldel(d, s->del_from, s->del_to, arg);
    } else {
        int32_t from = *(int32_t *)(intptr_t)
            (s->del_left + (EVV_AT(delta_vars *, d->vars)->fence_base + fd) * 4) & ~3;
        int32_t to = *(int32_t *)(intptr_t)
            (s->del_right + 0xc + fd * 4) & ~3;

        fdeldel(d, from, to, arg);
    }

    flushDeletedDeltaObjects(d);
}

/* Delete one statement: fold it into its neighbour if the language allows,
   then take the run out. */
int vdel_1pt(delta_state *d, uint8_t f, int32_t t, int32_t arg)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);

    EVV_AT(delta_owner *, d->owner)->changed = 1;

    s->del_field = (int8_t)f;
    s->del_to = t;
    s->del_from = t;
    s->del_left = VLSYNC((const delta_node *)(intptr_t)t, s->del_field);
    s->del_right = VRSYNC(d, (const int32_t *)(intptr_t)t, s->del_field);

    if ((*(int32_t *)(intptr_t)
         (t + (EVV_AT(delta_vars *, d->vars)->fence_base + f) * 4) & 1) == 0)
        return 1;

    mashtoks(d, f, t);
    fdel(d, 1, arg);
    return 1;
}

/* Delete everything between two statements. When the right-hand end turns out
   to be the first thing past the left one there is nothing between them, and
   only a stray non-sync is removed. */
int vdel_2pt(delta_state *d, uint8_t f, int32_t l, int32_t r)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);

    s->del_field = (int8_t)f;
    s->del_left = l;
    s->del_right = r;
    s->del_from = VRSYNC(d, (const int32_t *)(intptr_t)s->del_left,
                         s->del_field);
    s->del_to = VLSYNC((const delta_node *)(intptr_t)s->del_right,
                       s->del_field);

    EVV_AT(delta_owner *, d->owner)->changed = 1;

    if (s->del_right == s->del_from) {
        int32_t p = *(int32_t *)(intptr_t)
            (s->del_left + (EVV_AT(delta_vars *, d->vars)->fence_base + s->del_field) * 4) & ~3;

        if (p == 0 || (*(int32_t *)(intptr_t)p & 2) == 0)
            fdeldel(d, p, p, 0);
    } else {
        fdel(d, 0, 0);
    }

    return 1;
}

/* Put a new statement into a field between two nodes. Anything already
   between them goes first. The value is either copied in whole, when the
   caller has a record of the language's own type, or laid down field by
   field from whatever it does have. */
int vins_tok(delta_state *d, uint8_t f, int32_t l, int32_t r,
             const delta_operand *v)
{
    int32_t base = EVV_AT(delta_vars *, d->vars)->fence_base;
    int32_t t;

    EVV_AT(delta_owner *, d->owner)->changed = 1;

    if ((*(int32_t *)(intptr_t)(l + (base + f) * 4) & ~3) != r
        || (*(int32_t *)(intptr_t)(r + 0xc + f * 4) & ~3) != l)
        vdel_2pt(d, f, l, r);

    t = EVV_REF(alloc_tok(d, &vstmtbl[f]));
    if (t == 0)
        return 0;

    *(int32_t *)(intptr_t)(l + (base + f) * 4) =
        (*(int32_t *)(intptr_t)(l + (base + f) * 4) & 3) | t;
    *(int32_t *)(intptr_t)(r + 0xc + f * 4) =
        (*(int32_t *)(intptr_t)(r + 0xc + f * 4) & 3) | t;

    ((delta_node *)(intptr_t)t)->link = r;
    *(int32_t *)(intptr_t)t = l;

    if (v->kind >= 0)
        memcpy(TFLDS((void *)(intptr_t)t), v->ptr,
               (size_t)vstmtbl[f].length);
    else
        vinitflds(d, f, (char *)(intptr_t)t + 8, v->ptr);

    EVV_AT(delta_owner *, d->owner)->changed = 1;
    EVV_AT(delta_vars *, d->vars)->unknown_1170 = 0;
    return 1;
}

/* Empty a field and put the language's own starting statement in it. */
int vinit_stm(delta_state *d, int8_t f)
{
    const delta_stmt *e = &vstmtbl[f];
    delta_stack *s = EVV_AT(delta_stack *, d->stack);
    delta_operand v;

    vdel_2pt(d, (uint8_t)f, s->spine_l, s->spine_r);

    if (e->unknown_18 == 0)
        return 1;

    v.kind = e->fields[0].kind;
    v.flag = e->fields[0].flag;
    v.ptr = e->get[0]((void *)(intptr_t)e->deflt);

    if (!vins_tok(d, (uint8_t)f, s->spine_l, s->spine_r, &v))
        return 0;

    return 1;
}

/* Insert a run of statements, one per value in the string, with a fresh sync
   between each pair. Both spellings share everything but how they read a
   value; a string of nothing is a plain delete. */
static int ins_tokens_run(delta_state *d, uint8_t f, const uint8_t *str,
                          uint8_t n, int32_t arg, int16_t kind)
{
    delta_operand a;
    delta_operand b;
    uint8_t ch = 0;
    int32_t lng = 0;
    int16_t shrt = 0;
    const uint8_t *end;

    if (n == 0) {
        vdel_2pt(d, f, d->lpta.node, d->rpta.node);
        return 1;
    }

    a.kind = STMTYP((int8_t)f);
    a.flag = vstmtbl[f].fields[0].flag;

    switch (STMTYP((int8_t)f)) {
    case DK_UBYTE:  a.ptr = &ch; break;
    case DK_LONG:   a.ptr = &lng; break;
    case DK_SHORT2:
    case DK_SHORT:  a.ptr = &shrt; break;
    default:        return 0;
    }

    end = str + n;

    /* Which cell the string is decoded into, and therefore how much of the
       string one token takes: a byte, two bytes or four, the wide ones sign
       first. The four entry points below differ in nothing else. */
    b.kind = kind;
    switch (kind) {
    case DK_LONG:   b.ptr = &lng; break;
    case DK_UBYTE:  b.ptr = &ch; break;
    default:        b.ptr = &shrt; break;
    }
    b.flag = vstmtbl[f].fields[0].flag;

    while (str < end) {
        switch (kind) {
        case DK_UBYTE:
            ch = *str;
            str++;
            break;

        case DK_LONG:
            lng = (int32_t)(((uint32_t)(str[0] & 0x7f) << 24)
                            | ((uint32_t)str[1] << 16)
                            | ((uint32_t)str[2] << 8)
                            | (uint32_t)str[3]);
            if ((str[0] & 0x80) != 0)
                lng = -lng;
            str += 4;
            break;

        default:
            shrt = (int16_t)(((str[0] & 0x7f) << 8) | str[1]);
            if ((str[0] & 0x80) != 0)
                shrt = (int16_t)(-shrt);
            str += 2;
            break;
        }

        if (a.kind != b.kind)
            vassign(d, &a, &b);

        if (!vins_tok(d, f, d->lpta.node, d->rpta.node, &a))
            return 0;

        if (str < end) {
            d->lpta.node = EVV_REF(vins_sync(d, f,
                *(int32_t *)(intptr_t)(d->rpta.node + 0xc + f * 4) & ~3,
                d->rpta.node));
            (void)arg;
            if (d->lpta.node == 0)
                return 0;
        }
    }

    return 1;
}

int ins_tokens_s(delta_state *d, uint8_t f, const uint8_t *str, uint8_t n,
                 int32_t arg)
{
    return ins_tokens_run(d, f, str, n, arg, DK_UBYTE);
}

int ins_tokens_i(delta_state *d, uint8_t f, const uint8_t *str, uint8_t n,
                 int32_t arg)
{
    return ins_tokens_run(d, f, str, n, arg, DK_SHORT2);
}

/* The other two widths. No rule in the nine languages IBM shipped names a
   string in either of them, which is why they were missing; a language whose
   alphabet does not fit in a byte would want them. */
int ins_tokens_l(delta_state *d, uint8_t f, const uint8_t *str, uint8_t n,
                 int32_t arg)
{
    return ins_tokens_run(d, f, str, n, arg, DK_SHORT);
}

int ins_tokens_lng(delta_state *d, uint8_t f, const uint8_t *str, uint8_t n,
                   int32_t arg)
{
    return ins_tokens_run(d, f, str, n, arg, DK_LONG);
}

/* Split a run of time in two at a given offset, on whichever side of the
   statement the sign says. A fresh sync goes in, the statement that keeps the
   remainder has its value reduced by the amount taken, and a new statement
   holding that amount is inserted beside it. Returns the new sync. */
int32_t vsplit_time(delta_state *d, uint8_t f, int32_t t, int32_t off)
{
    const delta_stmt *e = &vstmtbl[f];
    int32_t base = EVV_AT(delta_vars *, d->vars)->fence_base;
    int32_t neighbour;
    int32_t keep;
    int32_t l;
    int32_t r;
    int32_t amount;
    int32_t wide = 0;
    int16_t narrow = 0;
    delta_operand v;

    if (off < 0) {
        neighbour = *(int32_t *)(intptr_t)(t + 0xc + f * 4) & ~3;
        keep = (neighbour != 0
                && (*(int32_t *)(intptr_t)neighbour & 2) != 0) ? 0 : neighbour;
        r = t;
        l = EVV_REF(vins_sync(d, f, neighbour, t));
        t = l;
    } else {
        neighbour = *(int32_t *)(intptr_t)(t + (base + f) * 4) & ~3;
        keep = (neighbour != 0
                && (*(int32_t *)(intptr_t)neighbour & 2) != 0) ? 0 : neighbour;
        l = t;
        r = EVV_REF(vins_sync(d, f, t, neighbour));
        t = r;
    }

    if (t == 0)
        return 0;

    if (keep != 0) {
        void *p = TFLDS((void *)(intptr_t)keep);

        if (e->fields[0].kind == DK_LONG) {
            wide = *(int32_t *)e->get[0](p) - abs(off);
            vinitflds(d, f, TFLDS((void *)(intptr_t)keep), &wide);
        } else if (e->fields[0].kind == DK_SHORT2) {
            wide = *(int16_t *)e->get[0](p) - abs(off);
            narrow = (int16_t)wide;
            vinitflds(d, f, TFLDS((void *)(intptr_t)keep), &narrow);
        }
    }

    amount = abs(off);

    /* Anything but these two kinds leaves the operand as it stands, which in
       the original is whatever the frame held. No shipped type does. */
    if (e->fields[0].kind == DK_LONG) {
        v.kind = DK_LONG;
        v.ptr = &amount;
        v.flag = 0;
    } else if (e->fields[0].kind == DK_SHORT2) {
        v.kind = DK_SHORT2;
        narrow = (int16_t)amount;
        v.ptr = &narrow;
        v.flag = 0;
    }

    if (!vins_tok(d, f, l, r, &v))
        return 0;

    return t;
}

/* Settle a position and, if an offset was left over, cut the run there so
   the position lands on a sync of its own. */
int vsync_tv(delta_state *d, delta_tpos *p)
{
    int32_t r;
    int32_t off;

    if ((p->flags & 1) != 0)
        return 1;

    r = vnormalize(d, p);
    off = p->offset;

    if (r == 2)
        p->node = vsplit_time(d, p->field, p->node, off);

    p->flags = 1;
    return 1;
}

/* The same, but a position that fell just short of a run is dragged to its
   end rather than cutting anything. */
int vtmark_tv(delta_state *d, delta_tpos *p, uint8_t back)
{
    int32_t r;
    int32_t off;

    if ((p->flags & 1) != 0)
        return 1;

    r = vnormalize(d, p);
    off = p->offset;

    if (r == 2) {
        p->node = vsplit_time(d, p->field, p->node, off);
    } else if (r == 3) {
        if (back == 0)
            p->node = EVV_REF(rmost(d, p->field, (int32_t *)(intptr_t)p->node));
        else
            p->node = EVV_REF(lmost(d, p->field, (delta_node *)(intptr_t)p->node));
    }

    p->flags = 1;
    return 1;
}

/* What a rule writes when it deletes at a point: settle the register, cut
   the run there, and take the statement out. Either failing backtracks. */
void delete_1pt(delta_state *d, uint8_t f)
{
    if (!vsync_tv(d, &d->lpta) || !vdel_1pt(d, f, d->lpta.node, 1))
        forceErrorBacktrack(d);
}

/* Store where the left register points into a rule's variable. */
void lpta_storep(delta_state *d, delta_loc *loc)
{
    if (!vsync_tv(d, &d->lpta))
        forceErrorBacktrack(d);

    if (EVV_AT(delta_vars *, d->vars)->testing)
        save_var(d, loc);

    loc->value = d->lpta.node;
}

/* Open a range at a position: settle it, make sure the field has something
   in it, then put a sync just to the left and hand it back. */
int vrange_l(delta_state *d, delta_tpos *p, delta_tpos *out, int8_t f,
             uint8_t dup)
{
    if (!vtmark_tv(d, p, 1))
        return 0;
    if (!vdef_proj(d, p->node, (uint8_t)f))
        return 0;

    if (p->node == EVV_AT(delta_stack *, d->stack)->spine_l)
        return 0;

    out->node = EVV_REF(vins_sync(d, (uint8_t)f,
        *(int32_t *)(intptr_t)(p->node + 0xc + f * 4) & ~3, p->node));

    if (out->node == 0)
        return 0;

    if (dup != 0)
        dupsync(d, out->node, p->node, 1);

    out->flags = 1;
    return 1;
}

/* And the same at the other end. */
int vrange_r(delta_state *d, delta_tpos *p, delta_tpos *out, int8_t f,
             uint8_t dup)
{
    if (!vtmark_tv(d, p, 0))
        return 0;
    if (!vdef_proj(d, p->node, (uint8_t)f))
        return 0;

    if (p->node == EVV_AT(delta_stack *, d->stack)->spine_r)
        return 0;

    out->node = EVV_REF(vins_sync(d, (uint8_t)f, p->node,
        *(int32_t *)(intptr_t)
        (p->node + (EVV_AT(delta_vars *, d->vars)->fence_base + f) * 4) & ~3));

    if (out->node == 0)
        return 0;

    if (dup != 0)
        dupsync(d, out->node, p->node, 0);

    out->flags = 1;
    return 1;
}

/* What a rule writes when it inserts at a point: open a range on one side of
   the left register, then let the language fill it. Either failing
   backtracks. */
void insert_l(delta_state *d, int8_t f, uint8_t n, const uint8_t *str,
              uint8_t dup)
{
    if (!vrange_l(d, &d->rpta, &d->lpta, f, dup)
        || !ins_tokens(d, f, str, n, 0))
        forceErrorBacktrack(d);
}

void insert_r(delta_state *d, int8_t f, uint8_t n, const uint8_t *str,
              uint8_t dup)
{
    if (!vrange_r(d, &d->lpta, &d->rpta, f, dup)
        || !ins_tokens(d, f, str, n, 0))
        forceErrorBacktrack(d);
}

/* Open a range between two positions.

   With no mode the two are compared and, if they differ, each is settled and
   given a place in the field. With one, the first position decides the shape:
   a leftover offset cuts the run there, an exact position that sits on a
   whole statement takes that statement's ends, and anything else has a sync
   put in beside it. The three modes name which ends are wanted. */
int vrange_2pt(delta_state *d, delta_tpos *a, delta_tpos *b, int8_t f,
               uint8_t mode)
{
    int32_t base = EVV_AT(delta_vars *, d->vars)->fence_base;
    int32_t r;

    if (mode == 0) {
        delta_tpos ca;
        delta_tpos cb;

        memcpy(&ca, a, sizeof(ca));
        memcpy(&cb, b, sizeof(cb));

        if (vcomp_pta(d, &ca, &cb))
            return 1;
        if (EVV_AT(delta_vars *, d->vars)->compared_equal == 0)
            return 1;

        if (!vtmark_tv(d, a, 0))
            return 1;
        if (!vtmark_tv(d, b, 1))
            return 1;

        if (!vdef_proj(d, a->node, (uint8_t)f))
            return 1;
        if (!vdef_proj(d, b->node, (uint8_t)f))
            return 1;

        return 0;
    }

    if ((*(int32_t *)(intptr_t)
         (a->node + (base + a->field) * 4) & 1) != 0
        && a->offset == 0)
        r = 3;
    else
        r = vnormalize(d, a);

    if (r == 2) {
        int32_t off = a->offset;

        a->node = vsplit_time(d, (uint8_t)a->field, a->node, off);
        b->node = EVV_REF(vins_sync(d, (uint8_t)a->field, a->node,
            *(int32_t *)(intptr_t)
            (a->node + (base + a->field) * 4) & ~3));

        if (b->node == 0)
            return 1;
    } else if (r == 3 || r == 4) {
        if (r == 3) {
            b->node = a->node;

            if (mode == 0xcd || mode == 0xce)
                a->node = EVV_REF(lmost(d, a->field, (delta_node *)(intptr_t)a->node));

            if (mode == 0xcd || mode == 0xcf)
                b->node = EVV_REF(rmost(d, a->field, (int32_t *)(intptr_t)b->node));

            /* Two different ends is already a range; the same one falls
               through to have a sync put in beside it. */
            if (a->node != b->node)
                goto settle;
        }

        if (mode == 0xce
            || (mode == 0xcd && a->node == EVV_AT(delta_stack *, d->stack)->spine_r)) {
            if (a->node == EVV_AT(delta_stack *, d->stack)->spine_l)
                return 1;

            b->node = a->node;
            a->node = EVV_REF(vins_sync(d, (uint8_t)a->field,
                *(int32_t *)(intptr_t)
                (a->node + 0xc + a->field * 4) & ~3, a->node));

            if (a->node == 0)
                return 1;
        } else {
            if (a->node == EVV_AT(delta_stack *, d->stack)->spine_r)
                return 1;

            b->node = EVV_REF(vins_sync(d, (uint8_t)a->field,
                a->node,
                *(int32_t *)(intptr_t)
                (a->node + (base + a->field) * 4) & ~3));

            if (b->node == 0)
                return 1;
        }
    } else {
        return 1;
    }

settle:
    a->flags = 1;
    b->flags = 1;

    if (!vdef_proj(d, a->node, (uint8_t)f))
        return 1;
    if (!vdef_proj(d, b->node, (uint8_t)f))
        return 1;

    return 0;
}

/* The two-point spellings: open a range between the registers, then fill,
   empty or mark it. */
int insert_2pt_s(delta_state *d, uint8_t f, uint8_t n, const uint8_t *str,
                 uint8_t mode)
{
    if (vrange_2pt(d, &d->lpta, &d->rpta, (int8_t)f, mode))
        return 1;

    ins_tokens_s(d, f, str, n, 0);
    return 0;
}

/* The insert a rule writes when the string is bytes and the range may have
   something else threaded through it: what visnonseq answers is handed to
   the language's own inserter, which is what tells it to keep whatever is
   there rather than lay the tokens down flat. */
int insert_2pt(delta_state *d, uint8_t f, uint8_t n, const uint8_t *str,
               uint8_t mode)
{
    int32_t arg;

    if (vrange_2pt(d, &d->lpta, &d->rpta, (int8_t)f, mode))
        return 1;

    arg = visnonseq(d, f, d->lpta.node, d->rpta.node);

    if (!ins_tokens(d, (int8_t)f, str, n, arg))
        return 0xf5;

    return 0;
}

/* Join the marks the two registers name. Both have to be marks, and the
   pair has to be one the machine allows; either way it is a fault rather
   than an answer, so the rule backtracks. */
int32_t merge(delta_state *d)
{
    if (!vsync_tv(d, &d->lpta) || !vsync_tv(d, &d->rpta))
        forceErrorBacktrack(d);

    if (!vmergable(d, d->lpta.node, d->rpta.node))
        forceErrorBacktrack(d);

    return vmerge(d, d->lpta.node, d->rpta.node);
}

int insert_2pt_l(delta_state *d, uint8_t f, uint8_t n, const uint8_t *str,
                 uint8_t mode)
{
    if (vrange_2pt(d, &d->lpta, &d->rpta, (int8_t)f, mode))
        return 1;

    ins_tokens_l(d, f, str, n, 0);
    return 0;
}

int insert_2pt_lng(delta_state *d, uint8_t f, uint8_t n, const uint8_t *str,
                   uint8_t mode)
{
    if (vrange_2pt(d, &d->lpta, &d->rpta, (int8_t)f, mode))
        return 1;

    ins_tokens_lng(d, f, str, n, 0);
    return 0;
}

int insert_2pt_i(delta_state *d, uint8_t f, uint8_t n, const uint8_t *str,
                 uint8_t mode)
{
    if (vrange_2pt(d, &d->lpta, &d->rpta, (int8_t)f, mode))
        return 1;

    ins_tokens_i(d, f, str, n, 0);
    return 0;
}

int delete_2pt(delta_state *d, uint8_t f, uint8_t mode)
{
    if (vrange_2pt(d, &d->lpta, &d->rpta, (int8_t)f, mode))
        return 1;

    vdel_2pt(d, f, d->lpta.node, d->rpta.node);
    return 0;
}

/* Write one byte into a field of every statement in the range. As in vmark,
   the address handed on is of the argument itself. */
int mark_s(delta_state *d, uint8_t f, uint8_t fld, uint8_t value, uint8_t mode)
{
    if (vrange_2pt(d, &d->lpta, &d->rpta, (int8_t)f, mode))
        return 1;

    if (vstmtbl[f].fields[fld].kind == DK_UBYTE)
        vmark(d, f, fld, d->lpta.node, d->rpta.node, &value);

    return 0;
}

/* Write a rule's variable into a field of every statement in the range.

   The original marks once when the value's kind already matches the field,
   and then tests the kind again and marks a second time for every kind it
   knows; an exact match therefore marks twice. It is writing the same bytes
   both times. */
int mark_v(delta_state *d, uint8_t f, uint8_t fld, delta_loc *loc,
           uint8_t mode)
{
    delta_operand v;

    if (vrange_2pt(d, &d->lpta, &d->rpta, (int8_t)f, mode)) {
        reset_field(loc);
        return 1;
    }

    vinitloc_new(d, &v, loc);

    if (v.kind == vstmtbl[f].fields[fld].kind) {
        vmark(d, f, fld, d->lpta.node, d->rpta.node, v.ptr);

        if (v.kind == DK_SYNC || (v.kind > -5 && v.kind < 0))
            vmark(d, f, fld, d->lpta.node, d->rpta.node, v.ptr);
    }

    reset_field(loc);
    return 0;
}

/* The two index notifications Japanese asks for, and nothing else does. Each
   is a slot in the block the layers above share: if a function has been put
   there it is called with whatever was left beside it, and if not there is
   nothing to do. Our public interface has no way to put one there, so both are
   quiet -- which is what the original does for a caller that registered
   neither, rather than something we have decided. Written because Japanese's
   rules name them and a language cannot be built without them. */
int32_t wordIndexCallback(delta_state *d, const delta_loc *loc)
{
    void *fn = ELOQ_CB(d, 0x1c);

    if (fn != 0)
        ((void (*)(int32_t, void *))fn)((int32_t)loc->field,
                                        ELOQ_CB(d, 0x20));
    return 0;
}

int32_t userIndexCallback(delta_state *d)
{
    void *fn = ELOQ_CB(d, 0x24);

    if (fn != 0)
        ((void (*)(void *))fn)(ELOQ_CB(d, 0x28));
    return 0;
}

/* The same as insert_2ptv, over a range taken to the right rather than between
   two points. Only Canadian French reaches it, and vrange_r answers the other
   way round from vrange_2pt: nought is the failure there, so the test reads
   inverted beside its neighbour and is right. */
int insert_rv(delta_state *d, uint8_t f, delta_loc *loc, uint8_t dup)
{
    delta_operand a;
    delta_operand b;

    if (!vrange_r(d, &d->lpta, &d->rpta, (int8_t)f, dup))
        forceErrorBacktrack(d);

    if (loc->kind < 0 && loc->kind != STMTYP((int8_t)f)) {
        a.kind = STMTYP((int8_t)f);

        switch (a.kind) {
        case DK_UBYTE:  a.ptr = &EVV_AT(delta_vars *, d->vars)->scratch_b; break;
        case DK_LONG:   a.ptr = &EVV_AT(delta_vars *, d->vars)->scratch_l; break;
        case DK_SHORT2:
        case DK_SHORT:  a.ptr = &EVV_AT(delta_vars *, d->vars)->scratch_s; break;
        default:        forceErrorBacktrack(d); break;
        }

        a.flag = vstmtbl[f].fields[0].flag;

        vinitloc_new(d, &b, loc);
        vassign(d, &a, &b);

        if (!vins_tok(d, f, d->lpta.node, d->rpta.node, &a))
            forceErrorBacktrack(d);
    } else {
        vinitloc_new(d, &b, loc);

        if (!vins_tok(d, f, d->lpta.node, d->rpta.node, &b))
            forceErrorBacktrack(d);
    }

    reset_field(loc);
    return 0;
}

/* Insert a rule's variable as a statement in the range. A value of a kind the
   statement does not use is converted through a scratch cell first. */
int insert_2ptv(delta_state *d, uint8_t f, delta_loc *loc, uint8_t mode)
{
    delta_operand a;
    delta_operand b;

    if (vrange_2pt(d, &d->lpta, &d->rpta, (int8_t)f, mode)) {
        reset_field(loc);
        return 1;
    }

    if (loc->kind < 0 && loc->kind != STMTYP((int8_t)f)) {
        a.kind = STMTYP((int8_t)f);

        switch (a.kind) {
        case DK_UBYTE:  a.ptr = &EVV_AT(delta_vars *, d->vars)->scratch_b; break;
        case DK_LONG:   a.ptr = &EVV_AT(delta_vars *, d->vars)->scratch_l; break;
        case DK_SHORT2:
        case DK_SHORT:  a.ptr = &EVV_AT(delta_vars *, d->vars)->scratch_s; break;
        default:        break;
        }

        a.flag = vstmtbl[f].fields[0].flag;

        vinitloc_new(d, &b, loc);
        vassign(d, &a, &b);

        if (!vins_tok(d, f, d->lpta.node, d->rpta.node, &a)) {
            forceErrorBacktrack(d);
            return 1;
        }
    } else {
        vinitloc_new(d, &b, loc);

        if (!vins_tok(d, f, d->lpta.node, d->rpta.node, &b)) {
            forceErrorBacktrack(d);
            return 1;
        }
    }

    reset_field(loc);
    return 0;
}

/* Put the spine back to an empty one: the two ends carry every field and
   point at each other, and with the flag set each field is given the
   language's starting statement again.

   The word it clears in each statement table entry is the language's own
   data, which the runtime writes to here and nowhere else. */
void deltaReinit(delta_state *d, int32_t full)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);
    int32_t base = EVV_AT(delta_vars *, d->vars)->fence_base;
    uint8_t i;

    CLRONESTM((delta_node *)(intptr_t)s->spine_l);
    CLRONESTM((delta_node *)(intptr_t)s->spine_r);

    for (i = 0; i < d->nstmts; i++) {
        ((delta_stmt *)(uintptr_t)vstmtbl)[i].unknown_1c = 0;

        *(int32_t *)(intptr_t)(s->spine_l + (base + i) * 4) |= 1;
        *(int32_t *)(intptr_t)(s->spine_r + (base + i) * 4) |= 1;

        *(int32_t *)(intptr_t)(s->spine_l + (base + i) * 4) =
            (*(int32_t *)(intptr_t)(s->spine_l + (base + i) * 4) & 3)
            | s->spine_r;
        *(int32_t *)(intptr_t)(s->spine_r + 0xc + i * 4) =
            (*(int32_t *)(intptr_t)(s->spine_r + 0xc + i * 4) & 3)
            | s->spine_l;

        if (full != 0)
            vinit_stm(d, (int8_t)i);
    }

    SETSPINER(d, (int32_t *)(intptr_t)s->spine_l, s->spine_r);
    SETSPINEL((delta_node *)(intptr_t)s->spine_r, s->spine_l);
}

/* Start a delta off. With no list every field is initialised; with a list as
   long as there are fields the whole thing is thrown away and rebuilt; with
   a shorter one only the fields it names. */
void initdelta(delta_state *d, uint8_t n, const uint8_t *list)
{
    uint8_t i;

    if (n == 0) {
        for (i = 0; i < d->nstmts; i++) {
            if (!vinit_stm(d, (int8_t)i))
                forceErrorBacktrack(d);
        }
    } else if (n == d->nstmts) {
        freeDeltaHeapTo(d, (uint8_t *)(intptr_t)EVV_AT(delta_stack *, d->stack)->spine_r, 0);
        deltaReinit(d, 1);
    } else {
        for (i = 0; i < n; i++) {
            if (!vinit_stm(d, (int8_t)list[i]))
                forceErrorBacktrack(d);
        }
    }

    vscaninit(d);
}

int init_ptr_active_record(delta_state *d)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);

    if (!push_ptr(d, v->active_record))
        return 0;

    v->active_record = v->ptr_count;
    return 1;
}

/* Enter a rule: save everything a backtrack would have to undo into the
   record the rule keeps in its own frame, push a marker on the backtracking
   stack pointing at it, and swap in the rule's own fence arrays. */
int ventproc(delta_state *d, delta_actrec *rec, uint8_t *index,
             uint8_t *chars, uint8_t *marks, void *jb)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);
    delta_stack *s = EVV_AT(delta_stack *, d->stack);
    uint8_t *slot;

    d->unknown_3c = 0;

    if (rec == NULL || !init_ptr_active_record(d))
        return 1;

    rec->unknown_00 = v->running;
    memcpy(rec->tags, &v->loop_tag, 8);
    rec->testing = (uint8_t)v->testing;
    rec->back = EVV_REF(EVV_AT(uint8_t *, v->back));
    rec->top = EVV_REF(EVV_AT(uint8_t *, s->top));
    rec->vbot = EVV_REF(getDeltaStackVBot(d));
    rec->fence_count = (uint8_t)v->fence_count;
    rec->err_jmp = EVV_REF(EVV_AT(void *, v->err_jmp));
    memcpy(rec->scan, &v->scan_ptr, 8);
    memcpy(&rec->lpta, &d->lpta, sizeof(rec->lpta));
    memcpy(&rec->rpta, &d->rpta, sizeof(rec->rpta));
    rec->compared_equal = (uint8_t)v->compared_equal;
    rec->names_depth = (uint8_t)s->names_depth;

    s->top -= s->size_a8;
    slot = EVV_AT(uint8_t *, s->top);
    s->limit -= s->size_a8;

    slot[0] = 7;
    *(evv_ref *)(slot + 4) = EVV_REF(rec);

    *(evv_ref *)(slot + 0xc) = d->fence_chars;
    d->fence_chars = EVV_REF(chars);
    *(evv_ref *)(slot + 8) = d->fence_index;
    d->fence_index = EVV_REF(index);
    *(evv_ref *)(slot + 0x10) = d->fence_marks;
    d->fence_marks = EVV_REF(marks);

    v->back = EVV_REF(EVV_AT(uint8_t *, s->top));
    v->err_jmp = EVV_REF(jb);
    return 0;
}

/* Leave a rule: pop its marker, put back everything the record holds, and
   report whether the rule left an error behind. The tag it is handed is not
   read. */
int vretproc(delta_state *d, int32_t tag)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);
    delta_stack *s = EVV_AT(delta_stack *, d->stack);
    int32_t exhausted = 0;
    int32_t r;
    uint8_t *frame;
    delta_actrec *rec;

    (void)tag;

    r = ret_ptr_active_record(d) ? 0 : 1;

    /* Nothing left below this frame: a rule reaching its end there has run
       the backtracking stack out, which is what the code says. */
    if (emptyDeltaStack(d))
        exhausted = 1;

    frame = popDeltaStackFrame(d, EVV_AT(uint8_t *, v->back));
    rec = EVV_AT(delta_actrec *, *(evv_ref *)(frame + 4));

    frame = EVV_AT(uint8_t *, s->top);
    d->fence_chars = *(evv_ref *)(frame + 0xc);
    d->fence_index = *(evv_ref *)(frame + 8);
    d->fence_marks = *(evv_ref *)(frame + 0x10);

    v->running = rec->unknown_00;
    memcpy(&v->loop_tag, rec->tags, 8);
    v->testing = (int8_t)rec->testing;
    v->back = EVV_REF(EVV_AT(uint8_t *, rec->back));
    freeDeltaStackTo(d, EVV_AT(uint8_t *, rec->top));
    setDeltaStackVBot(d, EVV_AT(void *, rec->vbot));
    v->fence_count = (int8_t)rec->fence_count;
    v->err_jmp = EVV_REF(EVV_AT(void *, rec->err_jmp));
    memcpy(&v->scan_ptr, rec->scan, 8);
    memcpy(&d->lpta, &rec->lpta, sizeof(d->lpta));
    memcpy(&d->rpta, &rec->rpta, sizeof(d->rpta));
    v->compared_equal = (int8_t)rec->compared_equal;
    s->names_depth = (int8_t)rec->names_depth;
    v->unknown_11e8 = 0;

    if (exhausted) {
        EVV_AT(delta_owner *, d->owner)->code = 0xea;
        r = deltaErrorThrown(d) ? 1 : 0;
    } else if (deltaErrorThrown(d)) {
        forceErrorBacktrack(d);
    }

    return r;
}

int succeed(delta_state *d)
{
    return vretproc(d, 1);
}

/* Put a small integer into a rule's variable. A variable of one of the sized
   kinds takes it directly; one the language declares goes through vassign.
   A kind below those is a fault and backtracks. */
/* The same as move_i with the value taken whole: what it writes into a
   location the language declared is a long rather than a short, and the
   value it puts into the two kinds it handles outright is not narrowed on
   the way. */
void move_lng(delta_state *d, delta_loc *loc, int32_t value)
{
    delta_operand a;
    delta_operand b;

    if (EVV_AT(delta_vars *, d->vars)->testing)
        save_var(d, loc);

    if (loc->kind == DK_SYNC || loc->kind == DK_LONG) {
        loc->value = value;
        return;
    }

    if (loc->kind == DK_SHORT2) {
        loc->field = (int16_t)value;
        return;
    }

    if (loc->kind < 0) {
        forceErrorBacktrack(d);
        return;
    }

    a.kind = DK_LONG;
    a.ptr = &value;
    a.flag = 0;

    vinitloc_new(d, &b, loc);
    vassign(d, &b, &a);
    reset_field(loc);
}

void move_i(delta_state *d, delta_loc *loc, int16_t value)
{
    delta_operand a;
    delta_operand b;

    if (EVV_AT(delta_vars *, d->vars)->testing)
        save_var(d, loc);

    if (loc->kind == DK_SYNC || loc->kind == DK_LONG) {
        loc->value = value;
        return;
    }

    if (loc->kind == DK_SHORT2) {
        loc->field = value;
        return;
    }

    if (loc->kind < 0) {
        forceErrorBacktrack(d);
        return;
    }

    a.kind = DK_SHORT2;
    a.ptr = &value;
    a.flag = 0;

    vinitloc_new(d, &b, loc);
    vassign(d, &b, &a);
    reset_field(loc);
}

/* Nothing at all: the original is a frame and a return. */
void pause(delta_state *d)
{
    (void)d;
}

/* Hand the dictionary's next step back through the slot a rule reads after
   a statement changes. */
int actd_goto(delta_state *d)
{
    d->unknown_3c = EVV_AT(delta_vars *, d->vars)->unknown_11ec;
    return 2;
}

/* Push a long on the number stack. The operand points at the argument the
   caller is still holding, which is why the value arrives by address. */
void npush_lng(delta_state *d, int32_t v)
{
    delta_operand op;

    op.ptr = &v;
    op.kind = DK_LONG;
    op.flag = 0;
    vnspush(d, &op);
}

/* The fourth width. The name stack carries the type beside the value, so
   these four differ in nothing but which type they say. */
void npush_l(delta_state *d, int32_t x)
{
    delta_operand v;

    v.ptr = &x;
    v.kind = DK_SHORT;
    v.flag = 0;
    vnspush(d, &v);
}

/* Take the top two off the name stack and compare them, the later push
   being the left operand. */
void ncompare(delta_state *d)
{
    delta_operand a;
    delta_operand b;

    vnspop(d, &a);
    vnspop(d, &b);
    vcompare(d, &a, &b);
}

/* Backtrack, and backtrack out of an alternative. The first says only that
   it happened; the second leaves a word behind that the rule's return
   clears. */
int back(delta_state *d)
{
    (void)d;
    return 1;
}

int back_nboa(delta_state *d)
{
    EVV_AT(delta_vars *, d->vars)->unknown_11e8 = 1;
    return 1;
}

/* And push a variable, which leaves its field unselected afterwards. */
void npush_v(delta_state *d, delta_loc *loc)
{
    delta_operand op;

    vinitloc_new(d, &op, loc);
    vnspush(d, &op);
    reset_field(loc);
}

/* The same again under a second name, as the original has it. */
void npush_vf(delta_state *d, delta_loc *loc)
{
    delta_operand op;

    vinitloc_new(d, &op, loc);
    vnspush(d, &op);
    reset_field(loc);
}

/* Note a variable is about to be assigned, so a rule under test can put it
   back on the way out. */
void c_assvar(delta_state *d, delta_loc *loc)
{
    if (EVV_AT(delta_vars *, d->vars)->testing)
        save_var(d, loc);
    reset_field(loc);
}

/* Step the scan on by one, answering the other way round from vscanadv. */
int advance_strm(delta_state *d)
{
    return vscanadv(d, 0, 1) ? 0 : 1;
}

/* Which sync a pointer belongs to. Nothing belongs to nowhere. */
int32_t absoluteSyncNum(delta_state *d, uint8_t *p)
{
    if (p == NULL)
        return -1;

    return getDeltaHeapSegNumber(d, p, EVV_AT(delta_stack *, d->stack)->sync_size);
}

/* Start a while loop: remember where the test and the body are, drop the
   backtracking the last round left, and clear the stack's round marker. */
int while_iterate(delta_state *d, int16_t test_tag, int16_t loop_tag)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);

    v->loop_tag = loop_tag;
    v->test_tag = test_tag;
    clearDeltaStackBack(d);
    EVV_AT(delta_stack *, d->stack)->unknown_9c = 0;
    return 2;
}

/* Give the left pointer's statement a default projection. Either step can
   refuse, and a refusal backtracks. */
void proj_def(delta_state *d, uint8_t f)
{
    if (!vsync_tv(d, &d->lpta) || !vdef_proj(d, d->lpta.node, f))
        forceErrorBacktrack(d);
}

/* Move the right pointer one statement leftwards. */
void rpta_movel(delta_state *d, uint8_t f)
{
    if (!vmove_tv(d, &d->rpta) || d->rpta.node == 0)
        forceErrorBacktrack(d);

    d->rpta.node = EVV_REF(vmovel(d, (delta_node *)(intptr_t)d->rpta.node, f));
}

/* The same for the left pointer, but asking first rather than faulting: a
   mark in the way is an answer, not an error. */
int lpta_tstmovel(delta_state *d, uint8_t f)
{
    if (vtsttmark_tv(d, &d->lpta, 0) || d->lpta.node == 0)
        return 1;

    d->lpta.node = EVV_REF(vmovel(d, (delta_node *)(intptr_t)d->lpta.node, f));
    return 0;
}

/* Put the right pointer into a rule's variable. */
void rpta_storep(delta_state *d, delta_loc *loc)
{
    if (!vsync_tv(d, &d->rpta))
        forceErrorBacktrack(d);

    if (EVV_AT(delta_vars *, d->vars)->testing)
        save_var(d, loc);

    loc->value = d->rpta.node;
}

/* Point the left pointer at a field of a statement a variable names. The
   long kind holds the statement, the short kind holds it in the field slot,
   and anything else is a fault. */
void lpta_loadv(delta_state *d, uint8_t f, const delta_loc *loc)
{
    d->lpta.flags = 2;
    d->lpta.field = (int8_t)f;

    if (loc->kind == DK_LONG)
        d->lpta.offset = loc->value;
    else if (loc->kind == DK_SHORT2)
        d->lpta.offset = loc->field;
    else
        forceErrorBacktrack(d);
}

/* The same as lpta_loadv, but the value comes as an immediate rather than out
   of a location, so the kind has to be asked of the statement table instead of
   read off the location. No English rule reaches it, which is why it was
   missing until the Spanishes, the Frenches and Italian were lifted and every
   one of them wanted it. Unlike lpta_loadv it does not fault on a kind it does
   not handle: the original leaves the offset alone and returns, and that is
   kept. */
void lpta_loadi(delta_state *d, uint8_t f, int32_t v)
{
    d->lpta.flags = 2;
    d->lpta.field = (int8_t)f;

    switch (STMTYP(d->lpta.field)) {
    case -4:
    case -3:
        d->lpta.offset = (int16_t)v;
        break;
    default:
        break;
    }
}

/* The right register's twin of it, and of the two loads beside it. Three
   things about these are the original's and are kept.
 *
 * Each asks the statement table about the *left* register's field and not its
 * own -- 0x44 where 0x54 was meant -- which is a slip in the original that
 * cannot show: both arms of the switch write the same thing, so all the wrong
 * question can do is decide whether the offset is written at all.
 *
 * The long forms take the immediate whole where the short ones narrow it to
 * sixteen bits first, and neither faults on a kind it does not handle.
 *
 * No rule in the nine languages IBM shipped calls any of these, which is why
 * they were missing; a rule of ours that walks rightwards wants them. */
void rpta_loadi(delta_state *d, uint8_t f, int32_t v)
{
    d->rpta.flags = 2;
    d->rpta.field = (int8_t)f;

    switch (STMTYP(d->lpta.field)) {
    case -4:
    case -3:
        d->rpta.offset = (int16_t)v;
        break;
    default:
        break;
    }
}

void lpta_loadlng(delta_state *d, uint8_t f, int32_t v)
{
    d->lpta.flags = 2;
    d->lpta.field = (int8_t)f;

    switch (STMTYP(d->lpta.field)) {
    case -4:
    case -3:
        d->lpta.offset = v;
        break;
    default:
        break;
    }
}

void rpta_loadl(delta_state *d, uint8_t f, int32_t v)
{
    d->rpta.flags = 2;
    d->rpta.field = (int8_t)f;

    switch (STMTYP(d->lpta.field)) {
    case -4:
    case -3:
        d->rpta.offset = v;
        break;
    default:
        break;
    }
}

void rpta_loadv(delta_state *d, uint8_t f, const delta_loc *loc)
{
    d->rpta.flags = 2;
    d->rpta.field = (int8_t)f;

    if (loc->kind == DK_LONG)
        d->rpta.offset = loc->value;
    else if (loc->kind == DK_SHORT2)
        d->rpta.offset = loc->field;
    else
        forceErrorBacktrack(d);
}

/* Where a register is told to sit: at the left end of a run, or the right.
   Nothing is read and nothing moves -- the flag is what a later move reads. */
void lpta_leftmost(delta_state *d, uint8_t f)
{
    d->lpta.flags = 6;
    d->lpta.field = (int8_t)f;
}

void rpta_leftmost(delta_state *d, uint8_t f)
{
    d->rpta.flags = 6;
    d->rpta.field = (int8_t)f;
}

void lpta_rightmost(delta_state *d, uint8_t f)
{
    d->lpta.flags = 0xa;
    d->lpta.field = (int8_t)f;
}

void rpta_rightmost(delta_state *d, uint8_t f)
{
    d->rpta.flags = 0xa;
    d->rpta.field = (int8_t)f;
}

/* Set a timing variable. The two names are the same code in the original. */
void settvar_i(delta_state *d, delta_loc *loc, int32_t v)
{
    if (EVV_AT(delta_vars *, d->vars)->testing)
        save_var(d, loc);

    vinitflds(d, (uint8_t)*(const int8_t *)loc, &loc->value, &v);
}

void settvar_s(delta_state *d, delta_loc *loc, int32_t v)
{
    if (EVV_AT(delta_vars *, d->vars)->testing)
        save_var(d, loc);

    vinitflds(d, (uint8_t)*(const int8_t *)loc, &loc->value, &v);
}

/* And two more names for it. The original compiles the same body into four
   entry points -- one per width a rule may name the value in -- and none of
   them looks at the width, so all four are this. */
void settvar_l(delta_state *d, delta_loc *loc, int32_t v)
{
    if (EVV_AT(delta_vars *, d->vars)->testing)
        save_var(d, loc);

    vinitflds(d, (uint8_t)*(const int8_t *)loc, &loc->value, &v);
}

void settvar_lng(delta_state *d, delta_loc *loc, int32_t v)
{
    if (EVV_AT(delta_vars *, d->vars)->testing)
        save_var(d, loc);

    vinitflds(d, (uint8_t)*(const int8_t *)loc, &loc->value, &v);
}

/* The same again where the value comes out of another location rather than
   as a constant: the source is opened as an operand and what it points at is
   what goes in. The source's field is reset afterwards and the target's is
   not, which is the original's doing. */
void settvar_v(delta_state *d, delta_loc *loc, delta_loc *src)
{
    delta_operand v;

    if (EVV_AT(delta_vars *, d->vars)->testing)
        save_var(d, loc);

    vinitloc_new(d, &v, src);
    vinitflds(d, (uint8_t)*(const int8_t *)loc, &loc->value, v.ptr);
    reset_field(src);
}

/* What the machine calls when an assignment was allowed and when it was
   refused. They are the same: put the field back and say nothing. */
void assok(delta_state *d, delta_loc *loc)
{
    (void)d;
    reset_field(loc);
}

void noass(delta_state *d, delta_loc *loc)
{
    (void)d;
    reset_field(loc);
}

/* Two that do nothing at all. Their bodies read no argument, so how many a
   rule passes cannot be recovered from the original; the state is given
   because every other primitive takes it, and the convention lets a caller
   push more than is read. */
void chkvars(delta_state *d)
{
    (void)d;
}

void chkokass(delta_state *d)
{
    (void)d;
}

/* Is a number negative? Only the two number kinds can be; anything else
   answers no rather than faulting. */
int vnegative(delta_state *d, const delta_operand *v)
{
    (void)d;

    if (v->kind == DK_LONG)
        return *(const int32_t *)v->ptr < 0;

    if (v->kind == DK_SHORT2)
        return *(const int16_t *)v->ptr < 0;

    return 0;
}

/* Compare two timing variables, leaving the answer where a test reads it. */
void compare_tvars(delta_state *d, delta_loc *a, delta_loc *b)
{
    delta_operand x, y;

    vinitloc_new(d, &x, a);
    vinitloc_new(d, &y, b);
    vcompare(d, &x, &y);
    reset_field(a);
    reset_field(b);
}

/* The six comparisons a rule can branch on. Each pops two numbers, compares
   them, and answers whether the test FAILED, which is the sense every rule
   is written against.

   The pop leaves the operand's pointer alone for a kind it does not know,
   and the comparison then reads through it. That is the original's own
   hazard and it is left as it stands; the number stack only ever holds the
   kinds the pop does know. */
static int compare_two(delta_state *d)
{
    delta_operand a, b;

    vnspop(d, &a);
    vnspop(d, &b);
    vcompare(d, &a, &b);
    return EVV_AT(delta_vars *, d->vars)->compared_equal;
}

int if_testeq(delta_state *d)
{
    return compare_two(d) != 0;
}

int if_testneq(delta_state *d)
{
    return compare_two(d) == 0;
}

int if_testlt(delta_state *d)
{
    return compare_two(d) != -1;
}

int if_testle(delta_state *d)
{
    return compare_two(d) == 1;
}

int if_testgt(delta_state *d)
{
    return compare_two(d) != 1;
}

int if_testge(delta_state *d)
{
    return compare_two(d) == -1;
}

/* Take a number off the stack and into a rule's variable. */
void npop(delta_state *d, delta_loc *loc)
{
    delta_operand v, dst;

    vnspop(d, &v);

    if (EVV_AT(delta_vars *, d->vars)->testing)
        save_var(d, loc);

    vinitloc_new(d, &dst, loc);
    vassign(d, &dst, &v);
    reset_field(loc);
}

/* Compare the number on top of the stack against a byte the rule names.
   A byte on the stack is compared here; anything else goes the long way
   round through the general comparison. */
void ncompare_s(delta_state *d, uint8_t c)
{
    delta_operand v;
    delta_operand w;

    vnspop(d, &v);

    if (v.kind == DK_UBYTE) {
        unsigned a = *(const uint8_t *)v.ptr;

        if (a == c)
            EVV_AT(delta_vars *, d->vars)->compared_equal = 0;
        else if ((int)a > (int)c)
            EVV_AT(delta_vars *, d->vars)->compared_equal = 1;
        else
            EVV_AT(delta_vars *, d->vars)->compared_equal = -1;
        return;
    }

    w.kind = DK_UBYTE;
    w.ptr = &c;
    w.flag = 0;
    vcompare(d, &w, &v);
}

/* Has a forall loop reached its end? Equal means yes, and then the rule is
   told where its test lives. */
int forall_to_test(delta_state *d, delta_loc *a, delta_loc *b)
{
    delta_operand x, y;

    vinitloc_new(d, &x, a);
    vinitloc_new(d, &y, b);
    vcompare(d, &x, &y);
    reset_field(a);
    reset_field(b);

    if (EVV_AT(delta_vars *, d->vars)->compared_equal != 0)
        return 0;

    d->unknown_3c = EVV_AT(delta_vars *, d->vars)->test_tag;
    return 2;
}

/* Mark a field across the range the two pointers span. Only a short field
   can carry a mark, and a range the pointers cannot span is a refusal. */
int mark_i(delta_state *d, uint8_t st, uint8_t fld, const void *v,
           uint8_t mode)
{
    if (vrange_2pt(d, &d->lpta, &d->rpta, (int8_t)st, mode))
        return 1;

    if (vstmtbl[st].fields[fld].kind == DK_SHORT2)
        vmark(d, st, fld, d->lpta.node, d->rpta.node, &v);

    return 0;
}

/* The same mark in the other two widths. Each refuses a field whose kind is
   not the one it is named for, and no rule in the nine languages IBM shipped
   names either -- their fields are bytes and short2s. */
int mark_l(delta_state *d, uint8_t st, uint8_t fld, const void *v,
           uint8_t mode)
{
    if (vrange_2pt(d, &d->lpta, &d->rpta, (int8_t)st, mode))
        return 1;

    if (vstmtbl[st].fields[fld].kind == DK_SHORT)
        vmark(d, st, fld, d->lpta.node, d->rpta.node, &v);

    return 0;
}

int mark_lng(delta_state *d, uint8_t st, uint8_t fld, const void *v,
             uint8_t mode)
{
    if (vrange_2pt(d, &d->lpta, &d->rpta, (int8_t)st, mode))
        return 1;

    if (vstmtbl[st].fields[fld].kind == DK_LONG)
        vmark(d, st, fld, d->lpta.node, d->rpta.node, &v);

    return 0;
}

/* Where a context begins and where it ends, which is one call into the
   context table with the direction said as a number. The two differ in that
   number and in nothing else. */
void SETCTXL(delta_state *d, int32_t *table, uint8_t idx, int32_t bits)
{
    vsetsc(d, 1, 1, table, idx, bits);
}

void SETCTXR(delta_state *d, int32_t *table, uint8_t idx, int32_t bits)
{
    vsetsc(d, 0, 1, table, idx, bits);
}

/* Can a timing pointer take a context? One already flagged can, and
   otherwise it depends on what normalising it makes of it. */
int vctxt_tv(delta_state *d, delta_tpos *p)
{
    if (p->flags & 1)
        return 1;

    switch (vnormalize(d, p)) {
    case 2:
        return 1;
    case 3:
    case 4:
        p->flags = 1;
        return 1;
    default:
        return 0;
    }
}

/* Compare two timing variables and answer whether they came out equal. */
int testeq_tvars(delta_state *d, delta_loc *a, delta_loc *b)
{
    compare_tvars(d, a, b);
    return testeq(d);
}

int testneq_tvars(delta_state *d, delta_loc *a, delta_loc *b)
{
    compare_tvars(d, a, b);
    return testneq(d);
}

/* A variable against a constant: push both and run the ordinary test. */
int if_testeq_v_i(delta_state *d, delta_loc *loc, int32_t x)
{
    npush_v(d, loc);
    npush_i(d, x);
    return if_testeq(d);
}

int if_testneq_v_i(delta_state *d, delta_loc *loc, int32_t x)
{
    npush_v(d, loc);
    npush_i(d, x);
    return if_testneq(d);
}

int if_testlt_v_i(delta_state *d, delta_loc *loc, int32_t x)
{
    npush_v(d, loc);
    npush_i(d, x);
    return if_testlt(d);
}

int if_testgt_v_i(delta_state *d, delta_loc *loc, int32_t x)
{
    npush_v(d, loc);
    npush_i(d, x);
    return if_testgt(d);
}

int if_testge_v_i(delta_state *d, delta_loc *loc, int32_t x)
{
    npush_v(d, loc);
    npush_i(d, x);
    return if_testge(d);
}

int if_testle_v_i(delta_state *d, delta_loc *loc, int32_t x)
{
    npush_v(d, loc);
    npush_i(d, x);
    return if_testle(d);
}

/* The same six with the constant taken whole rather than narrowed to sixteen
   bits, which is the only thing that distinguishes them. No rule in the
   languages IBM shipped names a constant that needs the width. */
int if_testeq_v_lng(delta_state *d, delta_loc *loc, int32_t x)
{
    npush_v(d, loc);
    npush_lng(d, x);
    return if_testeq(d);
}

int if_testneq_v_lng(delta_state *d, delta_loc *loc, int32_t x)
{
    npush_v(d, loc);
    npush_lng(d, x);
    return if_testneq(d);
}

int if_testlt_v_lng(delta_state *d, delta_loc *loc, int32_t x)
{
    npush_v(d, loc);
    npush_lng(d, x);
    return if_testlt(d);
}

int if_testgt_v_lng(delta_state *d, delta_loc *loc, int32_t x)
{
    npush_v(d, loc);
    npush_lng(d, x);
    return if_testgt(d);
}

int if_testge_v_lng(delta_state *d, delta_loc *loc, int32_t x)
{
    npush_v(d, loc);
    npush_lng(d, x);
    return if_testge(d);
}

int if_testle_v_lng(delta_state *d, delta_loc *loc, int32_t x)
{
    npush_v(d, loc);
    npush_lng(d, x);
    return if_testle(d);
}

/* Give a run of statements their default projections, one per letter of the
   spelling, each starting from the same token. */
void proj_def_mult(delta_state *d, uint8_t n, const uint8_t *str,
                   const delta_token *p)
{
    int32_t i;

    for (i = 0; i < (int32_t)n; i++) {
        lpta_loadp(d, p);
        proj_def(d, str[i]);
    }
}

/* Take a pointer to its context. A statement already carrying the field
   stays where it is; otherwise the pointer moves to the one that has it,
   looking to the left or to the right. */
static void pta_ctxt(delta_state *d, delta_tpos *p, uint8_t f, int32_t back)
{
    const int32_t *node;

    if (!vctxt_tv(d, p) || p->node == 0)
        forceErrorBacktrack(d);

    node = (const int32_t *)(intptr_t)p->node;
    if (node[EVV_AT(delta_vars *, d->vars)->fence_base + f] & 1)
        return;

    p->node = vgetsc(d, back, 1, p->node, f);
}

void lpta_ctxtl(delta_state *d, uint8_t f)
{
    pta_ctxt(d, &d->lpta, f, 1);
}

void lpta_ctxtr(delta_state *d, uint8_t f)
{
    pta_ctxt(d, &d->lpta, f, 0);
}

void rpta_ctxtl(delta_state *d, uint8_t f)
{
    pta_ctxt(d, &d->rpta, f, 1);
}

void rpta_ctxtr(delta_state *d, uint8_t f)
{
    pta_ctxt(d, &d->rpta, f, 0);
}

/* Words a minute from the engine's own speed number. */
int calcETI2WPM(delta_state *d, const delta_loc *in, delta_loc *out)
{
    int16_t v = in->field;

    (void)d;
    if (v < 0)
        v = 0;
    if (v >= 0xfb)
        v = 0xfa;

    out->field = delta_ETI2WPM_Table[v];
    return 0;
}

/* The pitch a voice sits at, from the baseline setting. */
int calcMidline(delta_state *d, const delta_loc *in, delta_loc *out)
{
    int16_t v = in->field;

    (void)d;
    if (v < 0)
        v = 0;
    if (v > 0x64)
        v = 0x64;

    /* The original checks the address of the field it is about to write,
       not the record, so the check can never fire. It is kept as it is. */
    if ((uintptr_t)&out->field != 0)
        out->field = delta_MidlineVals[v];

    return 0;
}

/* How much to stretch or squeeze a duration, from the speed setting. */
int calcSpeedFactori(delta_state *d, const delta_loc *in, delta_loc *out)
{
    int16_t v = in->field;
    int32_t n = (v < 0) ? 0 : v;

    (void)d;
    if (n > 0x96)
        n = 0x96;

    out->value = delta_SpeedTable[(int16_t)n];
    return 0;
}

/* Copy one rule variable into another. */
void copyvar(delta_state *d, delta_loc *a, delta_loc *b)
{
    delta_operand x, y;

    if (EVV_AT(delta_vars *, d->vars)->testing)
        save_var(d, a);

    vinitloc_new(d, &x, a);
    vinitloc_new(d, &y, b);
    vassign(d, &x, &y);

    if (a->kind >= 0)
        reset_field(a);
    if (b->kind >= 0)
        reset_field(b);
}

/* Step a forall loop leftwards. The scan runs forward through the spine and
   the loop carries on for as long as there is somewhere to go. */
int forall_adv_l(delta_state *d, int16_t tag, int16_t loop, int16_t bound,
                 uint8_t f, delta_token *tok)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);

    if (!for_loop_preamble(d, tag, loop, f, tok))
        return 1;

    v->scan_rev = 0;

    if (!vscanadv(d, 1, 0))
        return 0;

    clearDeltaStackBack(d);
    EVV_AT(delta_stack *, d->stack)->unknown_9c = 0;
    v->testing = 1;
    d->unknown_3c = bound;
    tok->value = v->scan_ptr;
    return 2;
}

/* The same rightwards. The original's two are the same function but for the
   direction it sets, and nothing in English ever calls this one; German
   does, which is how it came to be written. */
int forall_adv_r(delta_state *d, int16_t tag, int16_t loop, int16_t bound,
                 uint8_t f, delta_token *tok)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);

    if (!for_loop_preamble(d, tag, loop, f, tok))
        return 1;

    v->scan_rev = 1;

    if (!vscanadv(d, 1, 0))
        return 0;

    clearDeltaStackBack(d);
    EVV_AT(delta_stack *, d->stack)->unknown_9c = 0;
    v->testing = 1;
    d->unknown_3c = bound;
    tok->value = v->scan_ptr;
    return 2;
}

/* The same with an end to stop at. */
int forto_adv_l(delta_state *d, int16_t tag, int16_t loop, int16_t bound,
                uint8_t f, delta_token *tok, const delta_token *end)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);

    if (!for_loop_preamble(d, tag, loop, f, tok))
        return 1;

    v->scan_rev = 0;

    if (!vscanadv(d, 1, 0))
        return 0;
    if (v->scan_ptr == end->value)
        return 0;

    clearDeltaStackBack(d);
    EVV_AT(delta_stack *, d->stack)->unknown_9c = 0;
    v->testing = 1;
    d->unknown_3c = bound;
    tok->value = v->scan_ptr;
    return 2;
}

/* Has a counting loop reached its bound? Which way round the answer goes
   depends on the sign of the step. */
int for_test(delta_state *d, delta_loc *var, delta_loc *bound,
             delta_loc *step)
{
    delta_operand a, b, c;
    delta_vars *v = EVV_AT(delta_vars *, d->vars);

    v->testing = 0;

    vinitloc_new(d, &a, var);
    vinitloc_new(d, &b, bound);
    vinitloc_new(d, &c, step);
    vcompare(d, &a, &b);

    reset_field(var);
    reset_field(bound);
    reset_field(step);

    if (vnegative(d, &c)) {
        if (v->compared_equal != -1)
            return 0;
    } else if (v->compared_equal != 1) {
        return 0;
    }

    d->unknown_3c = v->test_tag;
    return 2;
}

/* Step a counting loop on and say whether it has another round in it. */
int for_adv(delta_state *d, int16_t test_tag, int16_t loop_tag,
            delta_loc *var, delta_loc *bound, delta_loc *step)
{
    delta_operand a, b, c;
    delta_vars *v = EVV_AT(delta_vars *, d->vars);

    v->loop_tag = loop_tag;
    v->test_tag = test_tag;
    v->testing = 0;

    vinitloc_new(d, &a, var);
    vinitloc_new(d, &c, step);
    vinitloc_new(d, &b, bound);

    vadd(d, &a, &c);
    vcompare(d, &a, &b);

    reset_field(var);
    reset_field(bound);
    reset_field(step);

    if (vnegative(d, &c))
        return (v->compared_equal == -1) ? 0 : 2;

    return (v->compared_equal == 1) ? 0 : 2;
}

/* Put the token the scan is standing on into a rule variable. The scan may
   be sitting on a marked link, in which case it is followed until a real
   one turns up or the walk runs out. */
int savetok(delta_state *d, delta_loc *loc)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);
    delta_operand dst, src;
    int32_t t;

    t = ((const int32_t *)(intptr_t)v->scan_ptr)[3 + v->scan_field] & -4;
    if (t == 0) {
        reset_field(loc);
        return 1;
    }

    EVV_WALK(w);

    while (t != 0 && (*(const int32_t *)(intptr_t)t & 2)) {
        EVV_WALKED(d, w);
        t = ((const int32_t *)(intptr_t)t)[3 + v->scan_field] & -4;
        if (t == 0) {
            reset_field(loc);
            return 1;
        }
    }

    vinitloc_new(d, &dst, loc);

    src.kind = v->scan_field;
    src.ptr = TFLDS((void *)(intptr_t)t);
    src.flag = 0;

    if (v->testing)
        vpush_var(d, &dst);

    vassign(d, &dst, &src);
    reset_field(loc);
    return 0;
}

/* Does this string spell a whole number? A sign is allowed only at the
   front, and everything after it has to be a digit. A lone sign passes,
   which is what the original does.

   The original hands a sign-extended character to the C library's digit
   test; the test is written out here instead, which answers the same for
   every input and asks nothing of the library. */
int chk_itok(const char *s)
{
    if (*s != '-' && *s != '+' && !(*s >= '0' && *s <= '9'))
        return 0;

    for (;;) {
        s++;
        if (*s == 0)
            return 1;
        if (!(*s >= '0' && *s <= '9'))
            return 0;
    }
}

/* Scale an intonation value by a strength, through the exponential table. */
int calcIntoni(delta_state *d, const delta_loc *base, const delta_loc *a,
               const delta_loc *b, delta_loc *out)
{
    int32_t f0 = base->field;
    int16_t x = a->field;
    int16_t y = b->field;
    int32_t r;

    (void)d;

    if (x == 0) {
        r = f0;
    } else {
        int32_t p = (int32_t)x * (int32_t)y;
        int32_t n = (p >= 0x7f) ? 0x7f : p;

        if (n < 0)
            n = 0;
        else
            n = (p >= 0x7f) ? 0x7f : p;

        r = ((int32_t)delta_ExpTable[(int16_t)n] * f0) >> 14;
        if (r > 0x7fff)
            r = 0x7fff;
    }

    out->field = (int16_t)r;
    return 0;
}

/* Modulate a breathiness pair by a roughness setting. */
int modulate_pwindi(delta_state *d, const delta_loc *in, delta_loc *a,
                    delta_loc *b)
{
    int16_t v = in->field;
    int32_t n;

    (void)d;

    n = (v > 9) ? 9 : v;
    if (n < 1)
        n = 1;
    else
        n = (v > 9) ? 9 : v;

    v = (int16_t)n;

    a->field = (int16_t)(((int32_t)a->field * delta_PwindModTable[v]) >> 7);

    if (v >= 5)
        b->field = (int16_t)(((int32_t)b->field * (0x69 - v)) / 0x64);
    else
        b->field = (int16_t)(((int32_t)b->field * (v + 0x5f)) / 0x64);

    return 0;
}

/* The boundary a helper written in C sees. A rule variable holds its value
   in the field slot or in the value slot depending on its kind, and the
   helper wants it at a width of its own choosing; these two move it across
   in both directions. A kind that is neither of the two number kinds is
   left alone rather than refused. */
void getDeltaCcodeParm(const delta_loc *src, void *dst, int16_t want)
{
    if (src->kind == DK_SHORT2) {
        if (want == DK_SHORT2)
            *(int16_t *)dst = src->field;
        else if (want == DK_LONG)
            *(int32_t *)dst = src->field;
        return;
    }

    if (src->kind == DK_LONG) {
        if (want == DK_SHORT2)
            *(int16_t *)dst = (int16_t)src->value;
        else if (want == DK_LONG)
            *(int32_t *)dst = src->value;
    }
}

void setDeltaCcodeReturnValue(const void *src, int16_t from, delta_loc *dst)
{
    if (dst->kind == DK_SHORT2) {
        /* Both widths give up the same sixteen bits here. */
        if (from == DK_SHORT2 || from == DK_LONG)
            dst->field = *(const int16_t *)src;
        return;
    }

    if (dst->kind == DK_LONG) {
        if (from == DK_SHORT2)
            dst->value = *(const int16_t *)src;
        else if (from == DK_LONG)
            dst->value = *(const int32_t *)src;
    }
}

/* And what such a helper answers the rule with. */
void setDeltaReturnCode(delta_state *d, uint8_t code)
{
    EVV_AT(delta_vars *, d->vars)->return_code = code;
}

/* One of those helpers: the remainder of one variable by another. */
int modulo(delta_state *d, const delta_loc *a, const delta_loc *b,
           delta_loc *out)
{
    int32_t x, y, r;

    (void)d;
    getDeltaCcodeParm(a, &x, DK_LONG);
    getDeltaCcodeParm(b, &y, DK_LONG);
    r = x % y;
    setDeltaCcodeReturnValue(&r, DK_LONG, out);
    return 0;
}

/* Is the whole run between a statement's two contexts empty of this field?
   The walk steps over markers by their fence link and over statements by
   the forward one, and stops at the first statement that carries a value. */
int ctxt_clstr(delta_state *d, int32_t t, int8_t f)
{
    void *(*get)(void *) = vstmtbl[f].get[0];
    int32_t a = vgetsc(d, 1, 1, t, f);
    int32_t b = vgetsc(d, 0, 1, t, f);

    EVV_WALK(w);

    while (a != b) {
        EVV_WALKED(d, w);

        if (a != 0 && (*(const int32_t *)(intptr_t)a & 2)) {
            a = ((const int32_t *)(intptr_t)a)[EVV_AT(delta_vars *, d->vars)->fence_base + f] & -4;
            continue;
        }

        if (vstmtbl[f].fields[0].kind == DK_LONG) {
            if (*(const int32_t *)get(TFLDS((void *)(intptr_t)a)) != 0)
                return 0;
        } else if (vstmtbl[f].fields[0].kind == DK_SHORT2) {
            if (*(const int16_t *)get(TFLDS((void *)(intptr_t)a)) != 0)
                return 0;
        }

        a = ((const int32_t *)(intptr_t)a)[1] & -4;
    }

    return 1;
}

/* Move the scan onto another stream. The scan is first walked forward until
   it stands on a statement that carries the field, then two records go on
   the stack so the move can be undone, and the fence is marked. */
int chstream(delta_state *d, int16_t v, uint8_t f)
{
    delta_vars *va = EVV_AT(delta_vars *, d->vars);
    delta_stack *s = EVV_AT(delta_stack *, d->stack);
    uint8_t *ctx;
    uint8_t *pos;

    while (!(((const int32_t *)(intptr_t)va->scan_ptr)[va->fence_base + f]
             & 1)) {
        if (!vscanadv(d, 0, 1))
            return 1;
    }

    s->top -= s->ca_size;
    ctx = EVV_AT(uint8_t *, s->top);
    s->limit -= s->ca_size;
    ctx[0] = 3;
    *(int32_t *)(ctx + 4) = v;

    s->top -= s->size_b0;
    pos = EVV_AT(uint8_t *, s->top);
    s->limit -= s->size_b0;
    pos[0] = 1;
    memcpy(pos + 4, &va->scan_ptr, 8);

    EVV_AT(uint8_t *, d->fence_marks)[EVV_AT(uint8_t *, d->fence_index)[f]] = 1;
    va->scan_field = f;
    return 0;
}

/* The engine's own speed number from a words-a-minute figure: a binary
   search through the same table the other direction reads, settling on
   whichever of the two ends it closes on is nearer.

   Outside the table's range the original answers with the table's own end
   value rather than with its index. That is what it does and it is left
   alone. */
int calcWPM2ETI(delta_state *d, const delta_loc *in, delta_loc *out)
{
    int16_t hi = 0xfb;
    int16_t lo = 0;
    int16_t v = in->field;

    (void)d;

    if (v < delta_ETI2WPM_Table[0]) {
        out->field = delta_ETI2WPM_Table[0];
        return 0;
    }
    if (v >= delta_ETI2WPM_Table[250]) {
        out->field = delta_ETI2WPM_Table[250];
        return 0;
    }

    while (hi > lo) {
        int16_t mid = (int16_t)(lo + (hi - lo) / 2);

        if (v != delta_ETI2WPM_Table[mid] && (hi - lo) >= 2) {
            if (v < delta_ETI2WPM_Table[mid])
                hi = mid;
            else
                lo = mid;
            continue;
        }

        if (v == delta_ETI2WPM_Table[mid] || hi == lo) {
            out->field = mid;
            return 0;
        }

        {
            int32_t a = v - delta_ETI2WPM_Table[hi];
            int32_t b = v - delta_ETI2WPM_Table[lo];

            if (a < 0)
                a = -a;
            if (b < 0)
                b = -b;

            out->field = (a < b) ? hi : lo;
        }
        return 0;
    }

    return 0;
}

/* Hertz from semitones. The table covers one octave in tenths of a
   semitone; anything above or below is brought into that octave and the
   answer doubled or halved once per octave moved. */
int calcST2HZ(delta_state *d, const delta_loc *in, delta_loc *out)
{
    int16_t shift = 0;
    int16_t v = (int16_t)in->value;
    int16_t idx;
    int16_t hz;

    (void)d;

    v = (int16_t)(v * 100);

    if (v > 0x1770) {
        shift = (int16_t)((v - 0x1770) / 0x4b0 + 1);
        idx = (int16_t)(v - shift * 0x4b0);
        idx = (int16_t)((idx - 0x12c0) / 10);
        hz = (int16_t)delta_frequencyInHz[idx];
        hz = (int16_t)(hz << shift);
    } else if (v < 0x12c0) {
        shift = (int16_t)((0x12c0 - v) / 0x4b0 + 1);
        idx = (int16_t)(v + shift * 0x4b0);
        idx = (int16_t)((idx - 0x12c0) / 10);
        hz = (int16_t)delta_frequencyInHz[idx];
        hz = (int16_t)(hz >> shift);
    } else {
        idx = (int16_t)((v - 0x12c0) / 10);
        hz = (int16_t)delta_frequencyInHz[idx];
    }

    out->field = hz;
    return 0;
}

/* Semitones from hertz, the other way round. The frequency is halved or
   doubled until it lands in the octave the table covers, the table is
   scanned for the first entry at or above it, and the octaves moved are
   added back on.

   The original leaves its answer unset until the scan finds an entry. The
   scan cannot come up empty, because the octave adjustment above is what
   puts the frequency inside the table's range. */
int calcHZ2ST(delta_state *d, const delta_loc *in, delta_loc *out)
{
    int16_t v = in->field;
    int16_t oct = 0;
    int16_t st = 0;
    int16_t i;

    (void)d;

    v = (int16_t)(v * 100);

    if (v > 0x65f4) {
        while (v > 0x6633) {
            v = (int16_t)(v >> 1);
            oct = (int16_t)(oct + 1);
        }
        for (i = 0; i < 0x7a; i = (int16_t)(i + 1)) {
            if (delta_frequencyInHz[i] >= v) {
                st = (int16_t)delta_frequencyInST[i];
                break;
            }
        }
        st = (int16_t)(st + oct * 0x4b0);
    } else if (v < 0x32c8) {
        while (v < 0x3319) {
            /* Shifted as a bit pattern and cut back to sixteen bits, which
               is what the instruction does and what C will not promise for
               a signed left shift. */
            v = (int16_t)((uint32_t)(int32_t)v << 1);
            oct = (int16_t)(oct + 1);
        }
        for (i = 0; i < 0x7a; i = (int16_t)(i + 1)) {
            if (delta_frequencyInHz[i] >= v) {
                st = (int16_t)delta_frequencyInST[i];
                break;
            }
        }
        st = (int16_t)(st - oct * 0x4b0);
    } else {
        for (i = 0; i < 0x7a; i = (int16_t)(i + 1)) {
            if (delta_frequencyInHz[i] >= v) {
                st = (int16_t)delta_frequencyInST[i];
                break;
            }
        }
    }

    out->field = st;
    return 0;
}

/* Splice a statement into a field's chain between the two statements that
   will stand either side of it, and mark it as carrying the field.

   The original takes two arguments it never reads, and it tests each
   neighbour for null only to choose which of that neighbour's links to
   write, not to decide whether to write at all; a null neighbour is
   therefore written through. Both are kept as they stand. */
void project_rl(delta_state *d, delta_node *t, int32_t unused_10,
                int32_t unused_14, delta_node *l, delta_node *r, uint8_t f)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);
    int32_t fb = v->fence_base + f;
    int32_t *tw = (int32_t *)t;

    (void)unused_10;
    (void)unused_14;

    tw[fb] |= 1;
    CLRONESTM(t);

    if (ALLNSQ(t) && EVV_AT(int8_t *, v->nsq_marks)[f] == 0)
        CLRALLNSQ(t);

    if (l == r)
        return;

    tw[3 + f] = (tw[3 + f] & 3) | EVV_REF(l);
    tw[fb] = (tw[fb] & 3) | EVV_REF(r);

    if (l != NULL && (*(const int32_t *)l & 2)) {
        int32_t *lw = (int32_t *)l;

        lw[fb] = (lw[fb] & 3) | EVV_REF(t);
    } else {
        ((int32_t *)l)[1] = EVV_REF(t);
    }

    if (r != NULL && (*(const int32_t *)r & 2)) {
        int32_t *rw = (int32_t *)r;

        rw[3 + f] = (rw[3 + f] & 3) | EVV_REF(t);
    } else {
        ((int32_t *)r)[0] = EVV_REF(t);
    }
}

/* Look a phrase up in the dictionary's action table and hand back the two
   statements its answer spans. The answer names how many statements to step
   over from the left pointer for each end; markers along the way are
   stepped over without counting. */
int actd_lookup(delta_state *d, int16_t n, delta_token *outl,
                delta_token *outr)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);
    const uint8_t *entry;
    const uint8_t *p;
    int32_t which;

    if (!vprt_range(d, &d->lpta, &d->rpta))
        forceErrorBacktrack(d);

    entry = EVV_AT(uint8_t *, d->act_table) + (int32_t)n * 0x28;
    p = actdlookup(d, d->lpta.node, d->rpta.node, entry);
    if (p == NULL)
        return 1;

    memcpy(&v->unknown_11ec, p + 2, 2);

    for (which = 0; which <= 1; which++) {
        uint8_t count = *p;
        int32_t node = d->lpta.node;
        int32_t steps = 0;

        p++;
        if (count == 0xff)
            continue;

        while (steps < count) {
            if (node != 0 && (*(const int32_t *)(intptr_t)node & 2)) {
                node = ((const int32_t *)(intptr_t)node)
                    [v->fence_base + entry[8]] & -4;
                continue;
            }
            node = ((const int32_t *)(intptr_t)node)[1] & -4;
            steps++;
        }

        if (which == 0) {
            if (outl != NULL)
                outl->value = node;
        } else if (outr != NULL) {
            outr->value = node;
        }
    }

    return 0;
}

/* Project a statement onto the field to the right of another. A statement
   already carrying the field is left where it is.

   The two context lookups are made for what they do to the spine, not for
   what they answer: the original keeps their answers in locals it never
   reads again. They are kept because dropping them would drop the relinking
   they do on the way. */
int vproj_r(delta_state *d, delta_node *t, delta_node *at, uint8_t f)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);
    int32_t fb = v->fence_base + f;
    int32_t next;
    int32_t after;

    if (((const int32_t *)t)[fb] & 1)
        return 1;

    if (v->ctx_both != 0) {
        vgetsc(d, 1, 1, EVV_REF(t), f);
        vgetsc(d, 0, 1, EVV_REF(t), f);
    }

    next = ((const int32_t *)at)[fb] & -4;

    /* A marker stands in for itself; anything else hands on to what follows
       it. The original reads through a null here rather than stopping. */
    if (next != 0 && (*(const int32_t *)(intptr_t)next & 2))
        after = next;
    else
        after = ((const int32_t *)(intptr_t)next)[1] & -4;

    project_rl(d, t, EVV_REF(at), after, at,
               (delta_node *)(intptr_t)next, f);

    if (NONSEQ(t) && v->relink != 0) {
        DELSPINE(d, t);
        INSSPINEL(d, t, (delta_node *)(intptr_t)after);
    }

    return 1;
}

/* Bring a token up to where the scan now stands, if the two are joined by
   nothing but markers. Which way the walk runs, and which links it follows,
   depends on which side of the token the scan is; a real statement in the
   way means they cannot be merged. */
int conj_merge(delta_state *d, delta_token *tok)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);
    int32_t start = tok->value;
    int32_t node = start;
    EVV_WALK(w);

    if (visleft(d, v->scan_ptr, start)) {
        while (node != v->scan_ptr) {
            EVV_WALKED(d, w);

            if (node == 0 || !(*(const int32_t *)(intptr_t)node & 2))
                return 1;
            node = ((const int32_t *)(intptr_t)node)[3 + v->scan_field] & -4;
        }
        if (v->scan_rev == 0)
            tok->value = v->scan_ptr;
        return 0;
    }

    if (visright(d, v->scan_ptr, start)) {
        while (node != v->scan_ptr) {
            EVV_WALKED(d, w);

            if (node == 0 || !(*(const int32_t *)(intptr_t)node & 2))
                return 1;
            node = ((const int32_t *)(intptr_t)node)
                [v->fence_base + v->scan_field] & -4;
        }
        if (v->scan_rev == 1)
            tok->value = v->scan_ptr;
        return 0;
    }

    if (v->scan_ptr != start)
        return 1;

    return 0;
}

/* The mirror of vproj_r: project a statement onto the field to the left of
   another.

   The original re-tests, inside the context block, the same condition it has
   already returned on, so the branch it guards can never be taken. Only the
   side it does take is written here. As in vproj_r the two lookups are made
   for the relinking they do, not for what they answer. */
int vproj_l(delta_state *d, delta_node *t, delta_node *at, uint8_t f)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);
    int32_t fb = v->fence_base + f;
    int32_t prev;
    int32_t before;

    if (((const int32_t *)t)[fb] & 1)
        return 1;

    if (v->ctx_both != 0) {
        vgetsc(d, 1, 1, EVV_REF(t), f);
        vgetsc(d, 0, 1, EVV_REF(t), f);
    }

    prev = ((const int32_t *)at)[3 + f] & -4;

    /* A marker stands in for itself; anything else hands back to what comes
       before it. The original reads through a null here rather than
       stopping. */
    if (prev != 0 && (*(const int32_t *)(intptr_t)prev & 2))
        before = prev;
    else
        before = *(const int32_t *)(intptr_t)prev & -4;

    project_rl(d, t, before, EVV_REF(at),
               (delta_node *)(intptr_t)prev, at, f);

    if (NONSEQ(t) && v->relink != 0) {
        DELSPINE(d, t);
        INSSPINER(d, t, (delta_node *)(intptr_t)before);
    }

    return 1;
}

/* Project the statement the right pointer names onto the field to the right
   of the one the left pointer names. Either step refusing is a fault. */
void proj_r(delta_state *d, uint8_t f)
{
    if (!vsync_tv(d, &d->lpta)
        || !vproj_r(d, (delta_node *)(intptr_t)d->rpta.node,
                    (delta_node *)(intptr_t)d->lpta.node, f))
        forceErrorBacktrack(d);
}

/* The mirror of proj_r: settle the right pointer onto a sync and project
   what it names onto the field to the left of what the left pointer names. */
void proj_l(delta_state *d, uint8_t f)
{
    if (!vsync_tv(d, &d->rpta)
        || !vproj_l(d, (delta_node *)(intptr_t)d->rpta.node,
                    (delta_node *)(intptr_t)d->lpta.node, f))
        forceErrorBacktrack(d);
}

/* Where the scan stands, looking back along the field rather than on. */
static int32_t scan_back(delta_state *d)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);

    return *(int32_t *)(intptr_t)
        (v->scan_ptr + (3 + v->scan_field) * 4) & ~3;
}

/* The mirror of forto_adv_upto_r. Two statements have to lie between the
   scan and the end for the loop to have another round in it, and neither
   may be a marker. */
int forto_adv_upto_l(delta_state *d, int16_t tag, int16_t loop, int16_t bound,
                     uint8_t f, delta_token *tok, const delta_token *end)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);
    int32_t nx;

    if (!for_loop_preamble(d, tag, loop, f, tok))
        return 1;

    v->scan_rev = 0;

    vscanadvUptoTokenOrMarker(d, end->value, 0);
    if (v->scan_ptr == end->value)
        return 0;

    nx = scan_back(d);
    if (nx == 0)
        return 0;
    if ((*(const int32_t *)(intptr_t)nx & 2) != 0)
        return 0;

    if (!vscanadv(d, 1, 0))
        return 0;
    if (v->scan_ptr == end->value)
        return 0;

    vscanadvUptoTokenOrMarker(d, end->value, 0);
    if (v->scan_ptr == end->value)
        return 0;

    nx = scan_back(d);
    if (nx == 0)
        return 0;
    if ((*(const int32_t *)(intptr_t)nx & 2) != 0)
        return 0;

    clearDeltaStackBack(d);
    EVV_AT(delta_stack *, d->stack)->unknown_9c = 0;
    v->testing = 1;
    d->unknown_3c = bound;
    tok->value = v->scan_ptr;
    return 2;
}

/* The engine's own pitch number from a frequency: the same search
   calcWPM2ETI does, over the baseline table instead, and with the frequency
   brought into the table's range first.

   Outside that range the original answers with the table's own end value
   rather than with its index, as its twin does. */
int calcHZ2ETI(delta_state *d, const delta_loc *in, delta_loc *out)
{
    int16_t hi = 0x65;
    int16_t lo = 0;
    int16_t v = in->field;

    (void)d;

    if (v < 0x190)
        v = 0x190;
    if (v > 0x107d)
        v = 0x107d;

    if (v < delta_MidlineVals[0]) {
        out->field = delta_MidlineVals[0];
        return 0;
    }
    if (v >= delta_MidlineVals[100]) {
        out->field = delta_MidlineVals[100];
        return 0;
    }

    while (hi > lo) {
        int16_t mid = (int16_t)(lo + (hi - lo) / 2);

        if (v != delta_MidlineVals[mid] && (hi - lo) >= 2) {
            if (v < delta_MidlineVals[mid])
                hi = mid;
            else
                lo = mid;
            continue;
        }

        if (v == delta_MidlineVals[mid] || hi == lo) {
            out->field = mid;
            return 0;
        }

        {
            int32_t a = v - delta_MidlineVals[hi];
            int32_t b = v - delta_MidlineVals[lo];

            if (a < 0)
                a = -a;
            if (b < 0)
                b = -b;

            out->field = (a < b) ? hi : lo;
        }
        return 0;
    }

    return 0;
}

/* Walk the scan forward until it stands on a real token. The same walk as
   vscanadvUptoTokenOrMarker without an end to stop at: it runs until a
   token turns up, a fence blocks it, or the chain runs out. */
int vscanadvUptoToken(delta_state *d, int32_t usefence)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);
    int32_t cur = v->scan_ptr;
    int32_t field = v->scan_field;
    EVV_WALK(w);

    for (;;) {
        int32_t next, i;

        EVV_WALKED(d, w);

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
            EVV_AT(uint8_t *, d->fence_marks)[i] = 0;
    }
}

/* Step a forall loop over one token to the right. The walk runs to the next
   real token and the loop carries on if what it finds there is a token and
   not a marker.

   The original recomputes where the scan stands three times over; the scan
   does not move between them, so it is read once here. */
int forall_adv_over_r(delta_state *d, int16_t tag, int16_t loop, int16_t bound,
                      uint8_t f, delta_token *tok)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);
    int32_t nx;

    if (!for_loop_preamble(d, tag, loop, f, tok))
        return 1;

    v->scan_rev = 1;

    vscanadvUptoToken(d, 0);

    nx = scan_here(d);
    if (nx == 0)
        return 0;
    if ((*(const int32_t *)(intptr_t)nx & 2) != 0)
        return 0;

    if (!vscanadv(d, 1, 0))
        return 0;

    clearDeltaStackBack(d);
    EVV_AT(delta_stack *, d->stack)->unknown_9c = 0;
    v->testing = 1;
    d->unknown_3c = bound;
    tok->value = v->scan_ptr;
    return 2;
}

/* The same over two tokens: both have to be real for the loop to go on. */
int forall_adv_upto_r(delta_state *d, int16_t tag, int16_t loop, int16_t bound,
                      uint8_t f, delta_token *tok)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);
    int32_t nx;

    if (!for_loop_preamble(d, tag, loop, f, tok))
        return 1;

    v->scan_rev = 1;

    vscanadvUptoToken(d, 0);

    nx = scan_here(d);
    if (nx == 0)
        return 0;
    if ((*(const int32_t *)(intptr_t)nx & 2) != 0)
        return 0;

    if (!vscanadv(d, 1, 0))
        return 0;

    vscanadvUptoToken(d, 0);

    nx = scan_here(d);
    if (nx == 0)
        return 0;
    if ((*(const int32_t *)(intptr_t)nx & 2) != 0)
        return 0;

    clearDeltaStackBack(d);
    EVV_AT(delta_stack *, d->stack)->unknown_9c = 0;
    v->testing = 1;
    d->unknown_3c = bound;
    tok->value = v->scan_ptr;
    return 2;
}

/* The same four walks leftwards rather than rightwards.
 *
 * A loop over a run of tokens comes in four spellings: whether it stops at
 * the far end or steps over it, and which way it walks. This tree had the
 * two that walk right and one of the two that walk to a marker, because
 * those are the ones the nine languages IBM shipped use; these are their
 * mirrors, and the mirror is two lines -- which way the scan is told it is
 * going, and which of a node's two link words the peek reads.
 */
static int forall_adv_leftwards(delta_state *d, int16_t tag, int16_t loop,
                                int16_t bound, uint8_t f, delta_token *tok,
                                int twice)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);
    int32_t nx;

    if (!for_loop_preamble(d, tag, loop, f, tok))
        return 1;

    v->scan_rev = 0;

    vscanadvUptoToken(d, 0);

    nx = scan_back(d);
    if (nx == 0)
        return 0;
    if ((*(const int32_t *)(intptr_t)nx & 2) != 0)
        return 0;

    if (!vscanadv(d, 1, 0))
        return 0;

    if (twice) {
        vscanadvUptoToken(d, 0);

        nx = scan_back(d);
        if (nx == 0)
            return 0;
        if ((*(const int32_t *)(intptr_t)nx & 2) != 0)
            return 0;
    }

    clearDeltaStackBack(d);
    EVV_AT(delta_stack *, d->stack)->unknown_9c = 0;
    v->testing = 1;
    d->unknown_3c = bound;
    tok->value = v->scan_ptr;
    return 2;
}

int forall_adv_over_l(delta_state *d, int16_t tag, int16_t loop, int16_t bound,
                      uint8_t f, delta_token *tok)
{
    return forall_adv_leftwards(d, tag, loop, bound, f, tok, 0);
}

int forall_adv_upto_l(delta_state *d, int16_t tag, int16_t loop, int16_t bound,
                      uint8_t f, delta_token *tok)
{
    return forall_adv_leftwards(d, tag, loop, bound, f, tok, 1);
}

/* And the two that step over the far end while walking to a marker, which
   is forto_adv_upto_l without its second look. */
static int forto_adv_over(delta_state *d, int16_t tag, int16_t loop,
                          int16_t bound, uint8_t f, delta_token *tok,
                          const delta_token *end, int rev)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);
    int32_t nx;

    if (!for_loop_preamble(d, tag, loop, f, tok))
        return 1;

    v->scan_rev = (uint8_t)rev;

    vscanadvUptoTokenOrMarker(d, end->value, 0);
    if (v->scan_ptr == end->value)
        return 0;

    nx = rev ? scan_here(d) : scan_back(d);
    if (nx == 0)
        return 0;
    if ((*(const int32_t *)(intptr_t)nx & 2) != 0)
        return 0;

    if (!vscanadv(d, 1, 0))
        return 0;
    if (v->scan_ptr == end->value)
        return 0;

    clearDeltaStackBack(d);
    EVV_AT(delta_stack *, d->stack)->unknown_9c = 0;
    v->testing = 1;
    d->unknown_3c = bound;
    tok->value = v->scan_ptr;
    return 2;
}

int forto_adv_over_l(delta_state *d, int16_t tag, int16_t loop, int16_t bound,
                     uint8_t f, delta_token *tok, const delta_token *end)
{
    return forto_adv_over(d, tag, loop, bound, f, tok, end, 0);
}

int forto_adv_over_r(delta_state *d, int16_t tag, int16_t loop, int16_t bound,
                     uint8_t f, delta_token *tok, const delta_token *end)
{
    return forto_adv_over(d, tag, loop, bound, f, tok, end, 1);
}

/* Insert a statement of a given type carrying a variable's value. A
   variable whose kind already matches the type goes in as it stands; one
   that does not is copied through a scratch slot of the right width first.

   A type whose own kind is none of the four leaves the scratch pointer
   unset and the copy then goes through whatever the local held. That is the
   original's hazard and the language never asks for such a type. */
void insert_lv(delta_state *d, uint8_t st, delta_loc *loc, uint8_t mode)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);
    delta_operand src;
    delta_operand var;

    if (!vrange_l(d, &d->rpta, &d->lpta, (int8_t)st, mode))
        forceErrorBacktrack(d);

    if (loc->kind >= 0 || loc->kind == STMTYP((int8_t)st)) {
        vinitloc_new(d, &var, loc);
        if (!vins_tok(d, st, d->lpta.node, d->rpta.node, &var))
            forceErrorBacktrack(d);
        reset_field(loc);
        return;
    }

    src.kind = STMTYP((int8_t)st);

    switch (src.kind) {
    case DK_UBYTE:
        src.ptr = &v->scratch_b;
        break;
    case DK_LONG:
        src.ptr = &v->scratch_l;
        break;
    case DK_SHORT:
    case DK_SHORT2:
        src.ptr = &v->scratch_s;
        break;
    default:
        break;
    }

    src.flag = vstmtbl[st].fields[0].flag;

    vinitloc_new(d, &var, loc);
    vassign(d, &src, &var);

    if (!vins_tok(d, st, d->lpta.node, d->rpta.node, &src))
        forceErrorBacktrack(d);

    reset_field(loc);
}

/* Settle a timing pointer onto the statement its offset points into,
   carrying the offset across as it steps. Unlike vctxt_tv this answers zero
   when it managed and one when normalising gave an answer it does not know.

   A statement type whose first field is neither a long nor a short leaves
   the value it reads indeterminate in the original. Zero is used here,
   which leaves the offset alone; the language declares no such type. */
int vtstctx_tv(delta_state *d, delta_tpos *p, int32_t back)
{
    if (p->flags & 1)
        return 0;

    switch (vnormalize(d, p)) {
    case 3:
    case 4:
        p->flags = 1;
        return 0;
    case 2:
        break;
    default:
        return 1;
    }

    if (p->offset > 0 && back == 1) {
        void *(*get)(void *) = vstmtbl[p->field].get[0];
        int32_t val = 0;

        p->node = ((const int32_t *)(intptr_t)p->node)
            [EVV_AT(delta_vars *, d->vars)->fence_base + p->field] & -4;

        if (vstmtbl[p->field].fields[0].kind == DK_LONG) {
            val = *(const int32_t *)get(TFLDS((void *)(intptr_t)p->node));
        } else if (vstmtbl[p->field].fields[0].kind == DK_SHORT2) {
            val = *(const int16_t *)get(TFLDS((void *)(intptr_t)p->node));
            if (val == -0x7fff)
                val = (int32_t)0x80000001u;
        }

        if (val != (int32_t)0x80000001u)
            p->offset -= val;

        p->node = ((const int32_t *)(intptr_t)p->node)[1] & -4;
        return 0;
    }

    if (p->offset < 0 && back == 0) {
        void *(*get)(void *) = vstmtbl[p->field].get[0];
        int32_t val = 0;

        p->node = ((const int32_t *)(intptr_t)p->node)[3 + p->field] & -4;

        if (vstmtbl[p->field].fields[0].kind == DK_LONG) {
            val = *(const int32_t *)get(TFLDS((void *)(intptr_t)p->node));
        } else if (vstmtbl[p->field].fields[0].kind == DK_SHORT2) {
            val = *(const int16_t *)get(TFLDS((void *)(intptr_t)p->node));
            if (val == -0x7fff)
                val = (int32_t)0x80000001u;
        }

        if (val != (int32_t)0x80000001u)
            p->offset += val;

        p->node = *(const int32_t *)(intptr_t)p->node & -4;
    }

    return 0;
}

/* Take the left pointer to its context, asking rather than faulting: a
   pointer that cannot be settled is an answer, not an error. A statement
   already carrying the field stays where it is.

   The two directions hand the settling step and the lookup opposite
   answers, which is why one argument is the other's complement. */
static int pta_tstctxt(delta_state *d, delta_pta *p, uint8_t f, int32_t back)
{
    const int32_t *node;

    if (vtstctx_tv(d, p, back) || p->node == 0)
        return 1;

    node = (const int32_t *)(intptr_t)p->node;
    if (node[EVV_AT(delta_vars *, d->vars)->fence_base + f] & 1)
        return 0;

    p->node = vgetsc(d, back ? 0 : 1, 1, p->node, f);
    return 0;
}

int lpta_tstctxtl(delta_state *d, uint8_t f)
{
    return pta_tstctxt(d, &d->lpta, f, 0);
}

int lpta_tstctxtr(delta_state *d, uint8_t f)
{
    return pta_tstctxt(d, &d->lpta, f, 1);
}

/* And the right register's two. In the original these are four functions
   with one body each, the register being the only thing that differs; the
   direction argument is the same in both pairs. */
int rpta_tstctxtl(delta_state *d, uint8_t f)
{
    return pta_tstctxt(d, &d->rpta, f, 0);
}

int rpta_tstctxtr(delta_state *d, uint8_t f)
{
    return pta_tstctxt(d, &d->rpta, f, 1);
}

/* The logarithm table read at a sixteenth of a step, straight when the
   value lands on an entry and interpolated when it falls between two. */
static int16_t ln_at(int16_t v)
{
    int16_t idx = (int16_t)(v >> 4);
    int32_t frac = v % 16;

    if (frac == 0)
        return delta_LnTable[idx];

    return (int16_t)(delta_LnTable[idx]
        + (int16_t)(((delta_LnTable[idx + 1] - delta_LnTable[idx]) * frac)
                    >> 4));
}

/* One step of a pitch contour: how far the fundamental moves over a run of
   statements, as a ratio read off the logarithm table and divided down. */
int f0_stepi(delta_state *d, const delta_loc *n, const delta_loc *f0,
             const delta_loc *step, const delta_loc *count, delta_loc *out)
{
    int16_t div = n->field;
    int16_t base = f0->field;
    int16_t st = step->field;
    int16_t cnt = count->field;
    int16_t delta;
    int16_t a, b;
    int32_t v;

    (void)d;

    if (base + st * cnt > 0)
        delta = (int16_t)(st * cnt);
    else
        delta = (int16_t)(0x14 - base);

    if (delta == 0) {
        out->field = 0;
        return 0;
    }

    v = (base >= 0x1070) ? 0x1070 : base;
    if (v < 0)
        v = 0;
    a = ln_at((int16_t)v);

    v = (base + delta >= 0x1070) ? 0x1070 : base + delta;
    if (v < 0)
        v = 0;
    b = ln_at((int16_t)v);

    out->field = (int16_t)((a - b) / div);
    return 0;
}

/* How long the run between two timing positions is, in the units the field
   counts. The walk runs from whichever position comes first, adding each
   statement's value and stepping over markers by their fence link, and the
   two positions' own offsets are added or taken off at the ends according
   to which way round the walk went.

   A position whose statement does not carry the field, and whose context
   is not empty of it either, has no answer; so does a run of no statements
   that comes to nothing. Both say so with the same sentinel. */
int32_t dur2(delta_state *d, const delta_tpos *a, const delta_tpos *b,
             int8_t f, int32_t back)
{
    int32_t fb = EVV_AT(delta_vars *, d->vars)->fence_base + f;
    int32_t from, to;
    int32_t total = 0;
    uint8_t counted = 0;

    if (a->node == b->node)
        return b->offset - a->offset;

    if (back != 0 || visleft(d, a->node, b->node)) {
        from = a->node;
        to = b->node;
    } else {
        from = b->node;
        to = a->node;
    }

    if (!(((const int32_t *)(intptr_t)a->node)[fb] & 1)
        && !ctxt_clstr(d, a->node, f))
        return (int32_t)0x80000001u;

    if (!(((const int32_t *)(intptr_t)b->node)[fb] & 1)
        && !ctxt_clstr(d, b->node, f))
        return (int32_t)0x80000001u;

    EVV_WALK(w);

    while (from != to && from != 0) {
        EVV_WALKED(d, w);

        if (*(const int32_t *)(intptr_t)from & 2) {
            from = ((const int32_t *)(intptr_t)from)[fb] & -4;
            continue;
        }

        counted = 1;

        if (vstmtbl[f].fields[0].kind == DK_LONG) {
            total += *(const int32_t *)
                vstmtbl[f].get[0](TFLDS((void *)(intptr_t)from));
        } else if (vstmtbl[f].fields[0].kind == DK_SHORT2) {
            total += *(const int16_t *)
                vstmtbl[f].get[0](TFLDS((void *)(intptr_t)from));
        }

        from = ((const int32_t *)(intptr_t)from)[1] & -4;
    }

    if (to == a->node) {
        if (a->flags & 2)
            total += a->offset;
        if (b->flags & 2)
            total -= b->offset;
        total = -total;
    } else {
        if (a->flags & 2)
            total -= a->offset;
        if (b->flags & 2)
            total += b->offset;
    }

    if (!counted && total == 0)
        return (int32_t)0x80000001u;

    return total;
}

/* The same, asking which position comes first rather than being told. */
int32_t vdur(delta_state *d, const delta_tpos *a, const delta_tpos *b,
             int8_t f)
{
    return dur2(d, a, b, f, 0);
}

/* Give the run between two positions a total duration, dividing it among
   the statements in proportion to what they already hold.

   The factor is in thousandths of what the run measures now, so a run
   already the right length leaves everything alone. Markers are stepped
   over by their fence link and take no share. Five hundred is added before
   each division by a thousand so the shares round to nearest rather than
   towards zero, and away from zero on the other side as well, which is why
   the sign of the factor picks which five hundred.

   Answers whether it gave up, which only happens when the two positions do
   not bound a run of this field at all. */
int vdur_ass(delta_state *d, delta_tpos *a, delta_tpos *b, int8_t f,
             int32_t total)
{
    const delta_stmt *e = &vstmtbl[f];
    int32_t span;
    int32_t factor;
    int32_t bias;
    int32_t p;

    if (vrange_2pt(d, a, b, f, 0))
        return 1;

    span = vdur(d, a, b, f);
    factor = (span != 0)
        ? (int32_t)((uint32_t)total * 1000u) / span
        : 0;

    if (e->fields[0].kind == DK_SHORT2 || e->fields[0].kind == DK_LONG)
        bias = (factor < 0) ? -500 : 500;
    else
        bias = 0;

    p = a->node;
    EVV_WALK(w);

    while (p != b->node) {
        EVV_WALKED(d, w);

        if (p != 0 && (*(const int32_t *)(intptr_t)p & 2)) {
            p = ((const int32_t *)(intptr_t)p)[EVV_AT(delta_vars *, d->vars)->fence_base + f] & -4;
            continue;
        }

        if (e->fields[0].kind == DK_LONG) {
            int32_t was = *(const int32_t *)
                e->get[0](TFLDS((void *)(intptr_t)p));

            total = (int32_t)((uint32_t)was * (uint32_t)factor
                              + (uint32_t)bias) / 1000;
            vinitflds(d, (uint8_t)f, TFLDS((void *)(intptr_t)p), &total);
        } else {
            int32_t was = *(const int16_t *)
                e->get[0](TFLDS((void *)(intptr_t)p));
            int16_t share;

            total = (int32_t)((uint32_t)was * (uint32_t)factor
                              + (uint32_t)bias) / 1000;
            share = (int16_t)total;
            vinitflds(d, (uint8_t)f, TFLDS((void *)(intptr_t)p), &share);
        }

        p = ((const int32_t *)(intptr_t)p)[1] & -4;
    }

    return 0;
}

/* The statement form of the same thing: read the wanted duration out of the
   variable the rule names, then spread it over the run between the two
   pointer registers. The field is released whichever way it goes. */
int dur_ass(delta_state *d, int8_t f, delta_loc *field, uint8_t mode)
{
    delta_operand want;
    delta_operand loc;
    int32_t total;

    if (vrange_2pt(d, &d->lpta, &d->rpta, f, mode)) {
        reset_field(field);
        return 1;
    }

    want.kind = DK_LONG;
    want.ptr = &total;
    want.flag = 0;

    vinitloc_new(d, &loc, field);
    vassign(d, &want, &loc);

    if (vdur_ass(d, &d->lpta, &d->rpta, f, total)) {
        reset_field(field);
        return 1;
    }

    reset_field(field);
    return 0;
}

/* The other direction: measure the run between the two pointer registers
   and put the answer in the variable the rule names. A run with no answer
   is reported as no time at all rather than passed on. Both registers have
   to be timing positions first, and a rule that asks otherwise is made to
   backtrack. */
void dur_expr(delta_state *d, uint8_t f, delta_loc *field)
{
    delta_operand dst;
    delta_operand src;
    int32_t total;

    if (!vtimept_tv(d, &d->lpta, 0) || !vtimept_tv(d, &d->rpta, 1))
        forceErrorBacktrack(d);

    vinitloc_new(d, &dst, field);

    total = vdur(d, &d->lpta, &d->rpta, (int8_t)f);
    if (total == (int32_t)0x80000001u)
        total = 0;

    src.flag = vstmtbl[f].fields[0].flag;
    src.kind = DK_LONG;
    src.ptr = &total;
    vassign(d, &dst, &src);

    reset_field(field);
}
