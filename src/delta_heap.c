#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "delta.h"
#include "evv_arena.h"

/* The Delta heap. Rules build tokens and syncs here and throw them away
   wholesale when a match fails, so what matters is the mark and rewind
   discipline: recordDeltaHeapPos takes a mark, freeDeltaHeapTo puts the heap
   back to it, and everything allocated in between goes at once.

   Segments are carved from the top down. A segment that empties is kept on a
   free list rather than returned, up to ten of them, because the same shapes
   are allocated over and over.

   Every link between segments is a reference rather than a pointer, because a
   segment header is a fixed shape the rules reach into by offset. Chasing one
   therefore reads SEG(x) rather than x, which is what the shorthand below is
   for; on a 32-bit build it is the cast the file always had. */

#define SEG(r) EVV_AT(delta_seg *, (r))

#define SEG_HEADER 0x18

#define AT_STACK(field, offset) \
    typedef char stack_##field##_at_##offset \
        [offsetof(delta_stack, field) == offset ? 1 : -1]

AT_STACK(sync_size, 0x0094);
AT_STACK(heap_first, 0x0500);
AT_STACK(seg, 0x0504);
AT_STACK(heap_cur, 0x0508);
AT_STACK(seg_size, 0x0514);
AT_STACK(walk, 0x0510);
AT_STACK(marks, 0x051c);
AT_STACK(free_count, 0x05e4);
AT_STACK(free_segs, 0x05e8);
typedef char delta_seg_is_0x18[sizeof(delta_seg) == 0x18 ? 1 : -1];
typedef char delta_mark_is_0x14[sizeof(delta_mark) == 0x14 ? 1 : -1];

/* Take a segment from the free list, or make one. The starting value of used
   is chosen so that end minus used is eight-aligned. */
static evv_ref allocDynaSegment(delta_state *d, int32_t size)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);
    delta_seg *seg;
    evv_ref ref;

    if (s->free_segs != 0) {
        ref = s->free_segs;
        seg = SEG(ref);
        seg->live = 0;
        s->free_segs = seg->next;
        s->free_count--;

        seg->used = seg->end & 3;
        if ((seg->end & 7) == 0)
            seg->used += 4;

        seg->next = 0;
        seg->prev = 0;
        return ref;
    }

    seg = delta_sys_alloc(SEG_HEADER);
    if (seg == NULL)
        return 0;

    seg->next = 0;
    seg->prev = 0;
    seg->live = 0;

    seg->block = EVV_REF(delta_sys_alloc((size_t)size));
    if (seg->block == 0) {
        delta_sys_free(seg);
        return 0;
    }

    seg->end = seg->block + size - 1;
    seg->used = seg->end & 3;
    if ((seg->end & 7) == 0)
        seg->used += 4;

    return EVV_REF(seg);
}

/* Give a whole chain of segments back. */
static void freeDynaMem(evv_ref ref)
{
    while (ref != 0) {
        evv_ref next = SEG(ref)->next;

        delta_sys_free(EVV_AT(void *, SEG(ref)->block));
        delta_sys_free(SEG(ref));
        ref = next;
    }
}

/* Carve size bytes off a segment, moving on to a fresh one when this will not
   fit. A size of zero or less asks how much is left rather than taking any. */
static uint8_t *allocDynaMem(delta_state *d, evv_ref ref, int32_t size)
{
    delta_seg *seg = SEG(ref);
    int32_t slack = size & 7;

    if (size <= 0)
        return EVV_AT(uint8_t *, seg->end - seg->used);

    if (slack != 0)
        size += 8 - slack;

    seg->used += size;

    if (seg->used < EVV_AT(delta_stack *, d->stack)->seg_size)
        return EVV_AT(uint8_t *, seg->end - seg->used);

    seg->used -= size;
    seg->next = allocDynaSegment(d, EVV_AT(delta_stack *, d->stack)->seg_size);
    if (seg->next == 0)
        return NULL;

    SEG(seg->next)->prev = ref;
    SEG(seg->next)->used += size;

    if (SEG(seg->next)->used > EVV_AT(delta_stack *, d->stack)->seg_size)
        return NULL;

    return EVV_AT(uint8_t *, SEG(seg->next)->end - SEG(seg->next)->used);
}

int initializeDeltaHeap(delta_state *d, int32_t size)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);
    int32_t i;

    s->heap_first = allocDynaSegment(d, size);
    s->heap_cur = s->heap_first;
    s->seg_size = size;

    for (i = 0; i < DELTA_MARKS; i++)
        s->marks[i].unused = 1;

    return s->heap_first != 0;
}

void resetDeltaHeap(delta_state *d)
{
    freeDynaMem(EVV_AT(delta_stack *, d->stack)->heap_first);
    initializeDeltaHeap(d, EVV_AT(delta_stack *, d->stack)->seg_size);
}

/* Hand out an object, with the segment it came from stamped in the four bytes
   in front of it so freeing knows where to put it back. */
DELTA_FASTCALL void *allocDeltaHeapObject(delta_state *d, int32_t size)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);
    uint8_t *p = allocDynaMem(d, s->heap_cur, size + 4);

    if (p == NULL)
        return NULL;

    if (SEG(s->heap_cur)->next != 0)
        s->heap_cur = SEG(s->heap_cur)->next;

    *(evv_ref *)p = s->heap_cur;
    SEG(s->heap_cur)->live++;
    return p + 4;
}

DELTA_FASTCALL void freeDeltaHeapObject(delta_state *d, void *p)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);
    uint8_t *head = (uint8_t *)p - 4;
    evv_ref ref = *(evv_ref *)head;
    delta_seg *seg = SEG(ref);

    seg->live--;
    if (seg->live != 0)
        return;

    if (ref == s->heap_cur) {
        /* Nothing is left in the segment being filled, so start it over. */
        seg->used = seg->end & 3;
        return;
    }

    if (s->free_count < DELTA_MARKS) {
        SEG(seg->prev)->next = seg->next;
        if (seg->next != 0)
            SEG(seg->next)->prev = seg->prev;

        seg->next = s->free_segs;
        s->free_segs = ref;
        s->free_count++;
        return;
    }

    SEG(seg->prev)->next = seg->next;
    if (seg->next != 0)
        SEG(seg->next)->prev = seg->prev;

    delta_sys_free(EVV_AT(void *, seg->block));
    delta_sys_free(seg);
}

/* How far into the heap a pointer is, counted in units. A negative answer
   means it is not in the heap at all: minus one when nothing holds it, minus
   two when only the free list does. */
/* Give back everything the heap and the stack are holding: the segments kept
   on the free list, the heap's own chain, and the one the stack lives in.
   Each chain is freed from its head, which takes the whole chain with it. */
void deltaHeapCleanup(delta_state *d)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);

    if (s->free_segs != 0)
        freeDynaMem(s->free_segs);
    if (s->heap_first != 0)
        freeDynaMem(s->heap_first);
    if (s->seg != 0)
        freeDynaMem(s->seg);

    s->seg = 0;
    s->heap_cur = 0;
    s->heap_first = 0;
    s->free_segs = 0;
}

/* One segment for the stack, and the two ends recorded in it. The stack runs
   downwards, so the top starts at the end of the segment and the limit at
   the far end, and both are then pulled back by the size of a
   begin-or-alternative marker -- one of which is written at the top before
   anything else, so an unwind always finds a floor. */
int32_t initializeDeltaStack(delta_state *d, int32_t size)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);

    s->seg = allocDynaSegment(d, size);

    s->top = SEG(s->seg)->end - SEG(s->seg)->used;
    s->base = size;
    s->limit = size - SEG(s->seg)->used;

    s->top -= s->boa_size;
    s->limit -= s->boa_size;

    *EVV_AT(uint8_t *, s->top) = 8;
    setDeltaStackVBot(d, EVV_AT(uint8_t *, s->top));

    return s->seg != 0;
}

/* Walking the backtracking stack a record at a time.
 *
 * Every record starts with its kind, and the kind says how long it is: the
 * fixed sizes the stack block carries for each, except a token record, whose
 * own length word decides it -- rounded up to the odd number above it and
 * then past the header. A kind the switch does not know costs two bytes,
 * which is the back marker.
 *
 * Nothing in the engine walks the stack this way. It is the display's, the
 * one src/delta_trace.c says why this tree does not have, and it is here for
 * completeness rather than for a caller.
 */
int32_t peekDeltaStackNext(delta_state *d)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);
    int32_t      at = s->walk;
    int32_t      size;

    switch (*EVV_AT(const int8_t *, at)) {
    case 0:  size = s->ca_size; break;
    case 1:  size = s->size_b0; break;
    case 2:
        size = ((((*EVV_AT(const int32_t *, at + 8) - 1) & ~1) | 1)
                + s->size_ac + 1);
        break;
    case 3:  size = s->ca_size; break;
    case 4:  size = s->boa_size; break;
    case 5:  size = s->size_b8; break;
    case 6:  size = s->boa_size; break;
    case 7:  size = s->size_a8; break;
    default: size = 2; break;
    }

    s->walk = at + size;
    return at;
}

/* Where such a walk begins. Note what it answers with: it sets the walk to
   the top, steps once, and hands back where the step left it rather than
   where it started. That is the original's doing. */
int32_t peekDeltaStackStart(delta_state *d)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);

    s->walk = s->top;
    peekDeltaStackNext(d);
    return s->walk;
}

/* Give the stack's own segment back and take a fresh one of the size it was
   built with. */
void resetDeltaStack(delta_state *d)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);

    freeDynaMem(s->seg);
    s->seg = 0;
    initializeDeltaStack(d, s->base);
}

int32_t getDeltaHeapSegNumber(delta_state *d, uint8_t *p, int32_t unit)
{
    evv_ref owner = *(evv_ref *)(p - 4);
    evv_ref seg = EVV_AT(delta_stack *, d->stack)->heap_first;
    int32_t n = 0;

    while (seg != 0 && seg != owner) {
        n++;
        seg = SEG(seg)->next;
    }

    if (seg == 0) {
        seg = EVV_AT(delta_stack *, d->stack)->free_segs;
        while (seg != 0 && seg != owner)
            seg = SEG(seg)->next;

        return seg == 0 ? -1 : -2;
    }

    return (int32_t)((uint32_t)EVV_AT(delta_stack *, d->stack)->seg_size / (uint32_t)unit) * n
           + (int32_t)(SEG(owner)->end - EVV_REF(p)) / unit;
}

/* Take a mark. Returns zero when all ten are already in use. */
int recordDeltaHeapPos(delta_state *d)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);
    int32_t i;

    for (i = 0; i < DELTA_MARKS; i++) {
        if (s->marks[i].unused == 0)
            continue;

        s->marks[i].unused = 0;
        s->marks[i].pos = SEG(s->heap_cur)->end - SEG(s->heap_cur)->used;
        s->marks[i].used = SEG(s->heap_cur)->used;
        s->marks[i].live = SEG(s->heap_cur)->live;
        s->marks[i].seg = s->heap_cur;
        return 1;
    }

    return 0;
}

/* Put the heap back to a mark, dropping every segment filled since. */
void freeDeltaHeapTo(delta_state *d, uint8_t *pos, int32_t release)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);
    int32_t i;

    for (i = 0; i < DELTA_MARKS; i++) {
        if (s->marks[i].unused != 0)
            continue;
        if (EVV_REF(pos) != s->marks[i].pos)
            continue;

        while (s->heap_cur != s->marks[i].seg && s->heap_cur != 0) {
            evv_ref gone = s->heap_cur;

            delta_sys_free(EVV_AT(void *, SEG(gone)->block));
            s->heap_cur = SEG(gone)->prev;
            delta_sys_free(SEG(gone));
        }

        if (s->heap_cur == 0)
            continue;

        SEG(s->heap_cur)->used = s->marks[i].used;
        SEG(s->heap_cur)->live = s->marks[i].live;

        if (release != 0)
            s->marks[i].unused = 1;

        return;
    }
}

void free_heap(delta_state *d, void *p)
{
    freeDeltaHeapObject(d, p);
}

/* A token is the statement's record with eight bytes of header in front. */
void *alloc_tok(delta_state *d, const delta_stmt *e)
{
    return allocDeltaHeapObject(d, e->length + 8);
}

/* A sync is one node, blanked and marked as one. */
void *alloc_sync(delta_state *d)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);
    delta_node *t = allocDeltaHeapObject(d, s->sync_size);

    if (t == NULL)
        return NULL;

    memset(t, 0, (size_t)s->sync_size);
    t->flags0 |= 2;
    SETONESTM(t);
    CLRALLNSQ(t);
    return t;
}
