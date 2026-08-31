/* Turning the ways a sentence can be read into the phrases it can be spoken
 * as.
 *
 * The path search leaves behind a set of paths -- each one a list of
 * dictionary entries that between them cover the text -- and this is what
 * turns each of those into a phrase: the words that make it up with their
 * readings, where its accent falls, how many moras it runs to, and what kind
 * of phrase it is. The answers go into a buffer of 686 slots, which is the
 * same buffer TextAnalysis holds three of, and Copy is what fills this one
 * from one of those.
 *
 * A phrase here is an accent phrase rather than a word: a content word and
 * whatever function words hang off it. That is why SetPhrasePart walks the
 * function-word chain the dictionary search left, and why a path with no
 * function word after it takes a different road at the end of that method.
 *
 * The record is IBM's and rom/jajp/phrasebuf.h is the map, checked against
 * the object by `tools/rom-offsets.py phrasebuf'. So is the phrase, which
 * IBM calls a _W_PHRASE_T, and the path and sub-word records this reads out
 * of JPath, which are rom/jajp/jpath.h.
 *
 * Held to IBM's answer by test/romprims.sh.
 */

#include <stdint.h>
#include <string.h>
#include "jprom.h"
#include "phrasebuf.h"
#include "jpath.h"
#include "dictsearch.h"
#include "txtanal.h"
#include "inputchar.h"

/* The pointers, which are parked past the record, here and in the two
   records this reaches into. */
#define PB_OWNER_OF(pb)  (*(void **)PB_P((pb), PB_OWNER_AT))
#define PB_SEARCH_OF(pb) (*(void **)PB_P((pb), PB_SEARCH_AT))
#define PB_JPATH_OF(pb)  (*(void **)PB_P((pb), PB_JPATH_AT))
#define TA_AT(ta, which) (*(void **)((uint8_t *)(ta) + (which)))
#define IC_TEXTP_OF(in)  (*(const uint8_t **)((uint8_t *)(in) + IC_TEXTP_AT))
#define IC_S16(in, off)  (*(int16_t *)((uint8_t *)(in) + (off)))

/* And the two records read out of JPath. */
#define JS_B(sub, off)   (*((const uint8_t *)(sub) + (off)))
#define JS_S16(sub, off) (*(const int16_t *)((const uint8_t *)(sub) + (off)))
#define JS_S32(sub, off) (*(const int32_t *)((const uint8_t *)(sub) + (off)))

/* What ModifyPos fills in and FzkParsing then reads: a bit per part of
   speech, which is how a function word says what may precede it. */
#define PB_POS_BYTES    14

/* How long the chain of function words after one phrase may get, and how many
   moras it may run to, before SetPhrasePart gives up on it. */
#define PB_FZK_MOST     15
#define PB_FZK_MORAS    0x19

/* One two-byte verb of the table, plus the nought after it. */
#define PB_VERB_SIZE    3

/* ---- being made, and taking a copy ----------------------------------- */

void *pb_ctor(void *pb, void *analysis)
{
    PB_OWNER_OF(pb)  = analysis;
    PB_SEARCH_OF(pb) = TA_AT(analysis, TA_DICTSEARCH_AT);
    PB_JPATH_OF(pb)  = TA_AT(analysis, TA_JPATH_AT);
    return pb;
}

/* One of the owner's three buffers taken whole. Nothing checks the number. */
void pb_Copy(void *pb, int16_t which)
{
    memcpy(PB_P(pb, PB_BUFFER),
           (uint8_t *)PB_OWNER_OF(pb) + TA_BUFFERS + which * TA_BUFFER_SIZE,
           PB_BUFFER_SIZE);
}

/* ---- what a part of speech allows ------------------------------------ */

/* The bit vector a function-word lookup is handed, which says what kind of
 * thing stands in front of it.
 *
 * A part of speech that is not itself a phrase head is written out as the
 * three bytes its table row carries, with one bit added where the row says
 * the word may also close a phrase. One that is a head is written as a single
 * bit instead, chosen by the row's fourth byte.
 *
 * That choice is made with a signed division, and a row whose fourth byte is
 * nought divides minus one by eight, which gives nought and a remainder of
 * minus one. The shift that follows is then by minus one, which on this
 * machine means by thirty-one, so the byte written is nought. It is IBM's and
 * it is kept; the mask is written out here because a negative shift is not
 * something C defines. */
void pb_ModifyPos(void *pb, uint8_t *out, uint8_t pos)
{
    const uint8_t *tg = dm_GetTGAt(pos);

    (void)pb;
    if (!(tg[1] & 0x01)) {
        out[0] = tg[0];
        out[1] = tg[1];
        out[2] = tg[2];
        if (tg[2] & 0x01)
            out[2] = (uint8_t)(out[2] | 0x20);
        return;
    }

    {
        int16_t i;
        int32_t which = (int32_t)tg[3] - 1;
        int16_t byte  = (int16_t)(which / 8);
        int16_t bit   = (int16_t)(which % 8);

        for (i = 0; i < PB_POS_BYTES; i++)
            out[i] = 0;
        out[byte] = (uint8_t)((uint32_t)0x80 >> (bit & 31));
    }
}

/* ---- what a word says about the phrase it is in ---------------------- */

/* Whether a word can be the last one in an accent phrase. The first two tests
   refuse it outright and the three after them let it through, which is why
   the order matters and a single mask would not do. */
int32_t pb_IsBunsetsuEnd(void *pb, const uint8_t *sub)
{
    const uint8_t *tg = dm_GetTGAt(JS_B(sub, JS_POS));

    (void)pb;
    if (tg[1] & 0x01) return 0;
    if (tg[2] & 0x02) return 0;
    if (tg[1] & 0x38) return 1;
    if (tg[2] & 0x3d) return 1;
    if (tg[3] & 0x66) return 1;
    return 0;
}

/* Whether a one-character word is one of the verbs a doubled consonant may
   attach to. The word has to be a single character with no hiragana in it,
   and then its two bytes are looked for in the table this object carries. */
int32_t pb_IsSokuonTankanVerb(void *pb, const uint8_t *sub)
{
    const uint8_t *textp;
    char           pair[3];
    int16_t        i;

    if (JS_B(sub, JS_CHARS) != 1)
        return 0;
    if (JS_B(sub, JS_HIRAGANA) != 0)
        return 0;

    textp = IC_TEXTP_OF(TA_AT(PB_OWNER_OF(pb), TA_INPUTCHAR_AT));
    pair[0] = (char)textp[JS_S32(sub, JS_MARK)];
    pair[1] = (char)textp[JS_S32(sub, JS_MARK) + 1];
    pair[2] = 0;

    for (i = 0; i < jajp_SokonTnknVerb_n / PB_VERB_SIZE; i++)
        if (strcmp((const char *)jajp_SokonTnknVerb + i * PB_VERB_SIZE,
                   pair) == 0)
            return 1;
    return 0;
}

/* What kind of phrase one word on its own makes. Only a phrase of a single
   word whose accent falls on its last mora is looked at; everything else
   keeps the kind it already had. */
int16_t pb_GetSpecialPhraseType(void *pb, const uint8_t *w)
{
    int16_t type = (int16_t)w[WP_TYPE];

    (void)pb;
    if (w[WP_WORDS] != 1)
        return type;
    if (w[WP_ACCENT] != *(const uint16_t *)(w + WP_MORAS))
        return type;

    if (*(const uint16_t *)(w + WP_MORAS) == 1)
        type = (dm_GetTGAt2(w[WP_WORD + WW_POS], 3) & 0x20) ? 6 : 5;
    else
        type = (dm_GetTGAt2(w[WP_WORD + WW_POS], 2) & 0x18) ? 2 : 4;
    return type;
}

/* One particular pair the analysis must not join: a doubled-consonant verb
   followed by the two-mora function word whose reading is the two codes
   below. Answers minus one to refuse the phrase. */
int16_t pb_ChkTTELink(void *pb, int32_t sokuon, const uint8_t *f)
{
    const uint8_t *dict;
    uint8_t        kana[2];
    uint8_t        n;
    int16_t        i;

    (void)pb;
    if (sokuon != 1)
        return 0;
    if (f[WF_KANALEN] != 2)
        return 0;
    if ((f[WF_CODE] & 0x7f) != 0x49)
        return 0;

    dict = dm_GetFuncDictEx();
    n    = (uint8_t)(dict[*(const int16_t *)(f + WF_AT)] - 6);
    if (n != 2)
        return 0;

    for (i = 0; i < n; i++)
        kana[i] = dict[*(const int16_t *)(f + WF_AT) + 6 + i];

    if (kana[0] == 0xfd && kana[1] == 0x23)
        return -1;
    return 0;
}

/* ---- filling one phrase from one path -------------------------------- */

/* The words of a path written into a phrase, and the phrase's reading length
 * and accent counted up as they go.
 *
 * Each word on the path is a sub-word of JPath's, and what goes into the
 * phrase is its reading, its accent, how many characters it covers and what
 * it is. A word whose part of speech is not a phrase head keeps that part of
 * speech; one that is a head is written as one of three stand-ins instead,
 * which is what tells the output side that the word carries no accent of its
 * own.
 *
 * Only the first nine codes of a reading are copied, which is all the phrase
 * has room for and is the same nine JPath itself kept.
 */
void pb_SetJrt(void *pb, const uint8_t *path, uint8_t *w,
               int16_t *outKana, int16_t *outAccent)
{
    void   *jp = PB_JPATH_OF(pb);
    int16_t moras = 0;
    int16_t i;

    if (JP_S16(jp, JP_PATH_COUNT) == 0)
        return;

    *outAccent = 0;
    *outKana   = 0;

    for (i = 0; i < path[JPT_COUNT]; i++) {
        const uint8_t *sub = JP_SUB_AT(jp, JP_INDEX_OF(jp, path[JPT_AT + i]));
        const uint8_t *tg  = dm_GetTGAt(JS_B(sub, JS_POS));
        uint8_t       *ww  = WW_SLOT(w, i);
        int16_t        n, k;

        ww[WW_KANALEN] = JS_B(sub, JS_KANALEN);
        ww[WW_CHARS]   = JS_B(sub, JS_CHARS);
        *(int16_t *)(ww + WW_ACCENT) = JS_S16(sub, JS_ACCENT);
        ww[WW_ATTR]    = JS_B(sub, JS_ATTR);
        *(int16_t *)(ww + WW_OFFSET) = JS_S16(sub, JS_OFFSET);

        if (tg[1] & 0x01) {
            if (tg[2] > 0)
                ww[WW_POS] = (tg[2] & 0x20) ? 0 : 2;
            else
                ww[WW_POS] = 0x14;
        } else {
            ww[WW_POS] = JS_B(sub, JS_POS);
            if (tg[3] & 0x10)
                w[WP_TYPE] = 9;
        }

        n = JS_B(sub, JS_KANALEN) > WW_KANA_N
            ? WW_KANA_N : (int16_t)JS_B(sub, JS_KANALEN);
        for (k = 0; k < n; k++)
            ww[WW_KANA + k] = JS_B(sub, JS_KANA + k);

        *outKana   = (int16_t)(*outKana + JS_B(sub, JS_CHARS));
        *outAccent = (int16_t)(*outAccent + JS_B(sub, JS_HIRAGANA));
        moras      = (int16_t)(moras + JS_B(sub, JS_KANALEN));
    }

    if (*outAccent != 0 && w[WP_TYPE] != 9)
        w[WP_TYPE] = 1;
    w[WP_KANALEN] = (uint8_t)moras;
}

/* One path turned into as many phrases as the function words after it allow.
 *
 * The dictionary search leaves a chain of function words for each place a
 * phrase may end, and each chain that fits is one phrase: the content words
 * of the path, then the function words in the order they are spoken, then the
 * accent and the length counted over both. A chain longer than fifteen words
 * or twenty-five moras is dropped rather than truncated.
 *
 * When no chain is taken at all -- which is the road out of the bottom of the
 * loop -- the path stands on its own, and then the last word on it has to be
 * one that can close a phrase or nothing is written.
 *
 * `n' is how many phrases are already in the buffer and is what comes back.
 * Every road out checks it against the bound, which is why the buffer cannot
 * be overrun even though nothing else bounds the loops.
 */
int16_t pb_SetPhrasePart(void *pb, const uint8_t *path, int16_t n,
                         int16_t fzk, int32_t sokuon, uint8_t *out)
{
    void   *jp     = PB_JPATH_OF(pb);
    void   *search = PB_SEARCH_OF(pb);
    int16_t kana   = 0;
    int16_t i;

    if (n == PB_SLOT_N)
        return n;

    for (i = 0; i < path[JPT_COUNT]; i++)
        kana = (int16_t)(kana + JS_B(JP_SUB_AT(jp,
                          JP_INDEX_OF(jp, path[JPT_AT + i])), JS_KANALEN));

    for (i = (int16_t)(fzk - 1); i >= 0; i--) {
        int16_t  chain[PB_FZK_MOST + 1];
        int16_t  nChain = 0;
        int16_t  moras  = 0;
        int16_t  total  = kana;
        int32_t  tooLong = 0;
        int16_t  at      = i;
        int16_t  accent  = 0;
        int16_t  length  = 0;
        int16_t  nOut    = 0;
        uint8_t *w       = WP_SLOT(out, n);
        int16_t  j;
        int16_t  a, b;

        if (!(DS_FZK_B(search, i, PF_FLAGS) & 0x01))
            continue;

        /* The chain walked from this word back along its links, stopping when
           it runs out or when it has grown past what a phrase may carry. */
        for (;;) {
            chain[nChain++] = at;
            total = (int16_t)(total
                     + dm_GetFuncDictEx()[DS_FZK_S16(search, at, PF_AT)]);
            total = (int16_t)(total - 6);
            if (nChain > PB_FZK_MOST || total > PB_FZK_MORAS) {
                tooLong = 1;
                break;
            }
            at = (int16_t)(int8_t)DS_FZK_B(search, at, PF_LINK);
            if (at < 0)
                break;
        }
        if (tooLong)
            continue;

        /* And written out in the order they are spoken, which is the order
           the chain was walked in reversed. */
        for (j = (int16_t)(nChain - 1); j >= 0; j--) {
            int16_t  f = chain[j];
            uint8_t *wf = WF_SLOT(w, nOut);

            wf[WF_CODE]    = DS_FZK_B(search, f, PF_CODE);
            *(int16_t *)(wf + WF_AT)     = DS_FZK_S16(search, f, PF_AT);
            wf[WF_KANALEN] = DS_FZK_B(search, f, PF_KANALEN);
            *(int16_t *)(wf + WF_ACCENT) = DS_FZK_S16(search, f, PF_ACCENT);
            *(int16_t *)(wf + WF_OFFSET) = DS_FZK_S16(search, f, PF_OFFSET);

            if (DS_FZK_B(search, f, PF_FLAGS) & 0x02) {
                moras = (int16_t)(moras + 3);
                if (j == 0)
                    wf[WF_CODE] = (uint8_t)(wf[WF_CODE] | 0x80);
            }
            moras  = (int16_t)(moras + 3);
            accent = (int16_t)(accent + DS_FZK_B(search, f, PF_MORAS));
            length = (int16_t)(length + DS_FZK_B(search, f, PF_KANALEN));
            nOut++;
        }

        if (pb_ChkTTELink(pb, sokuon, WF_SLOT(w, 0)) != 0)
            continue;

        w[WP_TYPE] = 0;
        *(int32_t *)(w + WP_COST) =
            JS_S32(JP_SUB_AT(jp, JP_INDEX_OF(jp, path[JPT_AT])), JS_MARK);
        pb_SetJrt(pb, path, w, &a, &b);

        *(int16_t *)(w + WP_MORAS) = (int16_t)(length + a);
        w[WP_ACCENT] = (uint8_t)(accent + b);
        w[WP_WORDS]  = path[JPT_COUNT];
        w[WP_CHARS]  = (uint8_t)(path[JPT_SPARE] + moras);
        w[WP_UNREAD_06] = (uint8_t)nOut;
        if (w[WP_ACCENT] == *(uint16_t *)(w + WP_MORAS))
            w[WP_TYPE] = 3;

        n++;
        if (n == PB_SLOT_N)
            return n;
    }

    /* No function word after it, so the path is a phrase on its own if its
       last word can close one. */
    {
        const uint8_t *sub =
            JP_SUB_AT(jp, JP_INDEX_OF(jp, path[JPT_SPARE + path[JPT_COUNT]]));
        uint8_t *w = WP_SLOT(out, n);
        int16_t  a, b;

        if (path[JPT_COUNT] == 1 && JS_B(sub, JS_CHARS) == 0)
            return n;
        if (!pb_IsBunsetsuEnd(pb, sub))
            return n;

        /* The cost is the first word's, not the last one's, which is the
           same word the arm above takes it from. */
        *(int32_t *)(w + WP_COST) =
            JS_S32(JP_SUB_AT(jp, JP_INDEX_OF(jp, path[JPT_AT])), JS_MARK);
        w[WP_TYPE] = 0;
        pb_SetJrt(pb, path, w, &a, &b);

        *(int16_t *)(w + WP_MORAS) = a;
        w[WP_ACCENT] = (uint8_t)b;
        w[WP_CHARS]  = path[JPT_SPARE];
        w[WP_WORDS]  = path[JPT_COUNT];
        w[WP_UNREAD_06] = 0;
        w[WP_TYPE]   = (uint8_t)pb_GetSpecialPhraseType(pb, w);
        n++;
    }
    return n;
}

/* Every path there is, turned into phrases, last path first.
 *
 * For each one the last word decides what may follow it: its part of speech
 * becomes a bit vector, and the dictionary search is asked which function
 * words agree with it at the place the path ends. A word that may not end a
 * phrase at all, and one that neither has a function word after it nor can
 * close a phrase itself, are both stepped over.
 *
 * The paths are walked backwards because the buffer fills forwards and the
 * output side wants the longest first.
 */
int16_t pb_SetPhraseBuffer(void *pb, uint8_t *out)
{
    void   *jp     = PB_JPATH_OF(pb);
    void   *search = PB_SEARCH_OF(pb);
    int16_t n      = 0;
    int16_t p;

    for (p = (int16_t)(JP_S16(jp, JP_PATH_COUNT) - 1); p >= 0; p--) {
        uint8_t        pos[PB_POS_BYTES];
        const uint8_t *path = JP_PATH_AT(jp, p);
        const uint8_t *sub;
        int16_t        moras, fzk;
        int32_t        sokuon;

        if (n == PB_SLOT_N)
            return n;

        memset(pos, 0, sizeof pos);

        sub = JP_SUB_AT(jp, JP_INDEX_OF(jp, path[JPT_SPARE + path[JPT_COUNT]]));
        pb_ModifyPos(pb, pos, JS_B(sub, JS_POS));

        moras = (int16_t)(JS_S16(sub, JS_AT) + JS_B(sub, JS_CHARS));
        if (moras < IC_S16(TA_AT(PB_OWNER_OF(pb), TA_INPUTCHAR_AT), IC_COUNT))
            fzk = ds_FzkParsing(search, pos, moras);
        else
            fzk = 0;

        if (dm_GetTGAt2(JS_B(sub, JS_POS), 2) & 0x02)
            continue;
        if (fzk == 0 && pb_IsBunsetsuEnd(pb, sub) != 1)
            continue;

        sokuon = pb_IsSokuonTankanVerb(pb, sub);
        n = pb_SetPhrasePart(pb, path, n, fzk, sokuon, out);
    }
    return n;
}
