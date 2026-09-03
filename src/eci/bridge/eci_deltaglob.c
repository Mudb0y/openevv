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
#include "delta_rules_c.h"

extern int32_t runtime_new(delta_state *d);
extern void    runtime_delete(delta_state *d);

#define ALIGN_UP(n, a) (((n) + (a) - 1) & ~((a) - 1))

/* Whichever language the caller set before asking for one. The engine
   array is what sets it: it keeps an engine per language and knows which it
   is about to make. A machine remembers the language it was made for, so
   nothing after this has to be told again. */
delta_state *delta_new(void)
{
    const delta_language *lang = delta_lang_now();
    delta_state *d;
    int32_t      at, i;
    int32_t      nword = 0, nlong = 0, nshort = 0, ncompound = 0;
    int32_t      w = 0, l = 0, s = 0, c = 0;

    /* The constants go where a value can name them before anything
       can name one. */
    delta_syms_bind();

    d = delta_lang_alloc(lang);
    if (d == 0)
        return 0;

    /* Cleared before anything can fail, because the failure path below
       hands the whole thing to delta_delete and that frees all four. The
       original left them holding whatever malloc did. */
    d->word = EVV_REF(0);
    d->compound = EVV_REF(0);
    d->lng = EVV_REF(0);
    d->shrt = EVV_REF(0);

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

    d->word = EVV_REF(malloc((size_t)d->nword * sizeof *EVV_AT(int32_t **, d->word)));
    if (EVV_AT(int32_t **, d->word) == 0) {
        delta_delete(d);
        return 0;
    }
    d->compound = EVV_REF(malloc((size_t)d->ncompound * sizeof *EVV_AT(delta_compound *, d->compound)));
    if (EVV_AT(delta_compound *, d->compound) == 0) {
        delta_delete(d);
        return 0;
    }
    d->lng = EVV_REF(malloc((size_t)d->nlong * sizeof *EVV_AT(int32_t **, d->lng)));
    if (EVV_AT(int32_t **, d->lng) == 0) {
        delta_delete(d);
        return 0;
    }
    d->shrt = EVV_REF(malloc((size_t)d->nshort * sizeof *EVV_AT(int16_t **, d->shrt)));
    if (EVV_AT(int16_t **, d->shrt) == 0) {
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
            EVV_AT(int32_t **, d->word)[w] = EVV_AT(int32_t **, d->word)[w + nword] = (int32_t *)(cell + 4);
            w++;
            at += 8;
            break;

        case DG_LONG:
            at   = ALIGN_UP(at, 4);
            cell = (unsigned char *)d + at;
            *(int16_t *)cell        = DG_LONG;
            *(int32_t *)(cell + 4)  = 0;
            EVV_AT(int32_t **, d->lng)[l] = EVV_AT(int32_t **, d->lng)[l + nlong] = (int32_t *)(cell + 4);
            l++;
            at += 8;
            break;

        case DG_SHORT:
            at   = ALIGN_UP(at, 2);
            cell = (unsigned char *)d + at;
            *(int16_t *)cell        = DG_SHORT;
            *(int16_t *)(cell + 2)  = 0;
            EVV_AT(int16_t **, d->shrt)[s] = EVV_AT(int16_t **, d->shrt)[s + nshort] = (int16_t *)(cell + 2);
            s++;
            at += 4;
            break;

        case DG_COMPOUND:
            /* A compound whose first word is 6 holds four-byte items and goes
               on a four-byte boundary; the rest want two. The lifter's model
               of this area in tools/gen-globals.py has the same rule and
               checks it against the object cell by cell, so the two have to
               agree or every offset from the first such compound onwards is
               two bytes out -- which is what four languages did, reading a
               variable's kind out of the middle of the cell before it. */
            at   = ALIGN_UP(at, delta_compounds[c].init == 6 ? 4 : 2);
            cell = (unsigned char *)d + at;
            EVV_AT(delta_compound *, d->compound)[c].at    = cell;
            EVV_AT(delta_compound *, d->compound)[c].init  = delta_compounds[c].init;
            EVV_AT(delta_compound *, d->compound)[c].bytes = delta_compounds[c].bytes;
            EVV_AT(delta_compound *, d->compound)[c + ncompound] = EVV_AT(delta_compound *, d->compound)[c];
            at += 4 + ALIGN_UP(delta_compounds[c].bytes, 2);
            c++;
            break;
        }
    }

    /* The second and third word variable, which something wants a direct
       handle on. The handle is on the cell, tag and all, not the value. */
    d->direct_a = EVV_REF((int16_t *)((char *)EVV_AT(int32_t **, d->word)[1] - 4));
    d->direct_b = EVV_REF((int16_t *)((char *)EVV_AT(int32_t **, d->word)[2] - 4));

    lang->link_new(d);
    lang->set_dict_new(d);
    lang->act_dict_new(d);
    if (runtime_new(d)) {
        /* What the original does, and it drops the four indexes on the
           floor rather than going through delta_delete. */
        delta_lang_free(d);
        return 0;
    }

    return d;
}

void delta_delete(delta_state *d)
{
    const delta_language *lang;
    const delta_language *was;

    if (d == 0)
        return;

    /* Taking a machine down reads the language's tables, and this is called
       from wherever the last reference went, which need not be a thread
       speaking that language. */
    lang = delta_lang_of(d);
    was  = delta_lang_set(lang);

    lang->link_delete(d);
    lang->set_dict_delete(d);
    lang->act_dict_delete(d);
    runtime_delete(d);

    if (EVV_AT(int32_t **, d->word) != 0) {
        free(EVV_AT(int32_t **, d->word));
        d->word = EVV_REF(0);
    }
    if (EVV_AT(delta_compound *, d->compound) != 0) {
        free(EVV_AT(delta_compound *, d->compound));
        d->compound = EVV_REF(0);
    }
    if (EVV_AT(int16_t **, d->shrt) != 0) {
        free(EVV_AT(int16_t **, d->shrt));
        d->shrt = EVV_REF(0);
    }
    if (EVV_AT(int32_t **, d->lng) != 0) {
        free(EVV_AT(int32_t **, d->lng));
        d->lng = EVV_REF(0);
    }

    delta_lang_free(d);
    delta_lang_set(was);
}
