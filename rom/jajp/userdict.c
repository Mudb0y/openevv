/* The user dictionary, which is how a caller teaches the romanizer a word.
 *
 * This is what stands behind the ECI dictionary calls when the language is
 * Japanese. A caller gives it a word as it is written and a reading in kana
 * with a caret where the accent falls; it turns both into the engine's own
 * codes, keeps them in a skip list keyed by the written form, and when a
 * sentence is analysed it looks every prefix of what is left of that sentence
 * up at once and writes what it finds straight into DictSearch's candidate
 * entries, beside what the built-in dictionary found.
 *
 * Two things are worth knowing before reading it. The key is not the caller's
 * bytes: makeKey normalises them first, so that half-width kana, a full-width
 * comma and a plain one, and the letters and digits all become the same
 * two-byte forms the built-in dictionary is written in. And a reading is
 * stored as the engine's yomi codes rather than as kana, because that is what
 * a candidate entry holds -- transKatakana2Yomi is DictSearch's own katakana
 * walk done again over a string instead of over the input.
 *
 * Held to IBM's answer by test/romprims.sh.
 */

#include <stdlib.h>
#include <string.h>
#include "jprom.h"
#include "dictsearch.h"
#include "txtanal.h"

/* Reaching the two classes this one writes through. DictSearch keeps IBM's
   layout, so its own header says where everything is. */
#define DS_AT(d, off)      ((uint8_t *)(d) + (off))
#define DS_W(d, off)       (*(int16_t *)DS_AT(d, off))
#define DS_L(d, off)       (*(int32_t *)DS_AT(d, off))
#define DS_ENTRY_AT(d, i)  DS_AT(d, DS_ENTRY + (i) * DS_ENTRY_SIZE)

#define DE_B(e, off)       (*((uint8_t *)(e) + (off)))
#define DE_W(e, off)       (*(int16_t *)((uint8_t *)(e) + (off)))
#define DE_L(e, off)       (*(int32_t *)((uint8_t *)(e) + (off)))

#define IC_OFFSET_AT(in, i) (*(int16_t *)((uint8_t *)(in) + IC_OFFSET + (i) * 2))
#define IC_MARK_AT(in, i)  (*(int32_t *)((uint8_t *)(in) + IC_MARK + (i) * 4))
#define IC_COUNT_AT(in)    (*(int16_t *)((uint8_t *)(in) + IC_COUNT))

/* The written forms the normalised key is made of. Read out of the object
   rather than decoded from the names they are stored under, which is the
   lesson rom/jajp/dictsearch.c records. */
static const char IDEO_SPACE[] = "\x81\x40";   /* the full-width space */
static const char IDEO_COMMA[] = "\x81\x41";
static const char IDEO_STOP[]  = "\x81\x42";

/* The small kana that do not count as a mora of their own, which is what
   decides where a caret falls. IBM's list is the small i, u and e and the
   small ya, yu and yo in both scripts -- and not the small a or the small o,
   which is its own choice and is left alone. */
static const char *const NOT_A_MORA[] = {
    "\x82\xa1", "\x83\x42",     /* small i */
    "\x82\xa3", "\x83\x44",     /* small u */
    "\x82\xa5", "\x83\x46",     /* small e */
    "\x83\x83", "\x82\xe1",     /* small ya */
    "\x83\x85", "\x82\xe3",     /* small yu */
    "\x83\x87", "\x82\xe5",     /* small yo */
    NULL
};

/* And the three that take the accent back one, because a doubled consonant or
   a lengthened vowel is not where an accent may sit. */
static const char *const NOT_ACCENTED[] = {
    "\x83\x62",                 /* the small katakana tsu */
    "\x82\xc1",                 /* and the hiragana one */
    "\x81\x5b",                 /* the long-vowel bar */
    NULL
};

/* Where in the yomi table the letters and digits are, and where the
   half-width kana are. */
#define YOMI_ASCII      0x229    /* two bytes each, from 0x21 */
#define YOMI_HALFKANA   0x2e7    /* two bytes each, from 0xa1 */
#define YOMI_CHOON      0x224
#define YOMI_SOKUON     0xfd

/* What the caller writes to say where the accent is. */
#define ACCENT_MARK     '^'

/* How long a reading and a key may be. The first is what makeUserDictData
   refuses beyond, the second what updateDictExt refuses beyond, and the
   third is the walk's own stopping point. */
#define KANA_MOST       0x33
#define KEY_MOST        0x20
#define YOMI_MOST       0x19

/* The whole of a UserDictData as bytes, which is how the reading is written
   into it. IBM's walk can put its last byte and its terminator past the kana
   and into the two fields after it; see transKatakana2Yomi. */
#define UD_BYTES(d)     ((uint8_t *)(d))
#define UD_KANA_AT      0x02

/* ---- making one ------------------------------------------------------ */

RomUserDict *rud_ctor(RomUserDict *u, void *analysis)
{
    u->analysis = analysis;
    u->input = *(void **)((uint8_t *)analysis + TA_INPUTCHAR_AT);
    u->search = *(void **)((uint8_t *)analysis + TA_DICTSEARCH_AT);
    u->dict = NULL;
    return u;
}

/* ---- the key --------------------------------------------------------- */

/* Normalise what the caller wrote into the form the dictionary is keyed by.
 *
 * Two-byte characters go through unchanged; a letter, digit or symbol becomes
 * the two-byte form the yomi table holds for it; half-width kana become their
 * two-byte forms, with a following voicing mark folded into the character it
 * marks. Anything else is dropped.
 *
 * Answers nought for a key with white space in it or one containing the
 * full-width space, since neither can be part of a word. */
int32_t rud_makeKey(RomUserDict *u, uint8_t *in, int32_t n, char *out,
                   int32_t *outLen)
{
    int32_t o = 0;
    int32_t i;

    (void)u;
    for (i = 0; i < n; i++) {
        if (in[i] == '\n' || in[i] == '\r' || in[i] == ' ' || in[i] == '\t')
            return 0;

        if (ju_IsDBCSLeadByte((char)in[i])
            && ju_IsDBCSTrailByte(in[i + 1])) {
            if (ju_TwoChCmp((char)in[i], (char)in[i + 1], (char *)IDEO_SPACE))
                return 0;
            out[o++] = (char)in[i];
            out[o++] = (char)in[i + 1];
            i++;
            continue;
        }

        if (ju_IsAlphaNumSym((char)in[i])) {
            const uint8_t *p = dm_GetYomiDataPtr() + YOMI_ASCII
                               + (uint8_t)(in[i] - 0x21) * 2;

            out[o++] = (char)p[0];
            out[o++] = (char)p[1];
            continue;
        }

        if (!ju_IsSBCSKana((char)in[i]))
            continue;

        if (in[i + 1] == 0xde || in[i + 1] == 0xdf)
            ju_ConvertDakuten(&out[o], in[i], in[i + 1]);
        else if (in[i] == 0xa4)
            ju_DbCpy(&out[o], IDEO_COMMA);
        else if (in[i] == 0xa1)
            ju_DbCpy(&out[o], IDEO_STOP);
        else
            ju_DbCpy(&out[o],
                     (const char *)(dm_GetYomiDataPtr() + YOMI_HALFKANA
                                    + (uint8_t)(in[i] - 0xa1) * 2));
        o += 2;
    }

    out[o] = 0;
    *outLen = o;
    return 1;
}

/* ---- the reading ----------------------------------------------------- */

/* Copy the kana out and work out which mora the caret marks.
 *
 * The caret is not part of the reading, so it is dropped and what is counted
 * is how many moras came before it. A small kana joins the one in front of it
 * and is not a mora of its own; a doubled consonant or a lengthened vowel is a
 * mora but cannot carry an accent, so a caret after one belongs to the mora
 * before that.
 *
 * Answers nought when the reading is longer than there is room for, or when
 * it holds something that is neither kana nor the caret. */
int32_t rud_makeTransValue(RomUserDict *u, const char *in, uint8_t *accent,
                          char *out, int16_t room)
{
    const char *p = in;
    int32_t     moras = 0;
    int16_t     o = 0;

    (void)u;
    while (*p != 0) {
        if (o > room)
            return 0;

        if (ju_IsHiragana(p) || ju_IsKatakana(p) || ju_IsLongVowel(p)) {
            int32_t own = 1;

            if (moras > 0) {
                int i;

                for (i = 0; NOT_A_MORA[i] != NULL; i++)
                    if (ju_DbCmp(p, NOT_A_MORA[i])) {
                        own = 0;
                        break;
                    }
            }
            if (own)
                moras++;
            out[o++] = *p++;
            out[o++] = *p++;
            continue;
        }

        if (*p != ACCENT_MARK)
            return 0;

        {
            int32_t back = 0;
            int     i;

            if (moras > 0)
                for (i = 0; NOT_ACCENTED[i] != NULL; i++)
                    if (ju_DbCmp(p - 2, NOT_ACCENTED[i])) {
                        back = 1;
                        break;
                    }
            *accent = (uint8_t)(back ? moras - 1 : moras);
        }
        p++;
    }

    out[o] = 0;
    return 1;
}

/* Spell a katakana reading out as the engine's yomi codes.
 *
 * This is DictSearch::ProcessKatakana's inner walk done again over a string
 * rather than over the input reader, and it has to stay that way: a candidate
 * entry the user dictionary writes has to look like one the built-in
 * dictionary wrote, or the path search above will weigh them differently.
 *
 * A caution. The walk stops at twenty-five codes but a single step may write
 * two, and the lengthening after it one more, so twenty-six can come out --
 * and the caller's buffer is the twenty-five bytes of UserDictData's kana.
 * The twenty-sixth code therefore lands on the accent byte and the terminator
 * on the first attribute byte. IBM's own arithmetic, and harmless only
 * because makeUserDictData writes the attributes afterwards; the accent it
 * does not, so a reading that long loses it. Reproduced rather than fixed,
 * and it takes a fifty-byte kana string to reach. */
uint8_t rud_transKatakana2Yomi(RomUserDict *u, char *kana, uint8_t *out)
{
    const uint8_t *yomi = dm_GetYomiDataPtr();
    int16_t        i = 0;
    int16_t        n = 0;

    for (; kana[i] != 0; i = (int16_t)(i + 2)) {
        int16_t y0;
        int16_t y1;
        uint8_t c;

        if (n >= YOMI_MOST)
            break;

        c = (uint8_t)((int8_t)kana[i + 1] - 0x40);
        y0 = ds_GetYoonIndex(u->search, &kana[i]);
        y1 = ds_GetYoonIndex(u->search, &kana[i + 2]);

        if (y0 < 0 && y1 >= 0xa && y1 <= 0x11) {
            uint8_t flag = (n > 0 && out[n - 1] % 8 == 2) ? 1 : 0;
            int16_t v = ds_ConvertYoonDict(u->search, (int16_t)c, y1, flag);

            if (v < 0) {
                out[n++] = yomi[c];
                i = (int16_t)(i + 2);
                c = (uint8_t)((int8_t)kana[i + 1] - 0x40);
                out[n++] = yomi[c];
            } else {
                out[n++] = (uint8_t)v;
                i = (int16_t)(i + 2);
            }
        } else if (y0 == 0x12) {
            out[n++] = y1 < 0 ? yomi[c] : YOMI_SOKUON;
        } else {
            out[n++] = yomi[c];
        }

        y1 = ds_GetYoonIndex(u->search, &kana[i + 2]);
        if (y1 == 8 && n < YOMI_MOST + 1) {
            int16_t col = (int16_t)(out[n - 1] % 8);

            if (col > 4)
                col = 0;
            out[n++] = yomi[YOMI_CHOON + col];
            i = (int16_t)(i + 2);
        }
    }

    out[n] = 0;
    return (uint8_t)n;
}

/* The same for a reading that may be in either script: put it all into
   katakana first and walk that. */
uint8_t rud_transKana2Yomi(RomUserDict *u, char *kana, uint8_t *out)
{
    size_t   room = strlen(kana) + 1;
    char    *buf = (char *)cpp_new((uint32_t)room);
    uint8_t  n;

    ju_Hiragana2Katakana((const uint8_t *)kana, (uint8_t *)buf);
    n = rud_transKatakana2Yomi(u, buf, out);
    cpp_delete(buf);
    return n;
}

/* ---- one stored entry ------------------------------------------------ */

/* Turn a caller's reading and part of speech into the record that is stored.
 *
 * The key's length in bytes is twice its length in characters, which is what
 * the first field holds. Answers nought for a reading too long to store or a
 * part of speech that is not one of the four. */
int32_t rud_makeUserDictData(RomUserDict *u, UserDictData *d,
                            uint8_t keyLen, char *kana, int32_t pos)
{
    char tmp[KANA_MOST + 8];

    d->accent = 0;
    if (strlen(kana) > KANA_MOST)
        return 0;
    if (!rud_makeTransValue(u, kana, &d->accent, tmp, KANA_MOST))
        return 0;

    d->kanaLen = rud_transKana2Yomi(u, tmp, UD_BYTES(d) + UD_KANA_AT);
    d->chars = (uint8_t)(keyLen / 2);

    if (pos < 0 || pos > 3)
        return 0;

    /* IBM writes the first of these once before the switch as well, out of a
       fallthrough the compiler kept; nothing can read it in between. */
    d->pos = jajp_s_anUserDictData[pos * 3];
    d->attr = jajp_s_anUserDictData[pos * 3 + 1];
    d->attr2 = jajp_s_anUserDictData[pos * 3 + 2];
    return 1;
}

/* Put one stored entry into DictSearch's candidate array, at `slot', as a
   word covering the text from `at'. Everything a candidate entry holds is
   written, so that what the user taught looks to the path search exactly like
   what the built-in dictionary found. */
int32_t rud_writeData(RomUserDict *u, UserDictData *d, int16_t slot,
                     int16_t at)
{
    uint8_t *e;
    int16_t  j;

    if (slot > DS_ENTRY_N)
        return 0;
    e = DS_ENTRY_AT(u->search, slot);

    DE_B(e, DE_CHARS) = d->chars;
    DE_W(e, DE_ACCENT) = (int16_t)(uint16_t)d->accent;
    DE_B(e, DE_KANALEN) = d->kanaLen;

    /* A long reading goes to the owner's store, and there is nowhere to put
       it once that is full. */
    if ((int8_t)*(uint8_t *)((char *)u->analysis + TA_LONGWORDS)
        >= TA_LONGWORD_N && DE_B(e, DE_KANALEN) > 9)
        return 0;

    if (DE_B(e, DE_KANALEN) > 9)
        ds_SetLongWord(u->search, (int16_t)DE_B(e, DE_KANALEN), e,
                       UD_BYTES(d) + UD_KANA_AT);
    else
        for (j = 0; j < d->kanaLen; j++)
            DE_B(e, DE_KANA + j) = UD_BYTES(d)[UD_KANA_AT + j];

    DE_B(e, DE_POS) = d->pos;
    for (j = 0; j < 2; j++)
        DE_B(e, DE_ATTR + j) = UD_BYTES(d)[0x1c + j];

    DE_W(e, DE_AT) = at;
    DE_L(e, DE_MARK) = IC_MARK_AT(u->input, at);
    DE_W(e, DE_OFFSET) = IC_OFFSET_AT(u->input, at);
    DE_L(e, DE_COST) = 8;
    return 1;
}

/* ---- what the engine calls ------------------------------------------- */

/* Every user word that starts where the analysis has got to.
 *
 * The whole of what is left of the sentence is one key, and multiSearch
 * answers with what matched each prefix of it at once -- so a one-character
 * word and a five-character one starting at the same place both come back,
 * each in the slot for its own length. Every one of them becomes a candidate
 * entry.
 *
 * When DictSearch is in its second mode there is a context to satisfy as
 * well. That context is a _SNLK_TABLE -- a reading the caller gave for this
 * very stretch of text, which rom/jajp/inputchar.h is the record of -- and
 * what has to match is how many characters its written form has, how many
 * yomi codes its reading is, and the written form itself. Answers how many
 * entries were written, or minus one when there was no room to ask. */
int16_t rud_lookup(RomUserDict *u, uint8_t *text, int16_t at, int16_t slot)
{
    Key          key;
    Translation *found;
    uint8_t     *context = *(uint8_t **)DS_AT(u->search, DS_USERDICT_WORD);
    int16_t      n = IC_COUNT_AT(u->input);
    int16_t      count = 0;
    int32_t      i;

    key_ctor(&key, (char *)text, n * 2);
    if (key.bytes == NULL) {
        key_dtor(&key);
        return -1;
    }

    found = sl_multiSearch(u->dict, &key);
    if (found == NULL) {
        key_dtor(&key);
        return -1;
    }

    for (i = 0; i < (uint16_t)n; i++) {
        if (found[i].valueLen <= 0)
            continue;

        if (DS_L(u->search, DS_USERDICT_MODE) == 1) {
            uint8_t *value = (uint8_t *)found[i].value;

            if (value[0] != context[SN_CHARS]
                || value[1] != context[SN_YOMI_N])
                continue;
            if (strcmp(found[i].word, *(char **)(context + SN_KEY_AT)) != 0)
                continue;
        }
        if (rud_writeData(u, (UserDictData *)found[i].value, slot, at)) {
            count++;
            slot++;
        }
    }

    sl_freeMultiSearch(found);
    key_dtor(&key);
    return count;
}

/* Add a word to a dictionary, or take one out when no reading is given.
 *
 * A deliberate difference, and the ninth in the tree. The written form may be
 * up to thirty-two bytes and every half-width kana in it becomes two, so the
 * key can reach sixty-four; IBM's own buffer for it is about thirty-six bytes
 * of its stack, and a word of twenty half-width kana walks off the end of its
 * frame -- measured, at exactly twenty, with Wine's debugger coming up.
 * Ours is sized for the bound the function itself enforces. */
int32_t rud_updateDictExt(RomUserDict *u, SkipList *list, int32_t which,
                         uint8_t *word, int32_t wordLen, char *kana,
                         int32_t kanaLen, int32_t pos)
{
    char    keyBuf[KEY_MOST * 2 + 8];
    int32_t keyLen = 0;
    int32_t rc = ECI_DICT_ERROR;
    Key     key;

    (void)which;
    (void)kanaLen;
    if (wordLen > KEY_MOST)
        return ECI_DICT_ERROR;
    if (!rud_makeKey(u, word, wordLen, keyBuf, &keyLen))
        return ECI_DICT_INVALID_ENTRY;

    key_ctor(&key, keyBuf, keyLen);
    if (key.bytes == NULL) {
        key_dtor(&key);
        return ECI_DICT_NO_MEMORY;
    }

    if (kana != NULL) {
        UserDictData *d = (UserDictData *)malloc(sizeof(UserDictData));

        if (rud_makeUserDictData(u, d, (uint8_t)keyLen, kana, pos)) {
            Translation t;

            tr_ctor(&t, (const char *)d, (int32_t)sizeof(UserDictData),
                    keyBuf, kana, pos);
            if (t.ok == 0) {
                tr_dtor(&t);
                key_dtor(&key);
                free(d);
                return ECI_DICT_NO_MEMORY;
            }
            if (sl_insert(list, &key, &t) < 0) {
                tr_dtor(&t);
                key_dtor(&key);
                free(d);
                return ECI_DICT_NO_MEMORY;
            }
            rc = 0;
            tr_dtor(&t);
        } else {
            rc = ECI_DICT_INVALID_ENTRY;
        }
        free(d);
    } else {
        sl_remove(list, &key);
        rc = 0;
    }

    key_dtor(&key);
    return rc;
}

/* And read one back: what the caller stored, its length, and its part of
   speech. */
int32_t rud_lookupDictExt(RomUserDict *u, SkipList *list, int32_t which,
                         uint8_t *word, int32_t wordLen, void **value,
                         int32_t *valueLen, int32_t *pos)
{
    char         keyBuf[KEY_MOST * 2 + 8];
    int32_t      keyLen = 0;
    int32_t      rc;
    Key          key;
    Translation *found;

    (void)which;
    if (!rud_makeKey(u, word, wordLen, keyBuf, &keyLen))
        return ECI_DICT_INVALID_ENTRY;

    key_ctor(&key, keyBuf, keyLen);
    if (key.bytes == NULL) {
        key_dtor(&key);
        return ECI_DICT_NO_MEMORY;
    }

    found = sl_search(list, &key);
    if (found != NULL) {
        rc = 0;
        *value = found->extra;
        *valueLen = found->extraLen;
        *pos = found->pos;
    } else {
        rc = ECI_DICT_NO_ENTRY;
        *value = NULL;
        *valueLen = 0;
        *pos = 0;
    }

    key_dtor(&key);
    return rc;
}
