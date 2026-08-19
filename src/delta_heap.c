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
   are allocated over and over. */

#define SEG_HEADER 0x18

#define AT_STACK(field, offset) \
    typedef char stack_##field##_at_##offset \
        [offsetof(delta_stack, field) == offset ? 1 : -1]

AT_STACK(sync_size, 0x0094);
AT_STACK(heap_first, 0x0500);
AT_STACK(seg, 0x0504);
AT_STACK(heap_cur, 0x0508);
AT_STACK(seg_size, 0x0514);
AT_STACK(marks, 0x051c);
AT_STACK(free_count, 0x05e4);
AT_STACK(free_segs, 0x05e8);
typedef char delta_seg_is_0x18[sizeof(delta_seg) == 0x18 ? 1 : -1];
typedef char delta_mark_is_0x14[sizeof(delta_mark) == 0x14 ? 1 : -1];

/* Take a segment from the free list, or make one. The starting value of used
   is chosen so that end minus used is eight-aligned. */
static delta_seg *allocDynaSegment(delta_state *d, int32_t size)
{
    delta_stack *s = d->stack;
    delta_seg *seg;

    if (s->free_segs != NULL) {
        seg = s->free_segs;
        seg->live = 0;
        s->free_segs = seg->next;
        s->free_count--;

        seg->used = EVV_REF(seg->end) & 3;
        if ((EVV_REF(seg->end) & 7) == 0)
            seg->used += 4;

        seg->next = NULL;
        seg->prev = NULL;
        return seg;
    }

    seg = delta_sys_alloc(SEG_HEADER);
    if (seg == NULL)
        return NULL;

    seg->next = NULL;
    seg->prev = NULL;
    seg->live = 0;

    seg->block = delta_sys_alloc((size_t)size);
    if (seg->block == NULL) {
        delta_sys_free(seg);
        return NULL;
    }

    seg->end = seg->block + size - 1;
    seg->used = EVV_REF(seg->end) & 3;
    if ((EVV_REF(seg->end) & 7) == 0)
        seg->used += 4;

    return seg;
}

/* Give a whole chain of segments back. */
static void freeDynaMem(delta_seg *seg)
{
    while (seg != NULL) {
        delta_seg *next = seg->next;

        delta_sys_free(seg->block);
        delta_sys_free(seg);
        seg = next;
    }
}

/* Carve size bytes off a segment, moving on to a fresh one when this will not
   fit. A size of zero or less asks how much is left rather than taking any. */
static uint8_t *allocDynaMem(delta_state *d, delta_seg *seg, int32_t size)
{
    int32_t slack = size & 7;
    uint8_t *out;

    if (size <= 0)
        return seg->end - seg->used;

    if (slack != 0)
        size += 8 - slack;

    seg->used += size;

    if (seg->used < d->stack->seg_size)
        return seg->end - seg->used;

    seg->used -= size;
    seg->next = allocDynaSegment(d, d->stack->seg_size);
    if (seg->next == NULL)
        return NULL;

    seg->next->prev = seg;
    seg->next->used += size;

    if (seg->next->used > d->stack->seg_size)
        out = NULL;
    else
        out = seg->next->end - seg->next->used;

    return out;
}

int initializeDeltaHeap(delta_state *d, int32_t size)
{
    delta_stack *s = d->stack;
    int32_t i;

    s->heap_first = allocDynaSegment(d, size);
    s->heap_cur = s->heap_first;
    s->seg_size = size;

    for (i = 0; i < DELTA_MARKS; i++)
        s->marks[i].unused = 1;

    return s->heap_first != NULL;
}

void resetDeltaHeap(delta_state *d)
{
    freeDynaMem(d->stack->heap_first);
    initializeDeltaHeap(d, d->stack->seg_size);
}

/* Hand out an object, with the segment it came from stamped in the four bytes
   in front of it so freeing knows where to put it back. */
DELTA_FASTCALL void *allocDeltaHeapObject(delta_state *d, int32_t size)
{
    delta_stack *s = d->stack;
    uint8_t *p = allocDynaMem(d, s->heap_cur, size + 4);

    if (p == NULL)
        return NULL;

    if (s->heap_cur->next != NULL)
        s->heap_cur = s->heap_cur->next;

    *(delta_seg **)p = s->heap_cur;
    s->heap_cur->live++;
    return p + 4;
}

DELTA_FASTCALL void freeDeltaHeapObject(delta_state *d, void *p)
{
    delta_stack *s = d->stack;
    uint8_t *head = (uint8_t *)p - 4;
    delta_seg *seg = *(delta_seg **)head;

    seg->live--;
    if (seg->live != 0)
        return;

    if (seg == s->heap_cur) {
        /* Nothing is left in the segment being filled, so start it over. */
        s->heap_cur->used = EVV_REF(s->heap_cur->end) & 3;
        return;
    }

    if (s->free_count < DELTA_MARKS) {
        seg->prev->next = seg->next;
        if (seg->next != NULL)
            seg->next->prev = seg->prev;

        seg->next = s->free_segs;
        s->free_segs = seg;
        s->free_count++;
        return;
    }

    seg->prev->next = seg->next;
    if (seg->next != NULL)
        seg->next->prev = seg->prev;

    delta_sys_free(seg->block);
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
    delta_stack *s = d->stack;

    if (s->free_segs != NULL)
        freeDynaMem(s->free_segs);
    if (s->heap_first != NULL)
        freeDynaMem(s->heap_first);
    if (s->seg != NULL)
        freeDynaMem(s->seg);

    s->seg = NULL;
    s->heap_cur = NULL;
    s->heap_first = NULL;
    s->free_segs = NULL;
}

/* One segment for the stack, and the two ends recorded in it. The stack runs
   downwards, so the top starts at the end of the segment and the limit at
   the far end, and both are then pulled back by the size of a
   begin-or-alternative marker -- one of which is written at the top before
   anything else, so an unwind always finds a floor. */
int32_t initializeDeltaStack(delta_state *d, int32_t size)
{
    delta_stack *s = d->stack;

    s->seg = allocDynaSegment(d, size);

    s->top = s->seg->end - s->seg->used;
    s->base = (uint8_t *)(intptr_t)size;
    s->limit = (uint8_t *)(intptr_t)(size - s->seg->used);

    s->top -= s->boa_size;
    s->limit -= s->boa_size;

    *s->top = 8;
    setDeltaStackVBot(d, s->top);

    return s->seg != NULL;
}

int32_t getDeltaHeapSegNumber(delta_state *d, uint8_t *p, int32_t unit)
{
    delta_seg *owner = *(delta_seg **)(p - 4);
    delta_seg *seg = d->stack->heap_first;
    int32_t n = 0;

    while (seg != NULL && seg != owner) {
        n++;
        seg = seg->next;
    }

    if (seg == NULL) {
        seg = d->stack->free_segs;
        while (seg != NULL && seg != owner)
            seg = seg->next;

        return seg == NULL ? -1 : -2;
    }

    return (int32_t)((uint32_t)d->stack->seg_size / (uint32_t)unit) * n
           + (int32_t)(owner->end - p) / unit;
}

/* Take a mark. Returns zero when all ten are already in use. */
int recordDeltaHeapPos(delta_state *d)
{
    delta_stack *s = d->stack;
    int32_t i;

    for (i = 0; i < DELTA_MARKS; i++) {
        if (s->marks[i].unused == 0)
            continue;

        s->marks[i].unused = 0;
        s->marks[i].pos = s->heap_cur->end - s->heap_cur->used;
        s->marks[i].used = s->heap_cur->used;
        s->marks[i].live = s->heap_cur->live;
        s->marks[i].seg = s->heap_cur;
        return 1;
    }

    return 0;
}

/* Put the heap back to a mark, dropping every segment filled since. */
void freeDeltaHeapTo(delta_state *d, uint8_t *pos, int32_t release)
{
    delta_stack *s = d->stack;
    int32_t i;

    for (i = 0; i < DELTA_MARKS; i++) {
        if (s->marks[i].unused != 0)
            continue;
        if (pos != s->marks[i].pos)
            continue;

        while (s->heap_cur != s->marks[i].seg && s->heap_cur != NULL) {
            delta_seg *gone = s->heap_cur;

            delta_sys_free(gone->block);
            s->heap_cur = gone->prev;
            delta_sys_free(gone);
        }

        if (s->heap_cur == NULL)
            continue;

        s->heap_cur->used = s->marks[i].used;
        s->heap_cur->live = s->marks[i].live;

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
    delta_stack *s = d->stack;
    delta_node *t = allocDeltaHeapObject(d, s->sync_size);

    if (t == NULL)
        return NULL;

    memset(t, 0, (size_t)s->sync_size);
    t->flags0 |= 2;
    SETONESTM(t);
    CLRALLNSQ(t);
    return t;
}
