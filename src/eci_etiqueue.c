/* A ring of pointers that grows when it fills.
 *
 * Two indices chase each other round one array: the head is where the next
 * pointer comes out, the tail is where the next one goes in. Equal indices
 * mean empty, so a full ring and an empty one would look alike -- which is
 * why filling it is not allowed to happen. The moment a push brings the tail
 * back round onto the head the array is doubled instead, and if that fails
 * the push is undone and refused, so the two are never equal for the wrong
 * reason.
 *
 * Doubling straightens the ring out as it copies: everything from the head
 * to the end of the old array first, then everything before the head after
 * it, so the new array starts at nought and runs to the old capacity.
 *
 * Four of the six are virtual, and reset and pop go through the table rather
 * than calling their own class directly, which is how a queue that overrides
 * pop still gets emptied properly.
 */

#include <stdint.h>
#include <string.h>
#include "eci_synththread.h"
#include "evv_abi.h"
#include "eci_objects.h"

struct ETIqueueVtbl {
    THIS void    *(*destroy)(ETIqueue *self, int32_t free_it);
    THIS int32_t  (*push)(ETIqueue *self, void *p);
    THIS int32_t  (*pop)(ETIqueue *self, void **out);
    THIS int32_t  (*peekHead)(ETIqueue *self, void **out);
};
typedef struct ETIqueueVtbl ETIqueueVtbl;

extern const ETIqueueVtbl vtbl_etiqueue;

/* How much room one takes here. IBM's is 0x14 bytes because a vtable
   pointer and an array pointer were four apiece; on a wider host it is more,
   and whoever makes one has to ask rather than assume. */
const uint32_t eq_bytes = sizeof(ETIqueue);

extern void *cpp_new(uint32_t n) MANGLED("??2@YAPAXI@Z");
extern void  cpp_delete(void *p) MANGLED("??3@YAXPAX@Z");

THIS void    *eq_destroy(ETIqueue *q, int32_t free_it);
THIS void     eq_dtor(ETIqueue *q);
THIS void     eq_reset(ETIqueue *q);
THIS int32_t  eq_isEmpty(ETIqueue *q);
THIS int32_t  eq_push(ETIqueue *q, void *p);
THIS int32_t  eq_pop(ETIqueue *q, void **out);
THIS int32_t  eq_peekHead(ETIqueue *q, void **out);
THIS int32_t eq_doubleArraySize(ETIqueue *q);

/* Room for the asked-for number of pointers. An allocation that fails leaves
   a queue with no room at all rather than one that lies about its size. */
THIS ETIqueue *eq_ctor(ETIqueue *q, uint32_t capacity)
{
    q->vt       = &vtbl_etiqueue;
    q->array    = 0;
    q->capacity = capacity;
    q->head     = 0;
    q->tail     = 0;

    q->array = cpp_new(capacity * (uint32_t)sizeof *q->array);
    if (!q->array)
        q->capacity = 0;
    return q;
}

/* Empty it first, so that a subclass which owns what it holds gets its say
   before the array goes. */
THIS void eq_dtor(ETIqueue *q)
{
    q->vt = &vtbl_etiqueue;
    eq_reset(q);

    if (q->array) {
        cpp_delete(q->array);
        q->array    = 0;
        q->capacity = 0;
        q->tail     = 0;
        q->head     = 0;
    }
}

THIS void *eq_destroy(ETIqueue *q, int32_t free_it)
{
    eq_dtor(q);
    if (free_it & 1)
        cpp_delete(q);
    return q;
}

/* Nothing in it, or nowhere to put anything. */
THIS int32_t eq_isEmpty(ETIqueue *q)
{
    if (!q->array || q->head == q->tail)
        return 1;
    return 0;
}

/* Pop until it stops giving, through the table so that an override runs. */
THIS void eq_reset(ETIqueue *q)
{
    int32_t got = 1;

    while (got) {
        void *p;

        if (eq_isEmpty(q))
            break;
        p = 0;
        got = ((const ETIqueueVtbl *)q->vt)->pop(q, &p);
    }
}

/* Write, step on, and refuse to let the tail land on the head. */
THIS int32_t eq_push(ETIqueue *q, void *p)
{
    if (!q->array)
        return 0;

    q->array[q->tail] = p;
    q->tail++;
    if (q->tail == q->capacity)
        q->tail = 0;

    if (q->tail == q->head && !eq_doubleArraySize(q)) {
        /* Put the tail back where it was and say it did not go in. */
        if (q->tail == 0)
            q->tail = q->capacity - 1;
        else
            q->tail--;
        return 0;
    }
    return 1;
}

THIS int32_t eq_peekHead(ETIqueue *q, void **out)
{
    int32_t ok = 0;

    if (q->array && !eq_isEmpty(q))
        ok = 1;

    if (ok)
        *out = q->array[q->head];
    else
        *out = 0;
    return ok;
}

/* Look, then step past what was looked at. */
THIS int32_t eq_pop(ETIqueue *q, void **out)
{
    int32_t got = ((const ETIqueueVtbl *)q->vt)->peekHead(q, out);

    if (got) {
        q->head++;
        if (q->head == q->capacity)
            q->head = 0;
    }
    return got;
}

/* Twice the room, and the ring straightened out into it. */
THIS int32_t eq_doubleArraySize(ETIqueue *q)
{
    uint32_t bigger;
    void   **fresh;
    void   **old;

    if (!q->array)
        return 0;

    bigger = q->capacity * 2;
    fresh  = cpp_new(bigger * (uint32_t)sizeof *q->array);
    if (!fresh)
        return 0;

    memcpy(fresh, q->array + q->head,
           (q->capacity - q->head) * sizeof *q->array);
    memcpy(fresh + (q->capacity - q->head), q->array,
           q->head * sizeof *q->array);

    old = q->array;
    cpp_delete(old);

    q->array    = fresh;
    q->head     = 0;
    q->tail     = q->capacity;
    q->capacity = bigger;
    return 1;
}

const ETIqueueVtbl vtbl_etiqueue = {
    eq_destroy,
    eq_push,
    eq_pop,
    eq_peekHead
};

ALIAS("??_7ETIqueue@@6B@", "vtbl_etiqueue");
ALIAS("??0ETIqueue@@QAE@K@Z", "eq_ctor");
ALIAS("??1ETIqueue@@UAE@XZ", "eq_dtor");
ALIAS("??_GETIqueue@@UAEPAXI@Z", "eq_destroy");
ALIAS("??_EETIqueue@@UAEPAXI@Z", "eq_destroy");
ALIAS("?reset@ETIqueue@@QAEXXZ", "eq_reset");
ALIAS("?isEmpty@ETIqueue@@QAEHXZ", "eq_isEmpty");
ALIAS("?push@ETIqueue@@UAEHPAX@Z", "eq_push");
ALIAS("?pop@ETIqueue@@UAEHPAPAX@Z", "eq_pop");
ALIAS("?peekHead@ETIqueue@@UAEHPAPAX@Z", "eq_peekHead");
ALIAS("?doubleArraySize@ETIqueue@@AAEHXZ", "eq_doubleArraySize");
