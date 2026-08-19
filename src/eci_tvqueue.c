/* A ring of times and values, growing and shrinking as it is used.

   The synthesis thread keeps what it has to report — an index mark reached,
   a word begun — against the sample count it happens at, and hands them to
   the caller as the audio is drained. The ring doubles when it fills and
   halves when it empties past a point, never going below the size it was
   asked for.

   This is the first class of the original's C++ that is ours, so it is also
   where the calling convention is settled: the original's methods take
   their object in a register and clean their own arguments off the stack,
   and are named the way the Microsoft compiler names them. The functions
   below say both of those things explicitly, and the assembler aliases at
   the end give each one the name its callers already use. Nothing else is
   needed to stand in for a class with no virtual functions. */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "evv_abi.h"
#include "evv_arena.h"
#include "eci_tvqueue.h"

THIS uint16_t tvq_size(TimeValueQueue *q)
{
    if (q->head > q->tail)
        return (uint16_t)(q->room - q->head + q->tail);
    return (uint16_t)(q->tail - q->head);
}

THIS uint16_t tvq_totalSize(TimeValueQueue *q)
{
    return q->room;
}

THIS int32_t tvq_isEmpty(TimeValueQueue *q)
{
    return (q->buf == 0 || q->head == q->tail) ? 1 : 0;
}

THIS TimeValueQueue *tvq_ctor(TimeValueQueue *q, uint16_t n)
{
    q->buf = 0;
    q->room = n;
    q->head = 0;
    q->tail = 0;
    q->want = n;
    q->buf = malloc((size_t)n * sizeof *q->buf);
    if (q->buf == 0)
        q->room = 0;
    return q;
}

THIS void tvq_dtor(TimeValueQueue *q)
{
    if (q->buf == 0)
        return;
    free(q->buf);
    q->buf = 0;
    q->room = 0;
    q->tail = 0;
    q->head = 0;
}

THIS int32_t tvq_reset(TimeValueQueue *q)
{
    if (q->buf != 0)
        free(q->buf);
    q->buf = malloc((size_t)q->want * sizeof *q->buf);
    if (q->buf == 0)
        return 0;
    q->room = q->want;
    q->tail = 0;
    q->head = 0;
    return 1;
}

/* Twice the room, with the wrapped part unwrapped into the new half.
   The original reaches through the answer before it checks it; the check
   comes first here, because a failed allocation would otherwise be a
   fault rather than a refusal. */
static THIS int32_t tvq_expand(TimeValueQueue *q)
{
    uint16_t bigger;
    TimeValuePair *p;

    if (q->buf == 0)
        return 0;
    bigger = (uint16_t)(q->room * 2);
    p = realloc(q->buf, (size_t)bigger * sizeof *p);
    if (p == 0)
        return 0;
    memmove(p + q->room, p, (size_t)q->tail * sizeof *p);
    memmove(p, p + q->head, (size_t)q->room * sizeof *p);
    q->buf = p;
    q->head = 0;
    q->tail = q->room;
    q->room = bigger;
    return 1;
}

/* Half the room once less than half of it is in use, but never below the
   size it was asked for. What is in it is straightened out first. */
static THIS int32_t tvq_shrinkIfNeeded(TimeValueQueue *q)
{
    int32_t used;
    int moved = 0;

    if (q->tail > q->head) {
        used = q->tail - q->head;
        if (q->room > q->want && used < q->room / 2) {
            memmove(q->buf, q->buf + q->head, (size_t)used * sizeof *q->buf);
            moved = 1;
        }
    } else {
        used = q->room - q->head + q->tail;
        if (q->room > q->want && used < q->room / 2) {
            memmove(q->buf + (q->room - q->head), q->buf,
                    (size_t)q->tail * sizeof *q->buf);
            memmove(q->buf, q->buf + q->head,
                    (size_t)(q->room - q->head) * sizeof *q->buf);
            moved = 1;
        }
    }

    if (moved) {
        uint16_t smaller = (uint16_t)(q->room / 2);
        TimeValuePair *p = realloc(q->buf, (size_t)smaller * sizeof *p);

        if (p == 0)
            return 0;
        q->buf = p;
        q->head = 0;
        q->tail = (uint16_t)used;
        q->room = smaller;
    }
    return 1;
}

/* Filling the ring grows it; failing to grow puts the tail back where it
   was, so nothing is lost and the caller is told. */
THIS int32_t tvq_push(TimeValueQueue *q, TimeValuePair p)
{
    if (q->buf == 0)
        return 0;

    q->buf[q->tail] = p;
    q->tail++;
    if (q->tail == q->room)
        q->tail = 0;

    if (q->tail == q->head && !tvq_expand(q)) {
        if (q->tail == 0)
            q->tail = (uint16_t)(q->room - 1);
        else
            q->tail--;
        return 0;
    }
    return 1;
}

THIS int32_t tvq_peekHead(TimeValueQueue *q, TimeValuePair *out)
{
    int32_t ok = (q->buf != 0 && !tvq_isEmpty(q)) ? 1 : 0;

    if (ok)
        *out = q->buf[q->head];
    return ok;
}

THIS int32_t tvq_pop(TimeValueQueue *q, TimeValuePair *out)
{
    int32_t ok = tvq_peekHead(q, out);

    if (!ok)
        return ok;
    q->head++;
    if (q->head == q->room)
        q->head = 0;
    return tvq_shrinkIfNeeded(q);
}

/* Everything in it, in order, in a block the caller then owns. */
THIS TimeValuePair *tvq_getElements(TimeValueQueue *q, int32_t *n)
{
    int32_t have = tvq_size(q);
    TimeValuePair *out = malloc((size_t)have * sizeof *out);
    int32_t i;

    if (out == 0)
        return 0;
    for (i = 0; i < have; i++)
        out[i] = q->buf[(q->head + i) % q->room];
    *n = have;
    return out;
}

ALIAS("??0TimeValueQueue@@QAE@G@Z", "tvq_ctor");
ALIAS("??1TimeValueQueue@@QAE@XZ", "tvq_dtor");
ALIAS("?size@TimeValueQueue@@QAEGXZ", "tvq_size");
ALIAS("?totalSize@TimeValueQueue@@QAEGXZ", "tvq_totalSize");
ALIAS("?reset@TimeValueQueue@@QAEHXZ", "tvq_reset");
ALIAS("?isEmpty@TimeValueQueue@@QAEHXZ", "tvq_isEmpty");
ALIAS("?push@TimeValueQueue@@QAEHUTimeValuePair_tag@@@Z", "tvq_push");
ALIAS("?pop@TimeValueQueue@@QAEHPAUTimeValuePair_tag@@@Z", "tvq_pop");
ALIAS("?peekHead@TimeValueQueue@@QAEHPAUTimeValuePair_tag@@@Z",
      "tvq_peekHead");
ALIAS("?getElements@TimeValueQueue@@QAEPAUTimeValuePair_tag@@PAH@Z",
      "tvq_getElements");
