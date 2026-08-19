/* A hash table, keyed either by string or by number.
 *
 * Buckets of singly linked entries, each entry holding its key, the next
 * entry, and whatever the caller wanted stored. Nothing ever grows: the
 * number of buckets is fixed when the table is made, and a table asked for
 * nothing takes two hundred and eleven, which is prime.
 *
 * Prime is the point. The caller can ask for the size it wants rounded up to
 * a prime, and then the modulo that turns a hash into a bucket spreads
 * evenly whatever the hash looks like. The primality test tries every number
 * below the candidate, which is only sane because this happens once.
 *
 * The string hash is the old ELF one: shift left four, add the character,
 * and when the top nibble fills, fold it back down. The number hash folds a
 * word in half, then folds four bits of what is left back over itself.
 *
 * Freeing is the caller's business twice over. A table remembers whether it
 * owns its keys, and every call that removes something is told separately
 * whether to free the key and whether to free the value, because the same
 * table is used both for things it owns and things it only points at.
 *
 * Two kinds of key means two of nearly everything. They are written out
 * separately rather than shared, which is what the original does, because
 * the comparison is the only difference and hiding it behind a function
 * pointer would cost more than it saved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "evv_arena.h"

/* What a table with nothing asked of it takes. */
#define DEFAULT_BUCKETS 0xd3

/* Above this, rounding up would overflow, so it stops. */
#define TOO_BIG 0x7ffffffe

typedef struct HashEntry {
    void             *key;   /* +0x00, a string or a number */
    struct HashEntry *next;  /* +0x04 */
    void             *value; /* +0x08 */
} HashEntry;

typedef struct Hash {
    int32_t     buckets;     /* +0x00 */
    int32_t     ownsKeys;    /* +0x04 */
    HashEntry **at;          /* +0x08 */
    int32_t     unused_0c;
} Hash;

typedef struct HashIter {
    Hash      *hash;  /* +0x00 */
    int32_t    bucket;/* +0x04 */
    HashEntry *entry; /* +0x08 */
} HashIter;

/* Every number below it, which is slow and does not matter: it happens once
   per table and only while one is being made. */
static int32_t isprime(int32_t n)
{
    int32_t i;

    for (i = 2; i < n; i++) {
        if (n % i == 0)
            return 0;
    }
    return 1;
}

/* The ELF hash. When the top nibble fills up it is folded back into the
   bottom, so no character stops mattering however long the string is. */
static uint32_t stringHashFunction(const Hash *h, const char *s)
{
    uint32_t hashval = 0;

    while (*s) {
        uint32_t g;

        hashval = (hashval << 4) + (uint32_t)(int32_t)(signed char)*s;
        g = hashval & 0xf0000000u;
        if (g) {
            hashval ^= g >> 24;
            hashval ^= g;
        }
        s++;
    }

    return hashval % (uint32_t)h->buckets;
}

/* Fold the word in half, then fold four bits of the middle back over it. */
static uint32_t intHashFunction(const Hash *h, uint32_t k)
{
    k = ((k & 0xffff0000u) >> 16) ^ (k & 0xffffu);
    k = ((k & 0x3c00u) >> 10) ^ k;
    return k % (uint32_t)h->buckets;
}

/* Room for the buckets and nothing in them. A size of nought or less takes
   the default; anything else is rounded up to an odd number and then to a
   prime, if asked. */
void *hashNew(int32_t want, int32_t ownsKeys, int32_t wantPrime)
{
    Hash *h = (Hash *)malloc(sizeof(Hash));

    if (!h)
        return 0;

    if (want > 0) {
        if (wantPrime) {
            if (want % 2 == 0 && want < TOO_BIG)
                want++;
            while (want < TOO_BIG && !isprime(want))
                want += 2;
        }
        h->buckets = want;
    } else {
        h->buckets = DEFAULT_BUCKETS;
    }

    h->ownsKeys = ownsKeys;
    h->at = (HashEntry **)calloc((size_t)h->buckets, sizeof(HashEntry *));
    if (!h->at) {
        free(h);
        return 0;
    }

    return h;
}

/* The whole table. Keys go back only if the table owned them and the caller
   asks; values only if the caller asks and there is one. */
int32_t hashDelete(void *table, int32_t freeKeys, int32_t freeValues)
{
    Hash   *h = (Hash *)table;
    int32_t i;

    if (!h)
        return 0;

    if (h->at) {
        for (i = 0; i < h->buckets; i++) {
            HashEntry *e = h->at[i];

            while (e) {
                HashEntry *next = e->next;

                if (freeKeys && h->ownsKeys)
                    free(e->key);
                if (freeValues && e->value)
                    free(e->value);
                free(e);
                e = next;
            }
        }
        free(h->at);
    }

    free(h);
    return 0;
}

/* ---- keyed by string ------------------------------------------------ */

/* Straight onto the front of its bucket, without looking to see whether the
   key is already there. Two entries with the same key are allowed; the
   later one is found first. */
int32_t hashInsertString(void *table, char *key, void *value)
{
    Hash      *h = (Hash *)table;
    uint32_t   i = stringHashFunction(h, key);
    HashEntry *was = h->at[i];
    HashEntry *e = (HashEntry *)malloc(sizeof(HashEntry));

    if (!e)
        return 0;

    e->key = key;
    e->value = value;
    e->next = was;
    h->at[i] = e;
    return 1;
}

void *hashLookupString(void *table, const char *key)
{
    Hash      *h = (Hash *)table;
    HashEntry *e = h->at[stringHashFunction(h, key)];

    while (e) {
        if (strcmp((const char *)e->key, key) == 0)
            return e->value;
        e = e->next;
    }
    return 0;
}

int32_t hashDeleteString(void *table, const char *key, int32_t freeKey,
                         int32_t freeValue)
{
    Hash      *h = (Hash *)table;
    uint32_t   i = stringHashFunction(h, key);
    HashEntry *prev = h->at[i];
    HashEntry *e;

    /* The first in the bucket is unhooked from the bucket itself. */
    if (strcmp((const char *)prev->key, key) == 0) {
        h->at[i] = prev->next;
        if (freeKey)
            free(prev->key);
        if (freeValue && prev->value)
            free(prev->value);
        free(prev);
        return 1;
    }

    for (e = prev->next; e; e = prev->next) {
        if (strcmp((const char *)e->key, key) == 0)
            break;
        prev = e;
    }
    if (!e)
        return 0;

    prev->next = e->next;
    if (freeKey)
        free(e->key);
    if (freeValue && e->value)
        free(e->value);
    free(e);
    return 1;
}

/* Give an entry a new key, and move it to the bucket the new key belongs in
   if that is a different one. Answers what it was storing. */
void *hashMoveString(void *table, const char *oldKey, char *newKey)
{
    Hash      *h = (Hash *)table;
    HashEntry *prev = 0;
    uint32_t   from = stringHashFunction(h, oldKey);
    uint32_t   to;
    HashEntry *e = h->at[from];

    while (e && strcmp((const char *)e->key, oldKey) != 0) {
        prev = e;
        e = e->next;
    }
    if (!e)
        return 0;

    to = stringHashFunction(h, newKey);
    e->key = newKey;

    if (to != from) {
        if (prev)
            prev->next = e->next;
        else
            h->at[from] = e->next;
        e->next = h->at[to];
        h->at[to] = e;
    }

    return e->value;
}

/* ---- keyed by number ------------------------------------------------ */

int32_t hashInsertInt(void *table, int32_t key, void *value)
{
    Hash      *h = (Hash *)table;
    uint32_t   i = intHashFunction(h, (uint32_t)key);
    HashEntry *was = h->at[i];
    HashEntry *e = (HashEntry *)malloc(sizeof(HashEntry));

    if (!e)
        return 0;

    e->key = (void *)(intptr_t)key;
    e->value = value;
    e->next = was;
    h->at[i] = e;
    return 1;
}

void *hashLookupInt(void *table, int32_t key)
{
    Hash      *h = (Hash *)table;
    HashEntry *e = h->at[intHashFunction(h, (uint32_t)key)];

    while (e) {
        if (EVV_REF(e->key) == key)
            return e->value;
        e = e->next;
    }
    return 0;
}

/* No key to free: a number was never allocated. */
int32_t hashDeleteInt(void *table, int32_t key, int32_t freeValue)
{
    Hash      *h = (Hash *)table;
    uint32_t   i = intHashFunction(h, (uint32_t)key);
    HashEntry *prev = h->at[i];
    HashEntry *e;

    if (EVV_REF(prev->key) == key) {
        h->at[i] = prev->next;
        if (freeValue && prev->value)
            free(prev->value);
        free(prev);
        return 1;
    }

    for (e = prev->next; e; e = prev->next) {
        if (EVV_REF(e->key) == key)
            break;
        prev = e;
    }
    if (!e)
        return 0;

    prev->next = e->next;
    if (freeValue && e->value)
        free(e->value);
    free(e);
    return 1;
}

void *hashMoveInt(void *table, int32_t oldKey, int32_t newKey)
{
    Hash      *h = (Hash *)table;
    HashEntry *prev = 0;
    uint32_t   from = intHashFunction(h, (uint32_t)oldKey);
    uint32_t   to;
    HashEntry *e = h->at[from];

    while (e && EVV_REF(e->key) != oldKey) {
        prev = e;
        e = e->next;
    }
    if (!e)
        return 0;

    to = intHashFunction(h, (uint32_t)newKey);
    e->key = (void *)(intptr_t)newKey;

    if (to != from) {
        if (prev)
            prev->next = e->next;
        else
            h->at[from] = e->next;
        e->next = h->at[to];
        h->at[to] = e;
    }

    return e->value;
}

/* ---- walking the whole table ---------------------------------------- */

/* Step to the next entry, or to the first entry of the next bucket that has
   one. Answers whether there is anything to look at. Running off the end
   puts the walk back at the start, which is the original's doing. */
int32_t hashIterNext(void *iter)
{
    HashIter *it = (HashIter *)iter;

    if (it->entry)
        it->entry = it->entry->next;

    while (!it->entry) {
        it->bucket++;
        if (it->bucket >= it->hash->buckets) {
            it->bucket = 0;
            it->entry = 0;
            break;
        }
        it->entry = it->hash->at[it->bucket];
    }

    return it->entry != 0;
}

/* Start at the first bucket, and step on if it is empty. */
int32_t hashIterConstruct(void *iter, void *table)
{
    HashIter *it = (HashIter *)iter;

    it->hash = (Hash *)table;
    it->bucket = 0;
    it->entry = it->hash->at[it->bucket];

    if (!it->entry)
        return hashIterNext(it);
    return 1;
}

const char *hashIterString(void *iter)
{
    return (const char *)((HashIter *)iter)->entry->key;
}

int32_t hashIterInt(void *iter)
{
    return EVV_REF(((HashIter *)iter)->entry->key);
}

void *hashIterRef(void *iter)
{
    return ((HashIter *)iter)->entry->value;
}
