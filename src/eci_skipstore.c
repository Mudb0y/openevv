/* The stored dictionary: a skip list of keys and what they mean.
 *
 * A skip list is an ordered list with a tower of forward pointers over it, each
 * level skipping about four times as far as the one below, so a lookup halves
 * and halves again rather than walking. How high a new entry's tower goes is
 * drawn at random, which is what makes the structure balance itself without
 * ever being rebalanced.
 *
 * Two things follow from that random draw and both matter.
 *
 * The list is seeded from the clock: the constructor calls srand(time(0)). So
 * two runs build different towers over the same entries, and a file saved twice
 * is not the same file twice. That is IBM's own behaviour, not ours, and it is
 * why test/romprims.sh compares what this answers rather than what it writes.
 * Nothing else in this engine uses rand, so the seeding disturbs nothing.
 *
 * The answers themselves do not depend on the draw. Order decides them, and
 * the towers only decide how quickly they are reached. That is what makes this
 * comparable at all.
 *
 * Who uses it: the Japanese romanizer's user dictionary, and nothing else. The
 * engine's own dictionary layer is elsewhere and does not touch this.
 *
 * `findClosest' works in a field of the list rather than a local, so two
 * lookups cannot run at once. That is the original's shape and is left alone.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "eci_key.h"

extern void *cpp_new(uint32_t n);
extern void  cpp_delete(void *p);

/* How many two-bit draws one call to rand supplies. */
#define SL_DRAWS 15

/* What save and load answer. Nought is well; -1 is something wrong with the
   list or with memory, and -2 is a file that would not open. */
#define SL_OK       0
#define SL_FAILED  (-1)
#define SL_NO_FILE (-2)

static void freeNode(SkipListNode *n)
{
    sln_dtor(n);
    cpp_delete(n);
}

SkipList *sl_ctor(SkipList *l)
{
    SkipListNode *head;

    l->count = 1;
    l->level = 0;
    srand((unsigned)time(0));
    l->bits = rand();
    l->bitsLeft = SL_DRAWS;
    l->closest = 0;
    l->cursor = 0;

    head = (SkipListNode *)cpp_new((uint32_t)sizeof *head);
    if (head != 0)
        sln_ctor(head, SL_MAX_LEVEL);
    l->head = head;

    /* A head whose tower could not be had is no head at all. */
    if (l->head != 0 && l->head->forward == 0) {
        freeNode(l->head);
        l->head = 0;
    }
    return l;
}

void sl_dtor(SkipList *l)
{
    if (l->head == 0)
        return;

    if (l->head->forward == 0) {
        freeNode(l->head);
        l->head = 0;
        return;
    }

    {
        SkipListNode *at = l->head;

        while (at != 0) {
            SkipListNode *next = at->forward[0];

            freeNode(at);
            at = next;
        }
    }
    l->head = 0;
}

/* How high the next tower goes: two bits at a time out of one rand, another
   level for each pair that comes up nought, stopping at the first pair that
   does not. One in four, and never more than the head can hold. */
static int32_t randomLevel(SkipList *l)
{
    int32_t level = 0;
    int32_t pair;

    do {
        pair = l->bits & 3;
        if (pair == 0)
            level++;
        l->bits >>= 2;
        l->bitsLeft--;
        if (l->bitsLeft == 0) {
            l->bits = rand();
            l->bitsLeft = SL_DRAWS;
        }
    } while (pair == 0);

    return level > SL_MAX_LEVEL ? SL_MAX_LEVEL : level;
}

/* The last node on one level whose key is still below the one wanted. `from'
   says where to start; nought means carry on from where the last call got to,
   which is how a search down the levels does not go back to the beginning each
   time. */
static SkipListNode *findClosest(SkipList *l, int32_t level, Key *key,
                                 SkipListNode *from, int32_t keyLen)
{
    for (;;) {
        SkipListNode *at;

        if (from != 0) {
            l->closest = from;
            from = 0;
        }
        at = l->closest->forward[level];
        if (at == 0)
            break;
        if (!key_lessthan(&at->base.key, key, keyLen))
            break;
        l->closest = at;
    }
    return l->closest;
}

/* Answers nought when the key was already there, whose translation is then
   replaced, and when it was put in; -1 when there was no room for a node. */
int32_t sl_insert(SkipList *l, Key *key, Translation *t)
{
    SkipListNode *update[SL_MAX_LEVEL + 1];
    SkipListNode *at;
    int32_t       level;
    int32_t       i;

    l->closest = l->head;
    for (level = l->level; level >= 0; level--)
        update[level] = findClosest(l, level, key, 0, 0);

    at = update[0]->forward[0];
    if (at != 0 && key_match(&at->base.key, key, 0)) {
        tr_assign(&at->base.trans, t);
        return 0;
    }

    level = randomLevel(l);
    if (level > l->level) {
        l->level++;
        level = l->level;
        update[level] = l->head;
    }

    at = (SkipListNode *)cpp_new((uint32_t)sizeof *at);
    if (at != 0)
        sln_ctor(at, level);
    if (at == 0)
        return -1;

    key_assign(&at->base.key, key);
    tr_assign(&at->base.trans, t);

    for (i = level; i >= 0; i--) {
        at->forward[i] = update[i]->forward[i];
        update[i]->forward[i] = at;
    }
    l->count++;
    return 0;
}

/* The translation for a key, or nought. The search starts at the top level and
   falls a level each time the key is not found there. */
Translation *sl_search(SkipList *l, Key *key)
{
    int32_t level = l->level;

    l->closest = l->head;
    for (; level >= 0; level--) {
        SkipListNode *at = findClosest(l, level, key, 0, 0);

        at = at->forward[level];
        if (at != 0 && key_match(&at->base.key, key, 0))
            return &at->base.trans;
    }
    return 0;
}

/* Every prefix of the key of even length, looked up in one pass.
 *
 * Japanese keys are two-byte characters, so the prefixes worth asking about
 * are two bytes, four bytes and so on; there are half as many of them as the
 * key is long. What comes back is an array of that many translations, with the
 * count in the four bytes before it, and an entry is left empty where its
 * prefix was not found.
 *
 * The position reached on each level is carried from one prefix to the next,
 * which is why this is one pass and not one search per prefix. */
Translation *sl_multiSearch(SkipList *l, Key *key)
{
    SkipListNode *from[SL_MAX_LEVEL + 1];
    int32_t       wanted = key->len / 2;
    char         *block;
    Translation  *out;
    int32_t       i;

    memset(from, 0, sizeof from);

    block = (char *)cpp_new((uint32_t)(wanted * (int32_t)sizeof(Translation)
                                       + 4));
    if (block == 0)
        return 0;
    *(int32_t *)block = wanted;
    out = (Translation *)(block + 4);
    for (i = 0; i < wanted; i++)
        tr_ctorEmpty(&out[i]);

    l->closest = l->head;
    for (i = 1; i <= wanted; i++) {
        int32_t level;

        for (level = l->level; level >= 0; level--) {
            SkipListNode *at;

            from[level] = findClosest(l, level, key, from[level], i * 2);
            at = from[level]->forward[level];
            if (at != 0 && key_match(&at->base.key, key, i * 2)) {
                tr_assign(&out[i - 1], &at->base.trans);
                break;
            }
        }
    }
    return out;
}

/* And give back what it answered.
 *
 * IBM calls Translation's vector deleting destructor on the array, which is
 * MSVC's own arrangement: the count sits in the four bytes in front of the
 * first element and the destructor walks back to find it. multiSearch above
 * lays the block out the same way, so this is the other half of that and the
 * only thing that may be handed the answer. */
void sl_freeMultiSearch(Translation *found)
{
    char    *block;
    int32_t  n;
    int32_t  i;

    if (found == 0)
        return;
    block = (char *)found - 4;
    n = *(int32_t *)block;
    for (i = 0; i < n; i++)
        tr_dtor(&found[i]);
    cpp_delete(block);
}

/* Answers -1 when the key was there and is now gone, nought when it was not.
   The list's level comes down again if the top row is left empty. */
int32_t sl_remove(SkipList *l, Key *key)
{
    SkipListNode *update[SL_MAX_LEVEL + 1];
    SkipListNode *at;
    int32_t       top = l->level;
    int32_t       level;

    l->closest = l->head;
    for (level = top; level >= 0; level--)
        update[level] = findClosest(l, level, key, 0, 0);

    at = update[0]->forward[0];
    if (at == 0 || !key_match(&at->base.key, key, 0))
        return 0;

    for (level = 0; level <= top; level++) {
        if (update[level]->forward[level] != at)
            break;
        update[level]->forward[level] = at->forward[level];
    }

    freeNode(at);
    l->count--;

    while (l->head->forward[top] == 0 && top > 0)
        top--;
    l->level = top;
    return -1;
}

int32_t sl_getFirst(SkipList *l, Key **key, Translation **t)
{
    l->cursor = l->head;
    return sl_getNext(l, key, t);
}

/* Answers -1 and fills both in while there is another entry, nought at the
   end. Both answers point into the list, not at copies. */
int32_t sl_getNext(SkipList *l, Key **key, Translation **t)
{
    if (l->cursor == 0)
        return 0;
    l->cursor = l->cursor->forward[0];
    if (l->cursor == 0)
        return 0;
    *key = &l->cursor->base.key;
    *t = &l->cursor->base.trans;
    return -1;
}

/* ---- to a file and back --------------------------------------------- */

/* How far along the bottom row a node is, counting the head as nought. This is
   what a forward pointer becomes in a file. */
static int32_t calcIndex(SkipList *l, SkipListNode *node)
{
    SkipListNode *at;
    int32_t       i = 0;

    if (node == 0)
        return 0;
    at = l->head;
    while (at != node && i < l->count) {
        i++;
        at = at->forward[0];
    }
    return i;
}

/* And back the other way. */
static SkipListNode *calcPointer(SkipList *l, int32_t index)
{
    SkipListNode *at;

    if (index == 0)
        return 0;
    at = l->head;
    while (index > 0) {
        at = at->forward[0];
        index--;
    }
    return at;
}

/* The count and the level, then every entry with its forward pointers turned
   into indices. An empty list -- one with nothing but a head -- is written as
   its two numbers and nothing else. */
int32_t sl_save(SkipList *l, const char *path)
{
    FILE          *f = fopen(path, "w+b");
    ArrayListNode *rows;
    char          *block;
    SkipListNode  *at;
    int32_t        i;
    int32_t        rc;

    if (f == 0)
        return SL_NO_FILE;

    fwrite(&l->count, 4, 1, f);
    fwrite(&l->level, 4, 1, f);
    if (l->count <= 1) {
        fclose(f);
        return SL_OK;
    }

    block = (char *)cpp_new((uint32_t)(l->count * (int32_t)sizeof *rows + 4));
    if (block == 0) {
        fclose(f);
        return SL_FAILED;
    }
    *(int32_t *)block = l->count;
    rows = (ArrayListNode *)(block + 4);
    for (i = 0; i < l->count; i++)
        aln_ctorEmpty(&rows[i]);

    at = l->head;
    for (i = 0; i < l->count; i++) {
        int32_t j;

        rows[i].count = at->levels;
        rows[i].forward = (int32_t *)cpp_new((uint32_t)(rows[i].count * 4));
        if (rows[i].forward == 0) {
            cpp_delete(block);
            fclose(f);
            return SL_FAILED;
        }

        key_assign(&rows[i].base.key, &at->base.key);
        tr_assign(&rows[i].base.trans, &at->base.trans);
        for (j = 0; j < rows[i].count; j++)
            rows[i].forward[j] = calcIndex(l, at->forward[j]);

        aln_save(&rows[i], f);
        at = at->forward[0];
    }

    rc = l->count;
    fclose(f);
    for (i = 0; i < l->count; i++)
        aln_dtor(&rows[i]);
    cpp_delete(block);
    return rc;
}

/* And back. The entries are read into an array first, then made into nodes and
   chained on the bottom row, and only then are the higher rows filled in --
   which they have to be, because an index cannot be turned into a pointer
   until every node it might name exists. */
int32_t sl_load(SkipList *l, const char *path)
{
    FILE          *f = fopen(path, "rb");
    ArrayListNode *rows;
    char          *block;
    SkipListNode  *at;
    SkipListNode  *last = 0;
    int32_t        i;
    int32_t        read = 0;

    if (f == 0)
        return SL_NO_FILE;

    fread(&l->count, 4, 1, f);
    if (l->count <= 1) {
        fclose(f);
        return SL_OK;
    }
    fread(&l->level, 4, 1, f);

    block = (char *)cpp_new((uint32_t)(l->count * (int32_t)sizeof *rows + 4));
    if (block == 0) {
        fclose(f);
        return SL_FAILED;
    }
    *(int32_t *)block = l->count;
    rows = (ArrayListNode *)(block + 4);
    for (i = 0; i < l->count; i++)
        aln_ctorEmpty(&rows[i]);

    for (read = 0; read < l->count; read++) {
        if (!aln_load(&rows[read], f))
            break;

        at = (SkipListNode *)cpp_new((uint32_t)sizeof *at);
        if (at != 0)
            sln_ctor(at, rows[read].count - 1);
        if (at == 0)
            break;
        if (at->forward == 0) {
            freeNode(at);
            break;
        }

        key_assign(&at->base.key, &rows[read].base.key);
        tr_assign(&at->base.trans, &rows[read].base.trans);

        if (last != 0) {
            last->forward[0] = at;
        } else {
            if (l->head != 0)
                freeNode(l->head);
            l->head = at;
        }
        last = at;
    }
    fclose(f);

    if (read < l->count) {
        /* Something ran out part way through: let go of what was made and
           say so. */
        at = last;
        while (at != 0) {
            SkipListNode *next = at->forward[0];

            freeNode(at);
            at = next;
        }
        for (i = 0; i < l->count; i++)
            aln_dtor(&rows[i]);
        cpp_delete(block);
        return SL_FAILED;
    }

    for (i = 0; i < last->levels; i++)
        last->forward[i] = 0;

    at = l->head;
    for (i = 0; i < l->count; i++) {
        int32_t j;

        for (j = 0; j < at->levels; j++)
            at->forward[j] = calcPointer(l, rows[i].forward[j]);
        at = at->forward[0];
    }

    read = l->count;
    for (i = 0; i < l->count; i++)
        aln_dtor(&rows[i]);
    cpp_delete(block);
    return read;
}
