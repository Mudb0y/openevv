/* The engine's output streams, and how values get into them.
 *
 * A language declares a handful of named streams -- pitch, duration and so
 * on -- and the rules push values into them as they go. Each stream is a
 * queue of time and value pairs sitting behind a name, and the whole set is
 * one array built once when the language says how many it wants.
 *
 * The interesting part is how a value is stored. Both the moment and the
 * value are only sixteen bits in the queue, but a moment can be bigger than
 * that, so times go in as gaps from the last one rather than absolutely.
 * A gap that still will not fit is written as all-ones followed by the whole
 * moment split across the next pair, and reading knows to put it back
 * together. So the common case costs one pair and the rare one costs two.
 *
 * Deleting a segment does nothing at all. The original declares it, calls it
 * from two places, and leaves it empty; it is kept so those two calls still
 * go somewhere.
 */

#include <stdint.h>
#include <string.h>
#include "eci_synththread.h"
#include "evv_abi.h"
#include "delta.h"
#include "klatt_lang.h"
#include "eci_tvqueue.h"

/* A moment too big for sixteen bits is written as this, then in full. */
#define ESCAPE 0xffff

/* A stream is a queue with a name and a memory of where it got to. */
typedef struct StreamArray {
    TimeValueQueue queue;  /* +0x00, the queue's own */
    char    *name;        /* +0x0c */
    uint32_t written;     /* +0x10, the last moment written */
    uint32_t read;        /* +0x14, the last moment read back */
} StreamArray;

/* One element, and the room the count in front of an array needs. The
   count has to be a whole element's worth so that what follows it is
   aligned as an element wants to be. */
#define STREAM_BYTES ((int32_t)sizeof(StreamArray))
#define STREAM_HEAD  ((int32_t)sizeof(StreamArray))

typedef struct StreamArrayList {
    StreamArray *streams; /* +0x00 */
    int16_t      count;   /* +0x04, how many the language asked for */
    int16_t      named;   /* +0x06, how many have been given names */
} StreamArrayList;

/* Where the list hangs off the machine, and the moment a cleared stream
   starts counting from. */
#define GEN(d)       EVV_AT(DeltaLang *, ((delta_state *)(d))->dlang)
#define GEN_LIST(d)  (*(StreamArrayList **)&GEN(d)->list)
#define GEN_ZERO(d)  (GEN(d)->zero)

extern void *cpp_new(uint32_t n) MANGLED("??2@YAPAXI@Z");
extern void  cpp_delete(void *p) MANGLED("??3@YAXPAX@Z");
extern int   ralStrIcmp(int n, const char *a, const char *b);

extern THIS void *tvq_ctor(void *q, uint16_t room)
    MANGLED("??0TimeValueQueue@@QAE@G@Z");
extern THIS void tvq_dtor(void *q) MANGLED("??1TimeValueQueue@@QAE@XZ");
extern THIS int32_t tvq_push(void *q, TimeValuePair p)
    MANGLED("?push@TimeValueQueue@@QAEHUTimeValuePair_tag@@@Z");
extern THIS int32_t tvq_pop(void *q, TimeValuePair *p)
    MANGLED("?pop@TimeValueQueue@@QAEHPAUTimeValuePair_tag@@@Z");
extern THIS int32_t tvq_reset(void *q) MANGLED("?reset@TimeValueQueue@@QAEHXZ");
extern THIS int32_t tvq_isEmpty(void *q)
    MANGLED("?isEmpty@TimeValueQueue@@QAEHXZ");

/* The machine is only ever passed through here, never looked into. */
/* The machine, under the name this file already used for it. */
typedef delta_state delta_state_fwd;

/* How much room a stream's queue starts with. */
#define QUEUE_ROOM 0x12c

THIS void sa_dtor(StreamArray *s);
THIS void sa_deleteSegment(StreamArray *s, delta_state_fwd *d, uint32_t a,
                           uint32_t b);

/* ---- one stream ----------------------------------------------------- */

THIS void sa_dtor(StreamArray *s)
{
    if (s->name) {
        cpp_delete(s->name);
        s->name = 0;
    }
    tvq_dtor(&s->queue);
}

/* The compiler's array-aware deleting destructor: bit one says this is an
   array, in which case the count sits in the word in front of it and every
   element is destroyed from the back. */
THIS void *sa_destroy(StreamArray *s, int32_t how)
{
    if (how & 2) {
        int32_t  n = *(int32_t *)((char *)s - STREAM_HEAD);
        int32_t  stride = STREAM_BYTES;
        char    *at = (char *)s + stride * n;

        while (--n >= 0) {
            at -= stride;
            sa_dtor((StreamArray *)at);
        }

        if (how & 1)
            cpp_delete((char *)s - STREAM_HEAD);
        return (char *)s - STREAM_HEAD;
    }

    sa_dtor(s);
    if (how & 1)
        cpp_delete(s);
    return s;
}

THIS void sa_setName(StreamArray *s, const char *name)
{
    if (s->name)
        cpp_delete(s->name);

    s->name = (char *)cpp_new((uint32_t)strlen(name) + 1);
    strcpy(s->name, name);
}

/* Store one value at one moment. The gap from the last moment goes in as
   sixteen bits if it fits; if it does not, all-ones goes in first and the
   moment follows in full, split across the next pair. */
THIS int32_t sa_appendValue(StreamArray *s, uint32_t when, int16_t value)
{
    TimeValuePair p;
    uint32_t      gap;
    int32_t       ok = 1;

    /* Time only goes forwards. */
    if (when < s->written)
        return 0;

    gap = when - s->written;
    p.value = value;

    if (gap < ESCAPE) {
        p.time = (uint16_t)gap;
        ok = tvq_push(&s->queue, p);
    } else {
        p.time = ESCAPE;
        ok = tvq_push(&s->queue, p);
        if (ok) {
            p.time = (uint16_t)(when >> 16);
            p.value = (int16_t)(when & 0xffff);
            ok = tvq_push(&s->queue, p);
        }
    }

    s->written = when;
    return ok;
}

/* Read the next pair back, putting an escaped moment together again. */
THIS int32_t sa_fetchNext(StreamArray *s, uint32_t *when, int32_t *value)
{
    TimeValuePair p;

    if (tvq_isEmpty(&s->queue))
        return 0;

    tvq_pop(&s->queue, &p);
    *value = p.value;

    if (p.time == ESCAPE) {
        if (tvq_isEmpty(&s->queue))
            return 0;
        tvq_pop(&s->queue, &p);
        *when = ((uint32_t)p.time << 16) | (uint16_t)p.value;
    } else {
        *when = p.time + s->read;
    }

    s->read = *when;
    return 1;
}

/* Declared, called from two places, and empty in the original. */
THIS void sa_deleteSegment(StreamArray *s, delta_state_fwd *d, uint32_t a,
                           uint32_t b)
{
    (void)s;
    (void)d;
    (void)a;
    (void)b;
}

/* ---- the whole set -------------------------------------------------- */

/* One block holding the count in front of the streams, which is what the
   array-aware destructor above expects to find there. */
THIS int32_t sal_build(StreamArrayList *l, void *d, int16_t count)
{
    int32_t  n = count;
    char *room = cpp_new((uint32_t)(n * STREAM_BYTES + STREAM_HEAD));

    (void)d;

    if (room) {
        StreamArray *at;
        int32_t      left = n;

        *(int32_t *)room = n;
        at = (StreamArray *)(room + STREAM_HEAD);
        while (--left >= 0) {
            tvq_ctor(&at->queue, QUEUE_ROOM);
            at->name = 0;
            at->written = 0;
            at = (StreamArray *)((char *)at + STREAM_BYTES);
        }
        l->streams = (StreamArray *)(room + STREAM_HEAD);
    } else {
        l->streams = 0;
    }

    if (!l->streams)
        return 0;

    l->count = count;
    return 1;
}

THIS void sal_dtor(StreamArrayList *l)
{
    if (l->streams) {
        sa_destroy(l->streams, 3);
        l->streams = 0;
    }
}

/* Names are handed out in order; the answer is which stream got it. */
THIS int32_t sal_addName(StreamArrayList *l, const char *name)
{
    StreamArray *s;

    if (l->named >= l->count)
        return -1;

    s = &l->streams[l->named];
    l->named = (int16_t)(l->named + 1);
    sa_setName(s, name);
    return l->named - 1;
}

THIS int16_t sal_findStream(const StreamArrayList *l, const char *name)
{
    int16_t i;

    for (i = 0; i < l->count; i++) {
        if (ralStrIcmp(0, name, l->streams[i].name) == 0)
            return i;
    }
    return -1;
}

/* Emptying a stream puts its clock back to wherever the engine is now. */
THIS void sal_clear(StreamArrayList *l, void *d, int16_t which)
{
    StreamArray *s = &l->streams[which];

    tvq_reset(&s->queue);
    s->written = GEN_ZERO((delta_state_fwd *)d);
    s->read = 0;
}

THIS void sal_clearAll(StreamArrayList *l, void *d)
{
    int16_t i;

    for (i = 0; i < l->count; i++) {
        StreamArray *s = &l->streams[i];

        tvq_reset(&s->queue);
        s->written = GEN_ZERO((delta_state_fwd *)d);
        s->read = 0;
    }
}

THIS void sal_deleteSegment(StreamArrayList *l, void *d, int16_t which,
                            uint32_t a, uint32_t b)
{
    sa_deleteSegment(&l->streams[which], (delta_state_fwd *)d, a, b);
}

THIS void sal_deleteAllSegments(StreamArrayList *l, void *d, uint32_t a,
                                uint32_t b)
{
    int16_t i;

    for (i = 0; i < l->count; i++)
        sa_deleteSegment(&l->streams[i], (delta_state_fwd *)d, a, b);
}

/* ---- what the rules call -------------------------------------------- */

int32_t stmarray_new(void *d)
{
    StreamArrayList *l;

    if (!d || !GEN(d))
        return 0;

    l = (StreamArrayList *)cpp_new(sizeof(StreamArrayList));
    if (l) {
        l->streams = 0;
        l->count = 0;
        l->named = 0;
    }
    GEN_LIST(d) = l;

    if (!GEN_LIST(d))
        return -2;
    return 0;
}

int32_t stmarray_delete(void *d)
{
    if (!d || !GEN(d))
        return 0;

    if (GEN_LIST(d)) {
        sal_dtor(GEN_LIST(d));
        cpp_delete(GEN_LIST(d));
    }
    GEN_LIST(d) = 0;
    return 0;
}

int32_t createStreamArrays(void *d, int16_t count)
{
    if (count <= 0)
        return 0;
    if (GEN_LIST(d)->streams != 0)
        return 0;
    if (!sal_build(GEN_LIST(d), d, count))
        return 0;
    return 1;
}

int32_t initStreamArray(void *d, int16_t which, const char *name)
{
    return sal_addName(GEN_LIST(d), name) == which;
}

int32_t streamArrayCount(void *d)
{
    return GEN_LIST(d)->count;
}

int32_t arrayStreamFind(void *d, const char *name)
{
    return sal_findStream(GEN_LIST(d), name);
}

int32_t arrayStreamLastOffset(void *d, int16_t which)
{
    return (int32_t)GEN_LIST(d)->streams[which].written;
}

int32_t arrayStreamFirstVal(void *d, int16_t which, uint32_t *when,
                            int32_t *value)
{
    return sa_fetchNext(&GEN_LIST(d)->streams[which], when, value);
}

int32_t arrayStreamNextVal(void *d, int16_t which, uint32_t *when,
                           int32_t *value)
{
    return sa_fetchNext(&GEN_LIST(d)->streams[which], when, value);
}

int32_t resetStreamArrayC(void *d)
{
    sal_clearAll(GEN_LIST(d), d);
    return 0;
}

int32_t resetStreamArrayStreamC(void *d, const int16_t *which)
{
    sal_clear(GEN_LIST(d), d, which[1]);
    return 0;
}

int32_t deleteStreamArray(void *d, const int32_t *a, const int32_t *b)
{
    sal_deleteAllSegments(GEN_LIST(d), d, (uint32_t)a[1], (uint32_t)b[1]);
    return 0;
}

int32_t deleteStreamArrayStream(void *d, const int16_t *which,
                                const int32_t *a, const int32_t *b)
{
    sal_deleteSegment(GEN_LIST(d), d, which[1], (uint32_t)a[1],
                      (uint32_t)b[1]);
    return 0;
}

/* One value into the stream a pointer register names. */
int32_t addStreamArrayPtValC(void *d, const int16_t *stream,
                             const int16_t *when, const int32_t *value)
{
    return sa_appendValue(&GEN_LIST(d)->streams[stream[1]],
                          (uint32_t)value[1], when[1]) == 0;
}

/* The same value into two streams at once. */
int32_t addStreamArraySsValC(void *d, const int16_t *stream,
                             const int16_t *when, const int32_t *first,
                             const int32_t *second)
{
    if (!sa_appendValue(&GEN_LIST(d)->streams[stream[1]],
                        (uint32_t)first[1], when[1]))
        return 1;

    return sa_appendValue(&GEN_LIST(d)->streams[stream[1]],
                          (uint32_t)second[1], when[1]) == 0;
}

/* The original prints nothing here. */
int32_t printStreamArrays(void)
{
    return 0;
}

int32_t printAllStreamArrays(void)
{
    return 0;
}

ALIAS("??1StreamArray@@QAE@XZ", "sa_dtor");
ALIAS("??_EStreamArray@@QAEPAXI@Z", "sa_destroy");
ALIAS("?setName@StreamArray@@QAEXPBD@Z", "sa_setName");
ALIAS("?appendValue@StreamArray@@QAEHKF@Z", "sa_appendValue");
ALIAS("?fetchNext@StreamArray@@QAEHPAKPAH@Z", "sa_fetchNext");
ALIAS("?deleteSegment@StreamArray@@QAEXPAUDelta_This_Struct@@KK@Z",
      "sa_deleteSegment");
ALIAS("??1StreamArrayList@@QAE@XZ", "sal_dtor");
ALIAS("?build@StreamArrayList@@QAEHPAUDelta_This_Struct@@F@Z", "sal_build");
ALIAS("?addName@StreamArrayList@@QAEHPBD@Z", "sal_addName");
ALIAS("?findStream@StreamArrayList@@QBEFPBD@Z", "sal_findStream");
ALIAS("?clear@StreamArrayList@@QAEXPAUDelta_This_Struct@@F@Z", "sal_clear");
ALIAS("?clearAll@StreamArrayList@@QAEXPAUDelta_This_Struct@@@Z",
      "sal_clearAll");
ALIAS("?deleteSegment@StreamArrayList@@QAEXPAUDelta_This_Struct@@FKK@Z",
      "sal_deleteSegment");
ALIAS("?deleteAllSegments@StreamArrayList@@QAEXPAUDelta_This_Struct@@KK@Z",
      "sal_deleteAllSegments");
