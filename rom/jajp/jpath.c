/* The path search: every way a sentence's readings can be strung together.
 *
 * DictSearch leaves behind a set of candidate entries -- each one a word the
 * text could be read as, with where it starts and how many characters it
 * covers. This is what joins them up: it starts a path at each entry that may
 * begin a phrase, extends every path with each entry that starts where the
 * path leaves off, and keeps the cheap ones. What comes out is the paths, a
 * copy of each entry that appears on one, and an index from an entry to that
 * copy, which is what PhraseBuf reads.
 *
 * What makes it a search rather than an enumeration is one byte a character
 * holding the least any path has cost to reach it. A path that reaches a
 * character having cost more than that is thrown away instead of grown, so
 * the number of paths stays in the hundreds rather than the millions.
 *
 * The cost itself is the interesting part, and it comes from two tables IBM
 * shipped in the phrase data rather than from anything computed here. Each
 * entry's part of speech indexes a four-byte "type group" that says what kind
 * of word it is, and JrtJrtCheck takes the type groups of the two words being
 * joined, picks one of fourteen thirty-two byte tables by the left word's
 * bits, and reads the cost out of it by the right word's. CheckAdFlag then
 * adjusts that by the two words' own attribute bytes -- a two-dimensional
 * decision on the two words' classes, which is the bulk of this file. A
 * negative answer from either means the two words may not be adjacent at all,
 * and the path is refused.
 *
 * The record is IBM's and rom/jajp/jpath.h is the map, checked against the
 * object by `tools/rom/offsets.py jpath'.
 *
 * Held to IBM's answer by test/harness/romprims.sh.
 */

#include <stdint.h>
#include "jprom.h"
#include "jpath.h"
#include "dictsearch.h"
#include "txtanal.h"

/* The three pointers, which are parked past the record. */
#define JP_VTABLE_OF(jp) (*(void **)JP_P((jp), JP_VTABLE_AT))
#define JP_OWNER_OF(jp)  (*(void **)JP_P((jp), JP_OWNER_AT))
#define JP_SEARCH_OF(jp) (*(void **)JP_P((jp), JP_SEARCH_AT))
#define TA_AT(ta, which) (*(void **)((uint8_t *)(ta) + (which)))
#define DS_W(d, off)     (*(int16_t *)((uint8_t *)(d) + (off)))

/* One candidate entry of DictSearch, which is thirty-two bytes at IBM's own
   offsets whether it is read from there or from a path. */
#define DS_ENTRY_AT(d, i) ((uint8_t *)(d) + DS_ENTRY + (i) * DS_ENTRY_SIZE)
#define DE_B(e, off) (*((const uint8_t *)(e) + (off)))
#define DE_W(e, off) (*(const int16_t *)((const uint8_t *)(e) + (off)))
#define DE_L(e, off) (*(const int32_t *)((const uint8_t *)(e) + (off)))

/* Where the connection costs sit in the phrase data.
 *
 * Fourteen tables of thirty-two signed bytes, one per class of left-hand
 * word, each read by the right-hand word's type-group bit: byte i of the type
 * group and bit 0x80 >> j together index i * 8 + j. The last two are four
 * bytes each rather than thirty-two, and are the masks IsEnd and
 * IsContinuable test a type group against.
 *
 * The block at PD_KANJI is the one exception to the shape: only its first two
 * rows are swept, the top nibble of the third type-group byte is read out of
 * PD_KANJI_TG2 instead, and a right-hand word that is the sentence boundary
 * or is marked takes one of the two single bytes after it. */
#define PD_TABLE        0x000
#define PD_TABLE_2      0x020
#define PD_TABLE_3      0x040
#define PD_JOSHI        0x060
#define PD_KANJI        0x080
#define PD_KANJI_TG2    0x090
#define PD_KANJI_END    0x098
#define PD_KANJI_MARK   0x09b
#define PD_SUFFIX       0x0a0
#define PD_PREFIX       0x0c0
#define PD_NUMBER       0x0e0
#define PD_TAIL         0x100
#define PD_HEAD         0x120
#define PD_VERB         0x140
#define PD_ADJ          0x160
#define PD_NOUN         0x180
#define PD_FZK          0x1a0
#define PD_END_MASK     0x1c0
#define PD_CONT_MASK    0x1c4

/* How dear a path may get, and how many moras it may run to, before AddPath
   refuses to grow it further. */
#define JP_COST_MOST    0x32
#define JP_MORAS_MOST   0x19

/* The type-group column IsHead tests, and the bit in it. */
#define TG_NOT_HEAD     0x01

/* ---- being made and unmade ------------------------------------------ */

/* IBM's constructor writes a vtable pointer at nought and the class has one
   member in it, the destructor. Nothing here dispatches through it, so nought
   stands in that slot and jp_destroy below is reached by name. */
void *jp_ctor(void *jp, void *analysis)
{
    JP_VTABLE_OF(jp) = 0;
    JP_OWNER_OF(jp)  = analysis;
    JP_SEARCH_OF(jp) = TA_AT(analysis, TA_DICTSEARCH_AT);
    return jp;
}

/* The scalar deleting destructor, which is the shape MSVC gives a virtual
   destructor: the flag says whether to give the storage back as well. It owns
   nothing else, so there is nothing to unmake first. */
void *jp_destroy(void *jp, int32_t freeIt)
{
    JP_VTABLE_OF(jp) = 0;
    if (freeIt & 1)
        cpp_delete(jp);
    return jp;
}

/* ---- what kind of word this is -------------------------------------- */

/* The eleven classes a type group falls into, which is what both cost
   functions switch on. Nought is the sentence boundary: `Make' seeds a path
   against a type group of all noughts but the top bit of its last byte, and
   that is the value this returns for it. */
int16_t jp_CheckType(void *jp, const uint8_t *tg)
{
    (void)jp;

    if (tg[3] & 0x01) {
        if (tg[2] & 0x03)
            return (int16_t)((tg[2] & 0x01) ? 6 : 5);
        return 4;
    }
    if (tg[2] & 0x03) {
        if (tg[3] & 0x10)
            return 9;
        return (int16_t)((tg[2] & 0x01) ? 3 : 2);
    }
    if (tg[3] & 0x80)
        return 0;
    if (tg[2] & 0x10)
        return 7;
    if (tg[3] & 0x08)
        return 8;
    if (tg[3] & 0x10)
        return 10;
    return 1;
}

/* Whether an entry may begin a phrase, and whether it may end or continue
   one. The two masks the last two test against are IBM's, out of the phrase
   data rather than out of anything here. */
int32_t jp_IsHead(void *jp, const uint8_t *e)
{
    (void)jp;
    return (dm_GetTGAt2(DE_B(e, DE_POS), 2) & TG_NOT_HEAD) ? 0 : 1;
}

int32_t jp_IsContinuable(void *jp, const uint8_t *e)
{
    int8_t i;

    (void)jp;
    for (i = 0; i < 4; i++) {
        const uint8_t *pd = dm_GetPhraseDataPtr();

        if (dm_GetTGAt2(DE_B(e, DE_POS), (uint8_t)i) & pd[PD_CONT_MASK + i])
            return 1;
    }
    return 0;
}

int32_t jp_IsEnd(void *jp, const uint8_t *e)
{
    int8_t i;

    for (i = 0; i < 4; i++) {
        const uint8_t *pd = dm_GetPhraseDataPtr();

        if (dm_GetTGAt2(DE_B(e, DE_POS), (uint8_t)i) & pd[PD_END_MASK + i])
            return 0;
    }
    /* A word of the plain class that says so in its own attributes does not
       end a phrase either, which is the one case the mask cannot express. */
    if (jp_CheckType(jp, dm_GetTGAt(DE_B(e, DE_POS))) == 1
        && (DE_B(e, DE_ATTR2) & 0x02))
        return 0;
    return 1;
}

/* How many moras a path runs to, counting one more entry that is not on it
   yet -- which is what AddPath is asking when it calls this. */
int16_t jp_GetMoraOnPath(void *jp, const uint8_t *path, int16_t extra)
{
    void   *d = JP_SEARCH_OF(jp);
    int16_t moras = 0;
    int16_t i;

    for (i = 0; i < path[JPT_COUNT]; i++) {
        int16_t e = path[JPT_AT + i];

        moras = (int16_t)(moras + DE_B(DS_ENTRY_AT(d, e), DE_KANALEN));
    }
    return (int16_t)(moras + DE_B(DS_ENTRY_AT(d, extra), DE_KANALEN));
}

/* ---- what one sub-word says about itself ---------------------------- */

/* The one byte of a sub-word that is worked out here rather than copied.
 *
 * The same fact -- that the entry's second attribute byte has its top bit --
 * is recorded in a different bit depending on what the word's type group says
 * it is, so that whatever reads the sub-word can tell a phrase head from a
 * prefix from a suffix without going back to the type table. Nothing written
 * so far reads any of these bits; the names are from where each one is set.
 */
void jp_SetWordAttr(void *jp, uint8_t *sub, const uint8_t *e)
{
    uint8_t pos = DE_B(e, DE_POS);

    (void)jp;
    sub[JS_ATTR] = 0;

    if ((dm_GetTGAt2(pos, 2) & 0x03) || (dm_GetTGAt2(pos, 3) & 0x01)) {
        /* A word the type table puts after a phrase rather than at its head.
           IBM asks both questions again here; they can only be answered the
           same way. */
        if ((dm_GetTGAt2(pos, 2) & 0x03) && dm_GetTGAt2(pos, 3) == 0
            && (DE_B(e, DE_ATTR2) & 0x80))
            sub[JS_ATTR] |= JS_ATTR_TAIL;
        return;
    }

    if ((dm_GetTGAt2(pos, 0) & 0xff) || (dm_GetTGAt2(pos, 1) & 0xd0)
        || (dm_GetTGAt2(pos, 2) & 0x80)) {
        if (DE_B(e, DE_ATTR2) & 0x80)
            sub[JS_ATTR] |= JS_ATTR_PREFIX;
    } else {
        if (DE_B(e, DE_ATTR2) & 0x80)
            sub[JS_ATTR] |= JS_ATTR_HEAD;
        if (DE_B(e, DE_ATTR2) & 0x20)
            sub[JS_ATTR] |= JS_ATTR_CONT;
    }

    if ((DE_B(e, DE_ATTR2) & 0x41) == 0x41 && (DE_B(e, DE_ATTR) & 0x80))
        sub[JS_ATTR] |= JS_ATTR_JOIN;
    if (DE_B(e, DE_ATTR) & 0x80)
        sub[JS_ATTR] |= JS_ATTR_NUMBER;
}

/* Every entry that appears on any path, copied out of DictSearch once, with
   an index from the entry number to the copy. An entry on no path keeps the
   minus one this fills the index with. */
void jp_MakeJrtSubTable(void *jp)
{
    void    *d = JP_SEARCH_OF(jp);
    int16_t  n = 0;
    uint8_t *sub = JP_SUB_AT(jp, n);
    int16_t  p, i;

    for (i = 0; i < JP_INDEX_N; i++)
        JP_INDEX_OF(jp, i) = -1;

    for (p = 0; p < (int32_t)JP_U16(jp, JP_PATH_COUNT); p++) {
        const uint8_t *path = JP_PATH_AT(jp, p);

        for (i = 0; i < path[JPT_COUNT]; i++) {
            int16_t        e = path[JPT_AT + i];
            const uint8_t *entry = DS_ENTRY_AT(d, e);
            int16_t        kana, k;

            if (JP_INDEX_OF(jp, e) >= 0)
                continue;

            *(int16_t *)(sub + JS_ENTRY)  = e;
            *(int16_t *)(sub + JS_AT)     = DE_W(entry, DE_AT);
            *(int32_t *)(sub + JS_MARK)   = DE_L(entry, DE_MARK);
            *(int16_t *)(sub + JS_OFFSET) = DE_W(entry, DE_OFFSET);
            *(int16_t *)(sub + JS_ACCENT) = DE_W(entry, DE_ACCENT);
            sub[JS_KANALEN]  = DE_B(entry, DE_KANALEN);
            sub[JS_CHARS]    = DE_B(entry, DE_CHARS);
            sub[JS_HIRAGANA] = DE_B(entry, DE_HIRAGANA);
            sub[JS_POS]      = DE_B(entry, DE_POS);
            jp_SetWordAttr(jp, sub, entry);

            kana = DE_B(entry, DE_KANALEN);
            if (kana > JS_KANA_N)
                kana = JS_KANA_N;
            for (k = 0; k < kana; k++)
                sub[JS_KANA + k] = DE_B(entry, DE_KANA + k);

            JP_INDEX_OF(jp, e) = n;
            n++;
            sub = JP_SUB_AT(jp, n);
        }
    }
}

/* ---- how dear it is to put two words together ----------------------- */

/* The adjustment the two words' own attribute bytes make to a cost the tables
 * have already given.
 *
 * It is a decision on both words' classes: the left word's class chooses a
 * block and the right word's chooses a branch within it, and what the branches
 * read are the attribute bytes rather than the type groups. Minus one means
 * the two may not stand together at all whatever the table said.
 *
 * `la' and `ra' are the two attribute pairs, left and right. `Make' seeds a
 * path by putting the real word on the left and a synthetic boundary on the
 * right, which is why the boundary branch below reads only the left word's
 * attributes: with no real neighbour there is nothing else to read.
 */
int16_t jp_CheckAdFlag(void *jp, const uint8_t *lt, const uint8_t *rt,
                              const uint8_t *la, const uint8_t *ra,
                              int16_t cost)
{
    int16_t lc = jp_CheckType(jp, lt);
    int16_t rc = jp_CheckType(jp, rt);
    uint8_t marked = 0;
    int8_t  adj = 0;
    int32_t r;

    if (rc == 0) {
        /* Nothing to the right of it. */
        switch (lc) {
        case 1:
            if (la[1] & 0x04)
                return -1;
            if (la[1] & 0x40)
                adj = (int8_t)(adj + 6);
            adj = (int8_t)(adj + 7 - (la[0] & 7));
            break;
        case 2:
            if (la[1] & 0x04)
                adj = (int8_t)(adj - 3);
            adj = (int8_t)(adj + 7 - (la[0] & 7));
            break;
        case 4:
            adj = (int8_t)(adj + 1 - (la[0] & 7));
            break;
        case 10:
            adj = (int8_t)(adj + 1);
            break;
        default:
            adj = (int8_t)(adj + 7 - (la[0] & 7));
            break;
        }
        r = cost + adj;
        if (r < 0)
            r = 0;
        return (int16_t)r;
    }

    switch (lc) {
    case 1:
        switch (rc) {
        case 8:
            /* Both marked, or neither: the two agree. */
            if (((ra[0] & 0x40) && (la[0] & 0x40))
                || (!(ra[0] & 0x40) && !(la[0] & 0x40)))
                adj = (int8_t)(adj - 2);
            if (ra[1] & 0x01) {
                if (ra[1] & 0x40)
                    return -1;
                adj = (int8_t)(adj + 8);
            }
            if (ra[1] & 0x04)
                adj = (int8_t)(adj - 3);
            if (ra[1] & 0x40)
                adj = (int8_t)(adj + 6);
            break;
        case 1:
            if (ra[1] & 0x01) {
                if (ra[1] & 0x40)
                    return -1;
                adj = (int8_t)(adj + 8);
            }
            if (ra[1] & 0x04)
                adj = (int8_t)(adj - 3);
            if (ra[1] & 0x40)
                adj = (int8_t)(adj + 6);
            if (la[0] & 0x80)
                return -1;
            break;
        case 3:
            if (ra[1] & 0x02)
                marked = 1;
            if (lt[1] & 0x08) {
                if (ra[1] & 0x40)
                    adj = (int8_t)(adj - 3);
                if (ra[1] & 0x10)
                    adj = (int8_t)(adj + 1);
            } else {
                if (ra[1] & 0x40)
                    adj = (int8_t)(adj + 1);
                if (ra[1] & 0x10)
                    adj = (int8_t)(adj - 1);
            }
            if (((ra[0] & 0x40) && (la[0] & 0x40))
                || (!(ra[0] & 0x40) && !(la[0] & 0x40)))
                adj = (int8_t)(adj - 3);
            if ((ra[1] & 0x20) && (lt[2] & 0x40))
                adj = (int8_t)(adj - 5);
            if (!(la[0] & 0x08) && !marked)
                return -1;
            if ((ra[1] & 0x01) && (la[1] & 0x08))
                adj = (int8_t)(adj - 2);
            break;
        case 2:
            if (ra[1] & 0x04)
                return -1;
            break;
        case 6:
            adj = (int8_t)(adj - 2);
            break;
        case 4:
            if (ra[0] & 0x20)
                adj = (int8_t)(adj + 15);
            if (ra[0] & 0x40)
                adj = (int8_t)(adj + 10);
            break;
        default:
            break;
        }
        if (la[1] & 0x02)
            adj = (int8_t)(adj - 3);
        if (la[1] & 0x01) {
            if (la[1] & 0x40)
                return -1;
            if (!marked)
                adj = (int8_t)(adj + 8);
        }
        break;

    case 2:
        switch (rc) {
        case 1:
            if (la[1] & 0x02)
                marked = 1;
            if (rt[1] & 0x08) {
                if (la[1] & 0x40)
                    adj = (int8_t)(adj - 3);
                if (la[1] & 0x10)
                    adj = (int8_t)(adj + 1);
            } else {
                if (la[1] & 0x40)
                    adj = (int8_t)(adj + 1);
                if (la[1] & 0x10)
                    adj = (int8_t)(adj - 1);
            }
            if ((la[1] & 0x20) && (rt[2] & 0x40))
                adj = (int8_t)(adj - 3);
            if (!(ra[0] & 0x10) && !marked)
                return -1;
            if (((ra[0] & 0x40) && (la[0] & 0x40))
                || (!(ra[0] & 0x40) && !(la[0] & 0x40)))
                adj = (int8_t)(adj - 2);
            if (ra[1] & 0x40)
                adj = (int8_t)(adj + 6);
            if ((la[1] & 0x01) && (ra[1] & 0x08))
                adj = (int8_t)(adj - 2);
            break;
        case 2:
            if (ra[1] & 0x08)
                adj = (int8_t)(adj - 3);
            if (ra[1] & 0x04)
                return -1;
            break;
        case 10:
            if (la[1] & 0x01)
                adj = (int8_t)(adj - 3);
            else
                return -1;
            break;
        default:
            break;
        }
        break;

    case 3:
        if (rc == 3) {
            if (ra[1] & 0x08)
                adj = (int8_t)(adj - 3);
            if (ra[1] & 0x04)
                return -1;
        }
        if (rc == 1 && (ra[1] & 0x01))
            return -1;
        if (rc == 4)
            return -1;
        if (rc == 1 && (ra[1] & 0x40))
            adj = (int8_t)(adj + 6);
        if (la[1] & 0x01)
            return -1;
        if (la[0] & 0x80)
            return -1;
        break;

    case 4:
        switch (rc) {
        case 4:
            if ((la[0] & 0x40) && (ra[0] & 0x20)) {
                adj = (int8_t)(adj - 4);
            } else {
                if (ra[0] & 0x20)
                    return -1;
                if (((ra[0] & la[0]) & 0xf8) == 0)
                    adj = (int8_t)(adj + 15);
            }
            break;
        case 6:
            if (((la[0] & ra[0]) & 0xf8) != 0)
                adj = (int8_t)(adj - 14);
            break;
        case 5:
            return -1;
        case 3:
            adj = (int8_t)(adj - 6);
            break;
        case 1:
            if (ra[1] & 0x01)
                return -1;
            break;
        default:
            break;
        }
        break;

    case 5:
        if (rc == 4)
            adj = (int8_t)(adj - 4);
        else if (rc == 5 || rc == 6)
            return -1;
        break;

    case 6:
        if (rc == 4 || rc == 5 || rc == 6)
            return -1;
        if (rc == 1)
            return -1;
        break;

    case 8:
        if ((ra[0] & 0x40) && (la[0] & 0x40))
            adj = (int8_t)(adj - 1);
        if (!(ra[0] & 0x40) && !(la[0] & 0x40))
            adj = (int8_t)(adj - 2);
        if (rc == 1 && (ra[1] & 0x01))
            return -1;
        if (rc == 1 && (ra[1] & 0x40))
            adj = (int8_t)(adj + 6);
        break;

    case 9:
    case 10:
        if (rc == 3) {
            if (ra[1] & 0x01)
                adj = (int8_t)(adj - 6);
            else
                return -1;
        } else if (rc == 10 || rc == 9) {
            adj = (int8_t)(adj - 6);
        } else {
            return -1;
        }
        break;

    case 7:
        if (rc == 1 && (ra[1] & 0x04))
            return -1;
        if (rc == 3)
            adj = (int8_t)(adj + 15);
        break;

    default:
        break;
    }

    adj = (int8_t)(adj + 7 - (ra[0] & 7));
    r = cost + adj;
    if (r < 0)
        r = 0;
    return (int16_t)r;
}

/* One sweep of a cost table: the right-hand word's type group read bit by
   bit, each bit that is set naming one signed byte, and the last one to be
   named winning. */
#define SWEEP(pd, base, rows)                                            \
    do {                                                                 \
        for (i = 0; i < (rows); i++) {                                   \
            uint8_t bit = 0x80;                                          \
            for (j = 0; j < 8; j++, bit = (uint8_t)(bit >> 1))           \
                if (rt[i] & bit)                                         \
                    v = (int8_t)(pd)[(base) + i * 8 + j];                \
        }                                                                \
    } while (0)

/* The same, but leaving one place in the table alone once a cost has been
   found. Two of the fourteen do that, and only for the fifth bit of the third
   byte. */
#define SWEEP_KEEP(pd, base)                                             \
    do {                                                                 \
        for (i = 0; i < 4; i++) {                                        \
            uint8_t bit = 0x80;                                          \
            for (j = 0; j < 8; j++, bit = (uint8_t)(bit >> 1)) {         \
                if (i == 2 && j == 4 && v > 0)                           \
                    continue;                                            \
                if (rt[i] & bit)                                         \
                    v = (int8_t)(pd)[(base) + i * 8 + j];                \
            }                                                            \
        }                                                                \
    } while (0)

/* And the adjustment pass that follows most of them. When `adjust' is set the
   adjusted cost is what comes out; when it is not, the adjustment is asked
   only for its sign and the unadjusted cost is kept. */
#define ADJUST()                                                         \
    do {                                                                 \
        if (adjust) {                                                    \
            v = jp_CheckAdFlag(jp, lt, rt, la, ra, v);                   \
        } else if (jp_CheckAdFlag(jp, lt, rt, la, ra, v) < 0) {          \
            return -1;                                                   \
        }                                                                \
    } while (0)

/* What it costs to put the right-hand word after the left-hand one, or minus
 * one if it may not go there at all.
 *
 * The left word's type group picks one of fourteen tables and the right
 * word's reads the cost out of it. The tests are in IBM's order and the first
 * one that matches wins; the last few fall through to each other instead of
 * answering, so a word that matches more than one of those takes the dearest
 * -- or, in the last case but one, the cheaper of two.
 */
int16_t jp_JrtJrtCheck(void *jp, const uint8_t *lt, const uint8_t *rt,
                              const uint8_t *la, const uint8_t *ra,
                              int32_t adjust)
{
    const uint8_t *pd = dm_GetPhraseDataPtr();
    int16_t v = -1;
    int16_t i, j;

    /* A word that closes what came before it, or is itself closed, may be
       followed only by the end of the sentence. */
    if ((lt[3] & 0x44) || (lt[1] & 0x01))
        return (int16_t)((rt[3] & 0x80) ? 0 : -1);
    if (rt[1] & 0x01)
        return -1;

    if (lt[3] & 0x20) {
        /* The only table whose answer is taken as it stands. */
        SWEEP(pd, PD_TAIL, 4);
        return v;
    }
    if (lt[3] & 0x08) {
        SWEEP_KEEP(pd, PD_FZK);
        if (v > 0)
            ADJUST();
        return v;
    }
    if ((lt[3] & 0x10) && (lt[2] & 0x20)) {
        for (i = 0; i < 4; i++) {
            uint8_t bit = 0x80;

            /* Not a test on i, so it either skips the whole sweep or none of
               it; IBM put it inside the loop all the same. */
            if ((rt[2] & 0x20) && (rt[3] & 0x10))
                continue;
            for (j = 0; j < 8; j++, bit = (uint8_t)(bit >> 1))
                if (rt[i] & bit)
                    v = (int8_t)pd[PD_HEAD + i * 8 + j];
        }
        if (v > 0)
            ADJUST();
        return v;
    }
    if ((lt[3] & 0x10) && (lt[2] & 0x01)) {
        SWEEP(pd, PD_VERB, 4);
        if (v > 0)
            ADJUST();
        return v;
    }
    if (lt[2] & 0x02) {
        if (lt[3] & 0x01) {
            SWEEP(pd, PD_ADJ, 4);
        } else {
            if (rt[2] & 0x01)
                return -1;
            SWEEP(pd, PD_PREFIX, 4);
        }
        if (v > 0)
            ADJUST();
        return v;
    }
    if ((lt[2] & 0x01) && !(lt[3] & 0x10)) {
        if (lt[3] & 0x01)
            SWEEP(pd, PD_NOUN, 4);
        else
            SWEEP(pd, PD_NUMBER, 4);
        if (v > 0)
            ADJUST();
        return v;
    }
    if ((lt[2] & 0x20) && (lt[3] & 0x02)) {
        SWEEP(pd, PD_JOSHI, 4);
        if (v > 0)
            ADJUST();
        return v;
    }

    /* From here the tests fall through instead of answering. */
    if (lt[0] != 0 || (lt[1] & 0xc6) || (lt[2] & 0xc0)) {
        if (rt[3] & 0x80)
            return 0;
        /* IBM's copy of the adjustment pass stands here and cannot fire,
           since nothing has set a cost yet. */
        v = -1;
    }
    if (lt[1] & 0x30) {
        SWEEP(pd, PD_TABLE, 4);
        if (v > 0)
            ADJUST();
        if (v > 0)
            return v;
    }
    if ((lt[1] & 0x80) || (lt[2] & 0x20)) {
        if (lt[3] & 0x01) {
            SWEEP(pd, PD_TABLE_3, 4);
        } else {
            SWEEP_KEEP(pd, PD_TABLE_2);
            if (v >= 0 && (lt[2] & 0x0c))
                v = (int16_t)(v + 7 - (ra[0] & 7));
        }
    }
    if (lt[2] & 0x10) {
        uint8_t top;

        SWEEP(pd, PD_KANJI, 2);
        for (j = 0, top = 0x80; j < 4; j++, top = (uint8_t)(top >> 1))
            if (rt[2] & top)
                v = (int8_t)pd[PD_KANJI_TG2 + j];
        if (rt[3] & 0x80)
            v = (int8_t)pd[PD_KANJI_END];
        if (rt[3] & 0x10)
            v = (int8_t)pd[PD_KANJI_MARK];
        if (v > 0)
            ADJUST();
        return v;
    }
    if (lt[2] & 0x0c) {
        int16_t was = v;

        SWEEP(pd, PD_SUFFIX, 4);
        if (v > 0)
            ADJUST();
        /* Two tables have now had their say, and the cheaper answer wins --
           unless this one refused, in which case the earlier one stands so
           long as the adjustment does not refuse it too. */
        if (was >= 0 && v >= 0)
            v = (was < v) ? was : v;
        if (was >= 0 && v < 0) {
            if (jp_CheckAdFlag(jp, lt, rt, la, ra, v) < 0)
                return -1;
            v = was;
        }
        return v;
    }

    /* Nought is a real cost here, where every block above wanted more. */
    if (v >= 0)
        ADJUST();
    return v;
}

#undef SWEEP
#undef SWEEP_KEEP
#undef ADJUST

/* ---- growing a path ------------------------------------------------- */

/* One entry put on the end of one path, into a path of its own, or nothing if
 * it may not go there or would cost too much.
 *
 * The four refusals after the cost is settled are what keep the search
 * finite: a path dearer than the cheapest already recorded at the character
 * it reaches, one over the whole allowance, one more path than there is room
 * for, and one running to more moras than a phrase may.
 */
int32_t jp_AddPath(void *jp, const uint8_t *path, const uint8_t *entry,
                   uint8_t *out, int16_t nPaths, int16_t entryIndex)
{
    void          *d = JP_SEARCH_OF(jp);
    const uint8_t *last;
    const uint8_t *ltg, *rtg;
    int16_t        nextAt;
    int16_t        v;
    int16_t        i;

    if (path[JPT_COUNT] == JPT_AT_N)
        return 0;

    last   = DS_ENTRY_AT(d, path[JPT_AT + path[JPT_COUNT] - 1]);
    nextAt = (int16_t)(DE_W(entry, DE_AT) + DE_B(entry, DE_CHARS));
    ltg    = dm_GetTGAt(DE_B(last, DE_POS));
    rtg    = dm_GetTGAt(DE_B(entry, DE_POS));

    v = jp_JrtJrtCheck(jp, ltg, rtg, last + DE_ATTR, entry + DE_ATTR, 1);
    if (v >= 0 && DE_L(entry, DE_COST) == 2)
        v = (int16_t)(v + 2);
    if (v >= 0 && DE_B(entry, DE_CHARS) > 1) {
        /* A word of more than one character is three cheaper a character,
           and the floor of nought belongs to that adjustment alone: a cost
           the tables already refused stays refused, and is thrown out by the
           test below rather than turned into a free join. */
        v = (int16_t)(v - (DE_B(entry, DE_CHARS) - 1) * 3);
        if (v < 0)
            v = 0;
    }

    if (path[JPT_COST] + v > JP_COST_AT(jp, nextAt))
        return 0;
    if (path[JPT_COST] + v >= JP_COST_MOST)
        return 0;
    if (v < 0)
        return 0;
    if (nPaths >= JP_PATH_N)
        return 0;
    if (jp_GetMoraOnPath(jp, path, entryIndex) >= JP_MORAS_MOST)
        return 0;

    for (i = 0; i < path[JPT_COUNT]; i++)
        out[JPT_AT + i] = path[JPT_AT + i];
    out[JPT_AT + path[JPT_COUNT]] = (uint8_t)entryIndex;
    out[JPT_COUNT] = (uint8_t)(path[JPT_COUNT] + 1);
    out[JPT_COST]  = (uint8_t)(path[JPT_COST] + v);
    out[JPT_END]   = (uint8_t)jp_IsEnd(jp, entry);
    out[JPT_CONT]  = (uint8_t)jp_IsContinuable(jp, entry);

    /* The cheapest way to reach that character, unless this word is one the
       search is not allowed to prune behind. */
    if (!(rtg[2] & 0x02) && !(DE_B(entry, DE_ATTR2) & 0x01))
        JP_COST_AT(jp, nextAt) = out[JPT_COST];
    return 1;
}

/* ---- the whole search ----------------------------------------------- */

/* Every path that covers the text from the given character on.
 *
 * The seeds are the entries starting there that may head a phrase, each
 * scored against a synthetic boundary; then each path that may be continued
 * is grown by every entry starting where it leaves off, until no path can be
 * grown. The entries are taken in falling order, which is IBM's, and matters
 * only in that it settles which of two equally cheap paths is written first.
 */
void jp_Make(void *jp, int16_t at)
{
    void    *d = JP_SEARCH_OF(jp);
    uint8_t  boundary[4];
    uint8_t  none[2];
    int16_t  nEntry;
    int16_t  first, past, e;
    int16_t  nPaths = 0;
    int16_t  i;

    JP_U16(jp, JP_PATH_COUNT) = 0;
    nEntry = DS_W(d, DS_COUNT);
    if (nEntry == 0)
        return;

    /* The word on the far side of the sentence's edge: no attributes at all,
       and the one type-group bit that CheckType reads as the boundary. */
    for (i = 0; i < 3; i++)
        boundary[i] = 0;
    boundary[3] = 0x80;
    for (i = 0; i < 2; i++)
        none[i] = 0;

    for (i = 0; i < JP_COST_N; i++)
        JP_COST_AT(jp, i) = 0xff;
    for (i = 0; i < JP_PATH_N * JP_PATH_SIZE; i++)
        JP_B(jp, JP_PATH + i) = 0;

    /* The run of entries that start at this character. They are together,
       so finding the first and walking to the end of the run is enough. */
    for (e = 0; ; e++) {
        if (DE_W(DS_ENTRY_AT(d, e), DE_AT) == at)
            break;
        if (e >= JP_PATH_N)
            return;
    }
    first = e;
    while (DE_W(DS_ENTRY_AT(d, e), DE_AT) == at)
        e++;
    past = e;

    for (e = (int16_t)(past - 1); e >= first; e--) {
        const uint8_t *entry = DS_ENTRY_AT(d, e);
        uint8_t       *path  = JP_PATH_AT(jp, nPaths);
        const uint8_t *tg;
        int16_t        v;

        if (!jp_IsHead(jp, entry))
            continue;

        tg = dm_GetTGAt(DE_B(entry, DE_POS));
        v  = jp_JrtJrtCheck(jp, tg, boundary, entry + DE_ATTR, none, 0);
        if (v < 0)
            continue;

        /* Three costs the dictionary marks on the entry itself. */
        if (DE_L(entry, DE_COST) == 2)
            v = (int16_t)(v + 2);
        if (DE_L(entry, DE_COST) == 8)
            v = (int16_t)(v - 7);
        if (DE_L(entry, DE_COST) == 9)
            v = (int16_t)(v - 5);

        path[JPT_COST] = (uint8_t)jp_CheckAdFlag(jp, tg, boundary,
                                                 entry + DE_ATTR, none, v);
        if ((int8_t)path[JPT_COST] < 0)
            continue;

        /* A word the dictionary marked with that one cost, of the plain
           class, carrying a case marker, costs nothing at all when the
           character before it is a case marker too. */
        if (DE_L(entry, DE_COST) == 1 && jp_CheckType(jp, tg) == 1
            && (DE_B(entry, DE_ATTR) & 0x80)
            && at > 1 && ds_CheckCaseMarker(d, (int16_t)(at - 1)))
            path[JPT_COST] = 0;

        path[JPT_COUNT]  = 1;
        path[JPT_AT]     = (uint8_t)e;
        path[JPT_END]    = (uint8_t)jp_IsEnd(jp, entry);
        path[JPT_CONT]   = (uint8_t)jp_IsContinuable(jp, entry);

        if (!(tg[2] & 0x02) && !(DE_B(entry, DE_ATTR2) & 0x01))
            JP_COST_AT(jp, (int16_t)(DE_W(entry, DE_AT)
                                     + DE_B(entry, DE_CHARS)))
                = path[JPT_COST];
        nPaths++;
    }

    if (nPaths == 0)
        return;

    for (i = 0; i < JP_PATH_N; i++) {
        const uint8_t *path = JP_PATH_AT(jp, i);
        int16_t        last, want;

        if (path[JPT_CONT] != 1)
            continue;

        last = path[JPT_AT + path[JPT_COUNT] - 1];
        want = (int16_t)(DE_W(DS_ENTRY_AT(d, last), DE_AT)
                         + DE_B(DS_ENTRY_AT(d, last), DE_CHARS));

        /* The run of entries that start where this path leaves off. Where
           there is none the path simply stops here. */
        for (e = 0; e < DS_W(d, DS_COUNT); e++)
            if (DE_W(DS_ENTRY_AT(d, e), DE_AT) == want)
                break;
        if (e == DS_W(d, DS_COUNT))
            continue;

        first = e;
        while (DE_W(DS_ENTRY_AT(d, e), DE_AT) == want)
            e++;
        past = e;

        for (e = (int16_t)(past - 1); e >= first; e--)
            if (jp_AddPath(jp, path, DS_ENTRY_AT(d, e),
                           JP_PATH_AT(jp, nPaths), nPaths, e))
                nPaths++;
    }

    JP_U16(jp, JP_PATH_COUNT) = (uint16_t)nPaths;
    jp_MakeJrtSubTable(jp);
}
