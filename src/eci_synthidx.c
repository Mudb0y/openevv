/* Index marks in the queue, and the name of a language.

   Small classes the synthesis thread is built out of. The index queue is one
   of the library's collections: a singly linked list with a queue over it and
   an index queue over that, which is why constructing one writes five tables
   into the same word in turn and taking one apart empties it twice.

   The index manager is a fixed-size allocator with a lock round it. An index
   is named by a number rather than by a pointer, because the number has to
   survive being handed to the engine and given back later.

   Names are prefixed; the aliases at the foot carry the real ones. */

#include <stdint.h>
#include <stdio.h>
#include "eci_synththread.h"
#include "evv_abi.h"

/* Every one of the collection tables the construction chain passes through.
   They belong to the original; only the methods below are ours. */
extern const void *vtbl_eCollection MANGLED("??_7ECollection@@6B@");
extern const void *vtbl_eList MANGLED("??_7EList@@6B@");
extern const void *vtbl_eslist MANGLED("??_7ESList@@6B@");
extern const void *vtbl_elistqueue MANGLED("??_7EListQueue@@6B@");
extern const void *vtbl_indexqueue MANGLED("??_7IndexQueue@@6B@");

extern THIS void el_listReset(void *l) MANGLED("?reset@EList@@QAEXXZ");
extern THIS void lang_setString(LangIdentifier *l)
    MANGLED("?setString@LangIdentifier@@AAEXXZ");

extern THIS uint32_t mm_newMemory(void *m)
    MANGLED("?newMemory@MemoryManager@@QAEKXZ");
extern THIS void mm_deleteMemory(void *m, uint32_t id)
    MANGLED("?deleteMemory@MemoryManager@@QAEXK@Z");
extern THIS void mm_deleteAll(void *m)
    MANGLED("?deleteAll@MemoryManager@@QAEXXZ");

/* A list: what it holds, then its two ends. The queue and the index queue
   each add one word after that. */
typedef struct {
    const void *vt;     /* +0x00 */
    void *head;         /* +0x04 */
    void *tail;         /* +0x08 */
    int32_t extra;      /* +0x0c, the index queue's own */
} List;

typedef IndexManager IndexMemory;
#define IDXMGR_LOCK(m) ((void *)(m)->lock)
/* The blocks start a little way past whatever the allocator keeps first. */
#define IDXMGR_FIRST   0x0c

/* ---- the collections ---- */

/* Empty on all three counts means the same thing: nothing at the head. */
THIS int32_t sti_esListIsEmpty(const List *l)   { return l->head == 0; }
THIS int32_t sti_eListQueueIsEmpty(const List *l) { return l->head == 0; }
THIS int32_t sti_indexQueueIsEmpty(const List *l) { return l->head == 0; }

THIS void *sti_esListHead(const List *l) { return l->head; }
THIS void *sti_esListTail(const List *l) { return l->tail; }

/* Build the index queue by walking up its own inheritance, which is what the
   compiler makes of a class three deep: each base puts its table in and the
   one above it puts its own in over the top. */
THIS List *sti_indexQueueCtor(List *l)
{
    l->vt = &vtbl_eCollection;
    l->vt = &vtbl_eList;
    l->vt = &vtbl_eslist;
    l->head = 0;
    l->tail = 0;
    l->vt = &vtbl_elistqueue;
    l->vt = &vtbl_indexqueue;
    l->extra = 0;
    return l;
}

/* And take it apart by walking back down, emptying it once at each level it
   was built at. */
THIS void *sti_indexQueueDestroy(List *l, int32_t free_it)
{
    l->vt = &vtbl_elistqueue;
    el_listReset(l);
    l->vt = &vtbl_eslist;
    el_listReset(l);
    if (free_it & 1)
        cpp_delete(l);
    return l;
}

THIS void *sti_eListQueueDestroy(List *l, int32_t free_it)
{
    l->vt = &vtbl_elistqueue;
    el_listReset(l);
    l->vt = &vtbl_eslist;
    el_listReset(l);
    if (free_it & 1)
        cpp_delete(l);
    return l;
}

THIS void *sti_esListDestroy(List *l, int32_t free_it)
{
    l->vt = &vtbl_eslist;
    el_listReset(l);
    if (free_it & 1)
        cpp_delete(l);
    return l;
}

/* Is the mark at the head of the queue due now? It is if there is one and it
   has no time left to wait. */
THIS int32_t sti_indexDue(const List *l)
{
    typedef struct {
        THIS int32_t (*isEmpty)(const List *);
        THIS void *(*newIter)(const List *);
        THIS int32_t (*size)(const List *);
        THIS int32_t *(*head)(const List *);
    } CollectionVtbl;
    const CollectionVtbl *vt = (const CollectionVtbl *)l->vt;

    if (vt->isEmpty(l))
        return 0;
    /* The head is a pair: what the mark is, and how far off it still is. */
    return vt->head(l)[1] == 0;
}

/* ---- the index manager ---- */

/* Take a number for a new mark. Everything here runs under the lock because
   marks are made on the caller's thread and used on ours. */
THIS uint32_t sti_newIndex(IndexMemory *m)
{
    void *lock = IDXMGR_LOCK(m);
    uint32_t id;

    sy_mutexWait(lock, -1);
    id = mm_newMemory(m);
    sy_mutexRelease(lock);
    return id;
}

/* And turn a number back into the record it stands for. The numbers start at
   one so that nought can mean no mark at all. */
THIS Index *sti_getIndex(IndexMemory *m, uint32_t id)
{
    void *lock = IDXMGR_LOCK(m);
    Index *ix;

    sy_mutexWait(lock, -1);
    ix = (Index *)(m->base + IDXMGR_FIRST + (int32_t)(id - 1) * m->stride);
    sy_mutexRelease(lock);
    return ix;
}

THIS void sti_deleteIndex(IndexMemory *m, uint32_t id)
{
    void *lock = IDXMGR_LOCK(m);

    sy_mutexWait(lock, -1);
    mm_deleteMemory(m, id);
    sy_mutexRelease(lock);
}

THIS void sti_deleteAll(IndexMemory *m)
{
    void *lock = IDXMGR_LOCK(m);

    sy_mutexWait(lock, -1);
    mm_deleteAll(m);
    sy_mutexRelease(lock);
}

/* ---- the name of a language ---- */

/* A language starts as nought, which is a real language rather than none:
   the first of the first family. The printable form is made from the number
   and kept beside it, so the two can never disagree. */
THIS LangIdentifier *sti_langCtor(LangIdentifier *l)
{
    l->packed = 0;
    l->packed = (0u << 16) | (0u << 8) | 0u;
    lang_setString(l);
    return l;
}

ALIAS("?isEmpty@ESList@@UBEHXZ", "sti_esListIsEmpty");
ALIAS("?isEmpty@EListQueue@@UBEHXZ", "sti_eListQueueIsEmpty");
ALIAS("?isEmpty@IndexQueue@@UBEHXZ", "sti_indexQueueIsEmpty");
ALIAS("?head@ESList@@UBEAAUIndexPair@@XZ", "sti_esListHead");
ALIAS("?tail@ESList@@UBEAAUIndexPair@@XZ", "sti_esListTail");
ALIAS("??0IndexQueue@@QAE@XZ", "sti_indexQueueCtor");
ALIAS("??_GIndexQueue@@UAEPAXI@Z", "sti_indexQueueDestroy");
ALIAS("??_EIndexQueue@@UAEPAXI@Z", "sti_indexQueueDestroy");
ALIAS("??_GEListQueue@@UAEPAXI@Z", "sti_eListQueueDestroy");
ALIAS("??_EEListQueue@@UAEPAXI@Z", "sti_eListQueueDestroy");
ALIAS("??_GESList@@UAEPAXI@Z", "sti_esListDestroy");
ALIAS("??_EESList@@UAEPAXI@Z", "sti_esListDestroy");
ALIAS("?indexDue@IndexQueue@@QBEHXZ", "sti_indexDue");
ALIAS("?newIndex@IndexManager@@QAEKXZ", "sti_newIndex");
ALIAS("?getIndex@IndexManager@@QAEPAUIndex@@K@Z", "sti_getIndex");
ALIAS("?deleteIndex@IndexManager@@QAEXK@Z", "sti_deleteIndex");
ALIAS("?deleteAll@IndexManager@@QAEXXZ", "sti_deleteAll");
ALIAS("??0LangIdentifier@@QAE@XZ", "sti_langCtor");
