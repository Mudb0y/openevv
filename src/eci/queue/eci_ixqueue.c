/* What an index queue does with time.
 *
 * The queue itself is an ESList of pairs, each pair an index mark and the
 * number of samples that must still go by before it is due. Those are built
 * and walked elsewhere; this file is only the arithmetic that keeps the two
 * halves of the bookkeeping agreeing with each other.
 *
 * There are two halves because asking "how long until the next mark" must be
 * cheap. The head pair carries the wait for the head mark alone, and the
 * queue carries the sum of every wait in it. So every change here touches
 * both: take time off the head, take the same off the total.
 *
 * Everything below reaches the list through its own virtual table rather
 * than through the fields, because the list is three classes deep and the
 * original never assumed which one it had.
 */

#include <stdint.h>
#include "eci_synththread.h"
#include "evv_abi.h"
#include "eci_index.h"

/* The list, plus the running total the queue adds on the end. */

#define IQ_SLOT(q, off) ((*(void ***)(q))[(off) / 4])
#define IQ_IS_EMPTY  0x00
#define IQ_HEAD      0x0c
#define IQ_ADD_TAIL  0x18

typedef THIS int32_t    (*IqEmptyFn)(IndexQueue *q);
typedef THIS IndexPair *(*IqHeadFn)(IndexQueue *q);
typedef THIS int32_t    (*IqAddFn)(IndexQueue *q, IndexPair p);

extern THIS void el_removeHead(IndexQueue *q) MANGLED("?removeHead@ESList@@UAEXXZ");

static IndexPair *iq_head(IndexQueue *q)
{
    return ((IqHeadFn)IQ_SLOT(q, IQ_HEAD))(q);
}

/* Put a mark on the end, waiting the given number of samples after whatever
   is already last. The total grows by the same amount only if the list took
   it; a list that refuses leaves the books untouched. */
THIS int32_t iq_addOffsetFromLast(IndexQueue *q, int32_t index, uint32_t offset)
{
    IndexPair p;

    p.index = index;
    p.lead  = offset;
    if (!((IqAddFn)IQ_SLOT(q, IQ_ADD_TAIL))(q, p))
        return 0;

    q->total += offset;
    return 1;
}

/* Time has passed: take it off the head. More time than the head was waiting
   for is not an error, it is simply all of it, and the answer says how much
   was actually used. */
THIS uint32_t iq_reduceLeadTime(IndexQueue *q, uint32_t by)
{
    if (by > iq_head(q)->lead)
        by = iq_head(q)->lead;

    iq_head(q)->lead  -= by;
    q->total          -= by;
    return by;
}

/* The same with no figure named: take all of it. */
THIS uint32_t iq_reduceLeadTimeAll(IndexQueue *q)
{
    uint32_t was = iq_head(q)->lead;

    iq_head(q)->lead = 0;
    q->total -= was;
    return was;
}

/* Push the head further out. On an empty queue there is no head to push and
   nothing to add to, so it does nothing at all. */
THIS void iq_increaseLeadTime(IndexQueue *q, uint32_t by)
{
    if (((IqEmptyFn)IQ_SLOT(q, IQ_IS_EMPTY))(q))
        return;

    q->total += by;
    iq_head(q)->lead += by;
}

/* Take the head off and say which mark it was. Whatever it was still waiting
   for comes off the total with it. */
THIS int32_t iq_remove(IndexQueue *q)
{
    int32_t index = iq_head(q)->index;

    q->total -= iq_head(q)->lead;
    el_removeHead(q);
    return index;
}

ALIAS("?addOffsetFromLast@IndexQueue@@QAEHHK@Z", "iq_addOffsetFromLast");
ALIAS("?reduceLeadTime@IndexQueue@@QAEKK@Z", "iq_reduceLeadTime");
ALIAS("?reduceLeadTime@IndexQueue@@QAEKXZ", "iq_reduceLeadTimeAll");
ALIAS("?increaseLeadTime@IndexQueue@@QAEXK@Z", "iq_increaseLeadTime");
ALIAS("?remove@IndexQueue@@QAEHXZ", "iq_remove");
