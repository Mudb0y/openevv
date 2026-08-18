/* Blocks of one fixed size, handed out by number.
 *
 * Everything is one array of equal-sized blocks and two lists threaded
 * through them: the free ones and the ones in use. A block's first two words
 * are its place in whichever list it is on, its third says whether it is in
 * use, and the caller's own bytes start after that. So the lists cost no
 * memory of their own, and a handle is a block's position in the array
 * counted from one -- nought being free to mean nothing.
 *
 * Running out grows the array rather than failing: a new one at least twice
 * the size, the old contents copied over, and every block past the old end
 * strung onto the end of the free list. Handles survive that, because a
 * handle is a position and positions do not move.
 *
 * Giving a block back wipes it, so nothing of the last user is left for the
 * next one to find.
 */

#include <stdint.h>
#include <string.h>
#include "eci_synththread.h"
#include "evv_abi.h"

extern void *cpp_new(uint32_t n) MANGLED("??2@YAPAXI@Z");
extern void  cpp_delete(void *p) MANGLED("??3@YAXPAX@Z");

typedef struct MemoryManager {
    int32_t  stride;      /* +0x00, one block, links and all */
    char    *base;        /* +0x04 */
    int32_t  capacity;    /* +0x08, how many blocks there are */
    int32_t  freeHead;    /* +0x0c */
    int32_t  freeTail;    /* +0x10 */
    int32_t  freeCount;   /* +0x14 */
    int32_t  usedHead;    /* +0x18 */
    int32_t  usedTail;    /* +0x1c */
    int32_t  usedCount;   /* +0x20 */
} MemoryManager;

/* What every block carries in front of the caller's bytes. */
#define LINKS  0x0c
#define BLOCK(m, h)  ((m)->base + ((h) - 1) * (m)->stride)
#define PREV(m, h)   (*(int32_t *)(BLOCK(m, h) + 0))
#define NEXT(m, h)   (*(int32_t *)(BLOCK(m, h) + 4))
#define INUSE(m, h)  (*(int32_t *)(BLOCK(m, h) + 8))

THIS int32_t mm_ensureFreeMemory(MemoryManager *m);

/* The size asked for is the caller's; the block is that plus its links. */
THIS MemoryManager *mm_ctor(MemoryManager *m, uint32_t size)
{
    m->stride    = (int32_t)size + LINKS;
    m->base      = 0;
    m->capacity  = 0;
    m->freeHead  = 0;
    m->freeTail  = 0;
    m->freeCount = 0;
    m->usedHead  = 0;
    m->usedTail  = 0;
    m->usedCount = 0;
    return m;
}

THIS void mm_dtor(MemoryManager *m)
{
    if (!m->base)
        return;

    cpp_delete(m->base);
    m->base      = 0;
    m->usedTail  = 0;
    m->usedHead  = 0;
    m->freeTail  = 0;
    m->freeHead  = 0;
    m->capacity  = 0;
    m->usedCount = 0;
    m->freeCount = 0;
}

/* String blocks first through last onto a free list running forwards. Each
   one points back at the one before and on at the one after, which leaves
   the first pointing back at the one before it -- the caller fixes up
   whichever end it means to join on to. */
static void mm_thread(MemoryManager *m, int32_t first, int32_t *lastOut)
{
    char       *q     = m->base + (first - 1) * m->stride;
    const char *limit = m->base + m->capacity * m->stride;
    int32_t     i     = first;

    while (q < limit) {
        *(int32_t *)q       = i - 1;
        *(int32_t *)(q + 4) = i + 1;
        q += m->stride;
        i++;
    }
    *lastOut = i;
}

/* Make sure there is something to hand out. Nothing free means a bigger
   array: twice what there is, or as many as fit in a kilobyte, whichever is
   more. Answers false only if the allocation failed. */
THIS int32_t mm_ensureFreeMemory(MemoryManager *m)
{
    int32_t  wanted;
    int32_t  perK;
    char    *fresh;
    int32_t  first;
    int32_t  after;

    if (m->freeCount != 0)
        return 1;

    perK = (int32_t)(0x400u / (uint32_t)m->stride);
    wanted = (perK > m->capacity * 2) ? perK : m->capacity * 2;

    fresh = (char *)cpp_new((uint32_t)(wanted * m->stride));
    if (!fresh)
        return 0;

    memset(fresh, 0, (size_t)(wanted * m->stride));
    if (m->base) {
        memcpy(fresh, m->base, (size_t)(m->capacity * m->stride));
        cpp_delete(m->base);
    }

    first      = m->capacity + 1;
    m->base    = fresh;
    m->capacity = wanted;

    mm_thread(m, first, &after);
    m->freeCount = after - first;

    after--;
    NEXT(m, after) = 0;

    /* Join the new run on behind whatever was already free. */
    if (m->freeTail != 0)
        NEXT(m, m->freeTail) = first;
    m->freeTail = after;
    if (m->freeHead == 0)
        m->freeHead = first;

    return 1;
}

/* Off the front of the free list and on to the front of the used one. */
THIS uint32_t mm_newMemory(MemoryManager *m)
{
    int32_t h;

    if (!mm_ensureFreeMemory(m))
        return 0;

    h = m->freeHead;
    m->freeHead = NEXT(m, h);
    if (m->freeHead == 0)
        m->freeTail = 0;
    else
        PREV(m, m->freeHead) = 0;
    m->freeCount--;

    INUSE(m, h) = 1;
    NEXT(m, h) = m->usedHead;
    if (m->usedHead == 0)
        m->usedTail = h;
    else
        PREV(m, m->usedHead) = h;
    PREV(m, h) = 0;
    m->usedHead = h;
    m->usedCount++;

    return (uint32_t)h;
}

/* Unstitch it from the used list, wipe it, and put it on the front of the
   free one. */
THIS void mm_deleteMemory(MemoryManager *m, uint32_t handle)
{
    int32_t h = (int32_t)handle;

    if (h == m->usedHead)
        m->usedHead = NEXT(m, h);
    else
        NEXT(m, PREV(m, h)) = NEXT(m, h);

    if (h == m->usedTail)
        m->usedTail = PREV(m, h);
    else
        PREV(m, NEXT(m, h)) = PREV(m, h);

    m->usedCount--;

    memset(BLOCK(m, h), 0, (size_t)m->stride);
    PREV(m, h) = 0;
    NEXT(m, h) = m->freeHead;
    if (m->freeHead != 0)
        PREV(m, m->freeHead) = h;
    else
        m->freeTail = h;
    m->freeHead = h;
    m->freeCount++;
}

/* Everything back at once: wipe the lot and rebuild one free list over it.
   The array itself is kept, so handles handed out afterwards start again
   from the beginning. */
THIS void mm_deleteAll(MemoryManager *m)
{
    int32_t last;

    if (!m->base)
        return;

    m->usedTail  = 0;
    m->usedHead  = 0;
    m->freeCount = m->capacity;
    m->usedCount = 0;

    memset(m->base, 0, (size_t)(m->capacity * m->stride));

    mm_thread(m, 1, &last);
    last--;
    NEXT(m, last) = 0;

    m->freeHead = 1;
    m->freeTail = last;
}

ALIAS("??0MemoryManager@@QAE@K@Z", "mm_ctor");
ALIAS("??1MemoryManager@@QAE@XZ", "mm_dtor");
ALIAS("?newMemory@MemoryManager@@QAEKXZ", "mm_newMemory");
ALIAS("?deleteMemory@MemoryManager@@QAEXK@Z", "mm_deleteMemory");
ALIAS("?deleteAll@MemoryManager@@QAEXXZ", "mm_deleteAll");
ALIAS("?ensureFreeMemory@MemoryManager@@AAEHXZ", "mm_ensureFreeMemory");
