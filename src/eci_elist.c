/* The list the index queue is built on.
 *
 * A plain singly linked list of index pairs. Each link is twelve bytes: the
 * pair, then where the next one is. The list itself keeps both ends, so
 * adding at either end is cheap and nothing here ever has to walk to find
 * one. Empty means no head.
 *
 * Everything is virtual because the list is the bottom of a small hierarchy
 * -- a collection, then a list, then this -- and the index queue sits on top
 * of it. So the operations reach the list through its own table, which is
 * what lets the index queue's extra bookkeeping ride along.
 *
 * Two things here are transcribed rather than written. EList::reset empties
 * a list through the table rather than by name, so a queue that overrides
 * removal is emptied properly. And ECollection::size counts by walking an
 * iterator that was never attached to anything: as the original stands, the
 * first step goes through a table of pure virtuals and stops the program.
 * Nothing calls it, and it is kept exactly as it is so that if anything ever
 * does, both builds stop in the same place.
 */

#include <stdint.h>
#include "eci_synththread.h"
#include "evv_abi.h"
#include "eci_index.h"

extern void *cpp_new(uint32_t n) MANGLED("??2@YAPAXI@Z");
extern void  cpp_delete(void *p) MANGLED("??3@YAXPAX@Z");

/* A link: the pair, then the next link. */
typedef struct Link {
    IndexPair    pair;
    struct Link *next;
} Link;

typedef struct ESList {
    const void *vt;
    Link       *head;
    Link       *tail;
} ESList;

/* An iterator: whose list, and where in it. */
typedef struct ESListIter {
    const void *vt;
    ESList     *list;
    Link       *at;
} ESListIter;

#define VT(p)  (*(void ***)(p))

typedef THIS int32_t (*EmptyFn)(void *self);
typedef THIS void   *(*NewIterFn)(void *self);
typedef THIS int32_t (*InBoundsFn)(void *self);
typedef THIS void   *(*PostIncFn)(void *self, int32_t unused);

/* The tables our own code already fills in elsewhere. */
extern THIS int32_t  sti_esListIsEmpty(const ESList *l);
extern THIS int32_t  sti_indexQueueIsEmpty(const ESList *l);
extern THIS int32_t  sti_eListQueueIsEmpty(const ESList *l);
extern THIS void    *sti_indexQueueDestroy(ESList *l, int32_t free_it);
extern THIS void    *sti_eListQueueDestroy(ESList *l, int32_t free_it);
extern THIS void    *sti_esListHead(const ESList *l);
extern THIS void    *sti_esListTail(const ESList *l);
extern THIS void    *sti_esListDestroy(ESList *l, int32_t free_it);

const void *vtbl_eslist[11];
const void *vtbl_eslistiter[8];
const void *vtbl_elistiter[8];
const void *vtbl_eiterator[7];
const void *vtbl_ecollectiter[7];

extern void purecall(void) MANGLED("__purecall");

/* ---- the list ------------------------------------------------------- */

/* A new link at the front. An empty list gains a tail as well. */
THIS int32_t el_addToHead(ESList *l, IndexPair p)
{
    Link *link = (Link *)cpp_new(sizeof(Link));

    if (link) {
        Link *was = l->head;

        link->pair = p;
        link->next = was;
    }
    if (!link)
        return 0;

    if (((EmptyFn)VT(l)[0])(l))
        l->tail = link;
    l->head = link;
    return 1;
}

/* A new link at the back. An empty list gains a head as well. */
THIS int32_t el_addToTail(ESList *l, IndexPair p)
{
    Link *link = (Link *)cpp_new(sizeof(Link));

    if (link) {
        link->pair = p;
        link->next = 0;
    }
    if (!link)
        return 0;

    if (((EmptyFn)VT(l)[0])(l))
        l->head = link;
    else
        l->tail->next = link;
    l->tail = link;
    return 1;
}

/* A new link after wherever an iterator is standing. Standing on the last
   link, that is the same as adding at the back, and it says so through the
   table rather than doing it here. */
THIS int32_t el_addAfter(ESList *l, ESListIter *it, IndexPair p)
{
    Link *link;

    if (it->at == l->tail)
        return ((THIS int32_t (*)(ESList *, IndexPair))VT(l)[6])(l, p);

    link = (Link *)cpp_new(sizeof(Link));
    if (link) {
        link->pair = p;
        link->next = it->at->next;
    }
    if (!link)
        return 0;

    it->at->next = link;
    return 1;
}

/* Off the front. The last link takes both ends with it. */
THIS void el_removeHead(ESList *l)
{
    Link *going = l->head;

    if (l->head == l->tail) {
        l->tail = 0;
        l->head = 0;
    } else {
        l->head = l->head->next;
    }
    cpp_delete(going);
}

/* The link after wherever an iterator is standing. If that was the last
   one, the list's back end moves to the iterator. */
THIS void el_removeAfter(ESList *l, ESListIter *it)
{
    Link *going = it->at->next;

    it->at->next = going->next;
    if (going == l->tail)
        l->tail = it->at;
    cpp_delete(going);
}

/* An iterator of our own, standing at the front. Three tables go in one
   after another, which is what the compiler makes of three classes deep. */
THIS void *el_newIter(ESList *l)
{
    ESListIter *it = (ESListIter *)cpp_new(sizeof(ESListIter));

    if (!it)
        return 0;

    it->vt   = &vtbl_eiterator;
    it->vt   = &vtbl_elistiter;
    it->vt   = &vtbl_eslistiter;
    it->list = l;
    it->at   = l->head;
    return it;
}

/* Empty it, through the table, so an override gets its say. */
THIS void el_listReset(ESList *l)
{
    while (!((EmptyFn)VT(l)[0])(l))
        ((THIS void (*)(ESList *))VT(l)[8])(l);
}

/* ---- the iterator --------------------------------------------------- */

THIS int32_t el_iterInBounds(const ESListIter *it) { return it->at != 0; }
THIS void   *el_iterAt(const ESListIter *it)       { return it->at; }
THIS void   *el_iterNext(const ESListIter *it)     { return it->at->next; }
THIS int32_t el_iterAtEnd(const ESListIter *it)    { return it->at == it->list->tail; }
THIS void    el_iterReset(ESListIter *it)          { it->at = it->list->head; }
THIS void    el_iterResetToTail(ESListIter *it)    { it->at = it->list->tail; }

/* Step on, and say where it now is. */
THIS void *el_iterPreInc(ESListIter *it)
{
    it->at = it->at->next;
    return it->at;
}

/* Step on, and say where it was. */
THIS void *el_iterPostInc(ESListIter *it, int32_t unused)
{
    Link *was = it->at;

    (void)unused;
    it->at = it->at->next;
    return was;
}

/* ---- counting ------------------------------------------------------- */

/* Kept as the original wrote it. See the note at the top of the file. */
THIS int32_t el_collectionSize(void *self)
{
    ESListIter  loose;
    void       *it;
    int32_t     count = 0;

    loose.vt = &vtbl_eiterator;
    loose.vt = &vtbl_ecollectiter;

    it = ((NewIterFn)VT(self)[1])(self);
    while (((InBoundsFn)VT(it)[0])(it)) {
        ((PostIncFn)VT(&loose)[4])(&loose, 0);
        count++;
    }

    loose.vt = &vtbl_ecollectiter;
    cpp_delete(it);
    return count;
}

/* ---- the tables ----------------------------------------------------- */

const void *vtbl_eslist[11] = {
    (void *)sti_esListIsEmpty,
    (void *)el_newIter,
    (void *)el_collectionSize,
    (void *)sti_esListHead,
    (void *)sti_esListTail,
    (void *)el_addToHead,
    (void *)el_addToTail,
    (void *)el_addAfter,
    (void *)el_removeHead,
    (void *)el_removeAfter,
    (void *)sti_esListDestroy
};

const void *vtbl_eslistiter[8] = {
    (void *)el_iterInBounds,
    (void *)el_iterAt,
    (void *)el_iterAtEnd,
    (void *)el_iterNext,
    (void *)el_iterPostInc,
    (void *)el_iterPreInc,
    (void *)el_iterReset,
    (void *)el_iterResetToTail
};

/* The bases have nothing of their own but the one they hand down. */
const void *vtbl_eiterator[7] = {
    (void *)purecall, (void *)purecall, (void *)purecall, (void *)purecall,
    (void *)purecall, (void *)purecall, (void *)purecall
};

const void *vtbl_elistiter[8] = {
    (void *)purecall, (void *)purecall, (void *)purecall, (void *)purecall,
    (void *)purecall, (void *)purecall, (void *)purecall, (void *)purecall
};

const void *vtbl_ecollectiter[7] = {
    (void *)purecall, (void *)purecall, (void *)purecall, (void *)purecall,
    (void *)purecall, (void *)purecall, (void *)purecall
};

/* The two lists built on this one differ from it in exactly two places:
   what they call empty, and what they do when they are deleted. Everything
   between is the list's own. */
const void *vtbl_indexqueue[11] = {
    (void *)sti_indexQueueIsEmpty,
    (void *)el_newIter,
    (void *)el_collectionSize,
    (void *)sti_esListHead,
    (void *)sti_esListTail,
    (void *)el_addToHead,
    (void *)el_addToTail,
    (void *)el_addAfter,
    (void *)el_removeHead,
    (void *)el_removeAfter,
    (void *)sti_indexQueueDestroy
};

const void *vtbl_elistqueue[11] = {
    (void *)sti_eListQueueIsEmpty,
    (void *)el_newIter,
    (void *)el_collectionSize,
    (void *)sti_esListHead,
    (void *)sti_esListTail,
    (void *)el_addToHead,
    (void *)el_addToTail,
    (void *)el_addAfter,
    (void *)el_removeHead,
    (void *)el_removeAfter,
    (void *)sti_eListQueueDestroy
};

ALIAS("??_7ESList@@6B@", "vtbl_eslist");
ALIAS("??_7IndexQueue@@6B@", "vtbl_indexqueue");
ALIAS("??_7EListQueue@@6B@", "vtbl_elistqueue");
ALIAS("??_7ESListIter@@6B@", "vtbl_eslistiter");
ALIAS("??_7EListIter@@6B@", "vtbl_elistiter");
ALIAS("??_7EIterator@@6B@", "vtbl_eiterator");
ALIAS("??_7ECollectIter@@6B@", "vtbl_ecollectiter");

ALIAS("?addToHead@ESList@@UAEHUIndexPair@@@Z", "el_addToHead");
ALIAS("?addToTail@ESList@@UAEHUIndexPair@@@Z", "el_addToTail");
ALIAS("?addAfter@ESList@@UAEHAAVEListIter@@UIndexPair@@@Z", "el_addAfter");
ALIAS("?removeHead@ESList@@UAEXXZ", "el_removeHead");
ALIAS("?removeAfter@ESList@@UAEXAAVEListIter@@@Z", "el_removeAfter");
ALIAS("?newIter@ESList@@UBEPAVEIterator@@XZ", "el_newIter");
ALIAS("?reset@EList@@QAEXXZ", "el_listReset");
ALIAS("?size@ECollection@@UBEHXZ", "el_collectionSize");

ALIAS("?inBounds@ESListIter@@UBEHXZ", "el_iterInBounds");
ALIAS("??RESListIter@@UBEAAUIndexPair@@XZ", "el_iterAt");
ALIAS("?atEnd@ESListIter@@UBEHXZ", "el_iterAtEnd");
ALIAS("?next@ESListIter@@UBEAAUIndexPair@@XZ", "el_iterNext");
ALIAS("??EESListIter@@UAEAAUIndexPair@@H@Z", "el_iterPostInc");
ALIAS("??EESListIter@@UAEAAUIndexPair@@XZ", "el_iterPreInc");
ALIAS("?reset@ESListIter@@UAEXXZ", "el_iterReset");
ALIAS("?resetToTail@ESListIter@@UAEXXZ", "el_iterResetToTail");
