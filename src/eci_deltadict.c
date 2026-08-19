/* Looking a span of the spine up in the language's own tables.
 *
 * Two questions get asked of a language: is this run of characters one of my
 * sets, and what does my dictionary say about it. Both are answered the same
 * way -- pull the characters out of the spine into a flat buffer, then search
 * a sorted block for them.
 *
 * The block is one run of nul-terminated strings laid end to end, in order.
 * A set is searched by halving the block itself and then backing up to the
 * start of whichever entry the middle landed inside, which is why the search
 * has to remember where it last looked: without that it would sit on the same
 * entry for ever. A dictionary has an index of offsets in front of it, so
 * halving the index needs no backing up, and what comes back is a pointer
 * just past the key, which is where the value starts.
 *
 * The comparison is its own function because it is not strcmp: running out of
 * wanted string counts as a match rather than as less-than, so a key that is
 * a prefix of an entry finds it.
 */

#include <stdint.h>
#include <string.h>
#include "delta.h"

/* What a set or an action entry keeps. The record is the language's own data
   and is the shape the language wrote it, so the store it names is four bytes
   whatever the host is. */
#define E_STORE(e)   EVV_AT(const uint8_t *, \
                            *(const evv_ref *)((const char *)(e) + 0x04))
#define E_STMT(e)    (*(const uint8_t *)((const char *)(e) + 0x08))
#define E_ACTN(e)    (*(const int32_t *)((const char *)(e) + 0x0c))
#define E_SETN(e)    (*(const int32_t *)((const char *)(e) + 0x10))
#define E_WIDTH(e)   (*(const uint8_t *)((const char *)(e) + 0x18))

/* How much of a run this carries, and how much it says it will. */
#define BUF_BYTES 0x108
#define BUF_ROOM  0xfe

#define BEFORE (-1)
#define SAME   0
#define AFTER  1

/* Wanted against an entry. The wanted string running out counts as a match,
   so a prefix finds the entry it is a prefix of. */
static int32_t scomp(const uint8_t *want, const uint8_t *at)
{
    for (;;) {
        if (*at == 0)
            return *want != 0;
        if (*want == 0 || *want < *at)
            return BEFORE;
        if (*want > *at)
            return AFTER;
        want++;
        at++;
    }
}

/* Pull the run between two nodes out flat, one field's worth at a time.
   Stops at the end of the spine, at the far node, or when the buffer holds
   as much as it was said to. */
static int32_t extract(delta_state *d, int32_t l, int32_t r, uint8_t field,
                       uint8_t *out, int32_t room, int32_t width)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);
    int32_t     f = v->fence_base + field;
    void       *(*get)(void *);
    int32_t     most = room / width;
    int32_t     n = 0;

    /* Both ends have to be fenced on this field or there is no run. */
    if (!(((int32_t *)(intptr_t)l)[f] & 1)
     || !(((int32_t *)(intptr_t)r)[f] & 1))
        return 0;

    get = (void *(*)(void *))vstmtbl[field].get[0];

    while (l != EVV_AT(delta_stack *, d->stack)->spine_r && l != r && n < most) {
        int32_t at = ((int32_t *)(intptr_t)l)[f] & ~3;

        if (at != 0 && (*(int32_t *)(intptr_t)at & 2)) {
            /* Nothing of its own here; step over it. */
            l = at;
        } else {
            memcpy(out, get(TFLDS((void *)(intptr_t)at)), (size_t)width);
            out += width;
            l = *(int32_t *)(intptr_t)(at + 4) & ~3;
        }
        n++;
    }

    *out = 0;
    return 1;
}

/* Halve a block of sorted strings. The middle lands wherever it lands, so
   back up to the start of that entry first; the same entry coming round
   twice is what ends the search. */
static int32_t bs(const uint8_t *store, int32_t count, const uint8_t *want)
{
    int32_t lo = 0;
    int32_t hi = count - 1;
    int32_t found = 0;
    int32_t last = -1;

    for (;;) {
        int32_t mid = (hi + lo) >> 1;
        int32_t r;

        if (hi < lo)
            break;

        while (mid >= 0 && store[mid] != 0)
            mid--;
        mid++;

        if (mid == last)
            break;
        last = mid;

        r = scomp(want, store + mid);
        if (r == BEFORE) {
            hi = mid - 1;
        } else if (r == SAME) {
            found = 1;
        } else if (r == AFTER) {
            while (store[mid] != 0)
                mid++;
            lo = mid + 1;
        }

        if (found)
            break;
    }

    return found;
}

/* The same over an index of offsets, which needs no backing up. What comes
   back is where the entry's value starts, just past its key. */
int32_t bs2(const int16_t *index, const uint8_t *store, int32_t count,
            const uint8_t *want, const uint8_t **out)
{
    int32_t lo = 0;
    int32_t hi = count - 1;
    int32_t found = 0;

    for (;;) {
        int16_t off;
        int32_t mid;
        int32_t r;

        if (hi < lo)
            break;

        mid = (hi + lo) >> 1;
        memcpy(&off, (const char *)index + mid * 2, 2);

        r = scomp(want, store + off);
        if (r == BEFORE) {
            hi = mid - 1;
        } else if (r == SAME) {
            *out = store + off;
            do {
                (*out)++;
            } while (**out != 0);
            (*out)++;
            found = 1;
        } else if (r == AFTER) {
            lo = mid + 1;
        }

        if (found)
            break;
    }

    return found;
}

/* Is the run between the two registers one of this set's entries? Only a
   field that compares as the narrowest kind is searched this way. */
int setdlookup(delta_state *d, int32_t from, int32_t to, void *set,
               int32_t arg)
{
    uint8_t buf[BUF_BYTES];

    if (vstmtbl[E_STMT(set)].fields->kind != -1)
        return 0;

    if (!extract(d, from, to, (uint8_t)arg, buf, BUF_ROOM, E_WIDTH(set)))
        return 0;

    return bs(E_STORE(set), E_WIDTH(set) * E_SETN(set), buf);
}

/* And what does the dictionary say about it? Nought for not found. */
const uint8_t *actdlookup(delta_state *d, int32_t l, int32_t r,
                          const void *entry)
{
    uint8_t        buf[BUF_BYTES];
    const uint8_t *value = 0;

    if (vstmtbl[E_STMT(entry)].fields->kind != -1)
        return 0;

    if (!extract(d, l, r, E_STMT(entry), buf, BUF_ROOM, E_WIDTH(entry)))
        return 0;

    if (!bs2((const int16_t *)E_STORE(entry),
             E_STORE(entry) + E_ACTN(entry) * 2,
             E_ACTN(entry), buf, &value))
        return 0;

    return value;
}
