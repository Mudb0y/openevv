/* Making a machine and taking one apart.
 *
 * Almost all of this is the global variables. The language declares a list
 * of them and they live in the tail of delta_state, each one a cell with
 * its type tag in front of its value, so that a pointer to a value always
 * has the tag four or two bytes behind it. delta_new walks the list once,
 * lays the cells out, and builds one index per kind.
 *
 * Two things about those indexes are worth saying out loud. Every one of
 * them holds its list twice, back to back, and the count the machine
 * records is of the doubled list; the original does this and nothing found
 * so far explains why. And nothing outside this file knows what offset a
 * variable landed at -- every reader goes through an index -- so the layout
 * is ours to decide. It is kept exactly as the original had it anyway.
 *
 * In the original all of this was written out flat, one instruction per
 * store, six and a half thousand of them, because the compiler unrolled
 * the loops. The list they were laying out is in delta_globals_enus.c.
 */

#include <stdint.h>
#include <stdlib.h>
#include "delta.h"

extern int32_t runtime_new(delta_state *d);
extern void    runtime_delete(delta_state *d);

#define ALIGN_UP(n, a) (((n) + (a) - 1) & ~((a) - 1))

delta_state *delta_new(void)
{
    delta_state *d;
    int32_t      at, i;
    int32_t      nword = 0, nlong = 0, nshort = 0, ncompound = 0;
    int32_t      w = 0, l = 0, s = 0, c = 0;

    d = malloc(DELTA_STATE_BYTES);
    if (d == 0)
        return 0;

    /* Cleared before anything can fail, because the failure path below
       hands the whole thing to delta_delete and that frees all four. The
       original left them holding whatever malloc did. */
    d->word     = 0;
    d->compound = 0;
    d->lng      = 0;
    d->shrt     = 0;

    for (i = 0; i < delta_globals_n; i++)
        switch (delta_globals[i]) {
        case DG_WORD:     nword++;     break;
        case DG_LONG:     nlong++;     break;
        case DG_SHORT:    nshort++;    break;
        case DG_COMPOUND: ncompound++; break;
        }

    d->nword     = nword * 2;
    d->nlong     = nlong * 2;
    d->nshort    = nshort * 2;
    d->ncompound = ncompound * 2;

    d->word = malloc((size_t)d->nword * sizeof *d->word);
    if (d->word == 0) {
        delta_delete(d);
        return 0;
    }
    d->compound = malloc((size_t)d->ncompound * sizeof *d->compound);
    if (d->compound == 0) {
        delta_delete(d);
        return 0;
    }
    d->lng = malloc((size_t)d->nlong * sizeof *d->lng);
    if (d->lng == 0) {
        delta_delete(d);
        return 0;
    }
    d->shrt = malloc((size_t)d->nshort * sizeof *d->shrt);
    if (d->shrt == 0) {
        delta_delete(d);
        return 0;
    }

    /* Lay the cells out in declaration order and index each one twice. A
       compound's body is not touched here; initGlobalVars fills it from
       the description that goes into the index. */
    at = DG_BASE;
    for (i = 0; i < delta_globals_n; i++) {
        unsigned char *cell;

        switch (delta_globals[i]) {
        case DG_WORD:
            at   = ALIGN_UP(at, 4);
            cell = (unsigned char *)d + at;
            *(int16_t *)cell        = DG_WORD;
            *(int32_t *)(cell + 4)  = 0;
            d->word[w] = d->word[w + nword] = (int32_t *)(cell + 4);
            w++;
            at += 8;
            break;

        case DG_LONG:
            at   = ALIGN_UP(at, 4);
            cell = (unsigned char *)d + at;
            *(int16_t *)cell        = DG_LONG;
            *(int32_t *)(cell + 4)  = 0;
            d->lng[l] = d->lng[l + nlong] = (int32_t *)(cell + 4);
            l++;
            at += 8;
            break;

        case DG_SHORT:
            at   = ALIGN_UP(at, 2);
            cell = (unsigned char *)d + at;
            *(int16_t *)cell        = DG_SHORT;
            *(int16_t *)(cell + 2)  = 0;
            d->shrt[s] = d->shrt[s + nshort] = (int16_t *)(cell + 2);
            s++;
            at += 4;
            break;

        case DG_COMPOUND:
            at   = ALIGN_UP(at, 2);
            cell = (unsigned char *)d + at;
            d->compound[c].at    = cell;
            d->compound[c].init  = delta_compounds[c].init;
            d->compound[c].bytes = delta_compounds[c].bytes;
            d->compound[c + ncompound] = d->compound[c];
            at += 4 + ALIGN_UP(delta_compounds[c].bytes, 2);
            c++;
            break;
        }
    }

    /* The second and third word variable, which something wants a direct
       handle on. The handle is on the cell, tag and all, not the value. */
    d->direct_a = (int16_t *)((char *)d->word[1] - 4);
    d->direct_b = (int16_t *)((char *)d->word[2] - 4);

    link_new(d);
    set_dict_new(d);
    act_dict_new(d);
    if (runtime_new(d)) {
        /* What the original does, and it drops the four indexes on the
           floor rather than going through delta_delete. */
        free(d);
        return 0;
    }

    return d;
}

void delta_delete(delta_state *d)
{
    if (d == 0)
        return;

    link_delete(d);
    set_dict_delete(d);
    act_dict_delete(d);
    runtime_delete(d);

    if (d->word != 0) {
        free(d->word);
        d->word = 0;
    }
    if (d->compound != 0) {
        free(d->compound);
        d->compound = 0;
    }
    if (d->shrt != 0) {
        free(d->shrt);
        d->shrt = 0;
    }
    if (d->lng != 0) {
        free(d->lng);
        d->lng = 0;
    }

    free(d);
}
