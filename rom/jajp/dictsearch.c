/* The dictionary search, as far as it is written.
 *
 * This is the class the rest of the analyser leans on: it turns a stretch of
 * the input into candidate words with readings, and everything above it
 * chooses between what it produced. IBM spreads it over seven objects --
 * dictsearch, dictapi, fdictapi, kanastr, engread, numanal and phrasetable --
 * and sixty-two methods; forty-six of them are here.
 *
 * The count is worth stating carefully, because it was wrong for three
 * commits. Reading only the first four objects gives sixty-four symbols, but
 * two of those are one method compiled into two objects as a COMDAT, and
 * three whole objects were missing: engread's four string-rule methods,
 * numanal's eight number ones, and phrasetable's copy of IsOnin. So the
 * closure of anything that reaches those looked smaller than it is, and
 * tools/rom-offsets.py was checking this record against four objects while
 * saying it checked the class. Both are put right. The map itself was never
 * wrong: none of the three missing objects touches a DictSearch field, which
 * is why the tiling held all along.
 *
 * The layout is IBM's rather than ours, which is a departure from the other
 * files in this directory. Two reasons, and both are about being able to prove
 * a piece at a time. A class this size arrives half-written for a long while,
 * and a half-written one has to work with state built by hand; keeping IBM's
 * offsets means test/romprims.c can build that state the same way on both
 * sides and compare, instead of maintaining two descriptions of the same
 * bytes. And the record is only partly understood -- rom/jajp/dictsearch.h
 * says which parts -- so a tidy struct would have to invent names for fields
 * nobody has read yet.
 *
 * What the whole thing does, in one paragraph. GenerateWord takes a position
 * in the sentence, copies the run of text that starts there into a buffer, and
 * asks GenerateKanaString to turn it into readings. That walk goes character by
 * character: katakana and hiragana it spells out itself, through the two
 * Process methods and the yomi table; a kanji it looks up in the kana
 * dictionary, which may answer with several readings at once, and every
 * reading already being built is duplicated so that each can carry each
 * answer. What comes out is up to thirty candidate readings for the same run of
 * text. Then, for every candidate the kanji dictionary did not itself produce,
 * SearchTankanTable looks the reading up in the single-kanji table and writes
 * whatever words it finds into the entry array, which is what the path search
 * above will choose between.
 *
 * Everything here is held to IBM's own answer by test/romprims.sh.
 */

#include <string.h>
#include "jprom.h"
#include "dictsearch.h"
#include "txtanal.h"

/* Reaching a field. The block is bytes and these say how to read one, at the
   offsets rom/jajp/dictsearch.h works out. */
#define DS_AT(d, off)      ((uint8_t *)(d) + (off))
#define DS_B(d, off)       (*(uint8_t *)DS_AT(d, off))
#define DS_W(d, off)       (*(int16_t *)DS_AT(d, off))
#define DS_L(d, off)       (*(int32_t *)DS_AT(d, off))
#define DS_P(d, off)       (*(void **)DS_AT(d, off))

#define DS_INPUT(d)        ((uint8_t *)DS_P(d, DS_INPUTCHAR))
#define DS_OWNER_OF(d)     ((uint8_t *)DS_P(d, DS_OWNER_AT))

/* The four arrays with one slot per candidate, the readings themselves, and
   the readings of the one kanji being looked up. */
#define DS_MARK_AT(d, i)   (*(uint8_t *)DS_AT(d, DS_MARK + (i)))
#define DS_CHARS_AT(d, i)  (*(int16_t *)DS_AT(d, DS_CHARS + (i) * 2))
#define DS_LEN_AT(d, i)    (*(int16_t *)DS_AT(d, DS_LEN + (i) * 2))
#define DS_TAKEN_AT(d, i)  (*(int16_t *)DS_AT(d, DS_TAKEN + (i) * 2))
#define DS_READ_AT(d, i)   DS_AT(d, DS_READING + (i) * DS_READING_SIZE)
#define DS_KANA_AT(d, i)   DS_AT(d, DS_KANA + (i) * DS_KANA_SIZE)
#define DS_KANA_CHARS_AT(d, i) (*(uint8_t *)DS_AT(d, DS_KANA_CHARS + (i)))
#define DS_KANA_LEN_AT(d, i)   (*(uint8_t *)DS_AT(d, DS_KANA_LEN + (i)))
#define DS_ENTRY_AT(d, i)  DS_AT(d, DS_ENTRY + (i) * DS_ENTRY_SIZE)

/* And into a candidate entry, which is the same thirty-two bytes whether it
   lives in the array or on the caller's stack. */
#define DE_B(e, off)       (*((uint8_t *)(e) + (off)))
#define DE_W(e, off)       (*(int16_t *)((uint8_t *)(e) + (off)))
#define DE_U(e, off)       (*(uint16_t *)((uint8_t *)(e) + (off)))
#define DE_L(e, off)       (*(int32_t *)((uint8_t *)(e) + (off)))

/* And into the input reader, whose own file will name these properly. */
#define IC_CHAR(in, i)     ((char *)((in) + IC_TEXT + (i) * 2))
#define IC_KIND_AT(in, i)  (*(int32_t *)((in) + IC_KIND + (i) * 4))
#define IC_OFFSET_AT(in, i) (*(int16_t *)((in) + IC_OFFSET + (i) * 2))
#define IC_MARK_AT(in, i)  (*(int32_t *)((in) + IC_MARK + (i) * 4))
#define IC_COUNT_AT(in)    (*(int16_t *)((in) + IC_COUNT))
#define IC_LENGTH_AT(in)   (*(int16_t *)((in) + IC_LENGTH))

/* The particle wo, which marks the object of a verb. That is what the name
   means -- a case marker in the grammatical sense, not a typographic one -- and
   a run of text stops at it because what follows it is a new word.
 *
 * Read out of the object rather than decoded from the name it is stored
 * under: 0x82f0 is wo, and the mangled form `?$IC?p' reads as 0x8270 to
 * anybody working the encoding out by hand, which is a different character
 * altogether. test/romprims.sh is what caught that. */
static const char CASE_MARKER[] = "\x82\xf0";

/* And the one character that is neither a letter nor a kana but belongs to an
   English word all the same. It sits in the Roman-numeral range, which is why
   the walk over letters has to name it rather than test a class. */
static const char ENG_ROMAN_MARK[] = "\xfa\x56";

/* The small kana, which are the ones that do not stand alone: a small vowel or
   a small ya, yu or yo joins the sound in front of it, and a small tsu doubles
   the consonant after it. Their order is the whole of the interface -- the code
   that reads an index out of this table then tests it against a number -- so it
   is IBM's order and not a tidier one.
 *
 * Nought to nine are hiragana and ten to eighteen katakana, and the katakana
 * half is one short because the long-vowel bar at index eight belongs to
 * neither script and is not repeated. That off-by-one is visible in
 * ConvertYoonDict, which folds a katakana index onto a hiragana one by taking
 * ten off it: the small katakana tsu at eighteen lands on the bar rather than
 * on the small hiragana tsu. Faithful, and left alone. */
static const char *const YOON[] = {
    "\x82\x9f", "\x82\xa1", "\x82\xa3", "\x82\xa5", "\x82\xa7",  /* small aiueo */
    "\x82\xe1", "\x82\xe3", "\x82\xe5",                          /* ya yu yo */
    "\x81\x5b",                                                  /* the bar */
    "\x82\xc1",                                                  /* small tsu */
    "\x83\x40", "\x83\x42", "\x83\x44", "\x83\x46", "\x83\x48",
    "\x83\x83", "\x83\x85", "\x83\x87",
    "\x83\x62",
    NULL
};

/* How many characters one lookup may take. */
#define TEXT_MOST       5

/* What a candidate entry can hold itself before the reading has to go into
   the owner's long-word store. */
#define KANA_INLINE     9

/* The second byte of the first hiragana and of the first katakana, which is
   what a kana's yomi index is measured from. */
#define HIRAGANA_BASE   0x9f
#define KATAKANA_BASE   0x40

/* Where the yomi table keeps the doubled vowels the long-vowel bar becomes,
   and where it keeps the name of each Roman letter, six bytes to a row. */
#define YOMI_CHOON      0x224
#define YOMI_LETTER     0x58

/* The code that stands for a doubled consonant, which is what a small tsu
   becomes when there is nothing for it to double. */
#define YOMI_SOKUON     0xfd

/* ---- the leaves ------------------------------------------------------ */

/* Whether a code stands for a sound rather than for a kana.
 *
 * The codes are read as a row and a column, over eight and the remainder.
 * Everything in row 0x1e is one -- that is the row CheckCnvChoon writes a
 * doubled vowel into -- and so are two of row 0x1f. */
int32_t ds_IsOnin(uint8_t code)
{
    if (code / 8 == 0x1e)
        return 1;
    if (code / 8 != 0x1f)
        return 0;
    return (code % 8 == 5 || code % 8 == 6) ? 1 : 0;
}

/* Which small kana this is, or minus one for anything else. */
int16_t ds_GetYoonIndex(void *d, char *s)
{
    int16_t i;

    (void)d;
    for (i = 0; YOON[i] != NULL; i++)
        if (ju_DbCmp(s, YOON[i]))
            return i;
    return -1;
}

/* Put a reading too long for the entry into the owner's store.
 *
 * This is TextAnalysis's method and lives here because it lives in IBM's
 * dictsearch object; the store itself is mapped in txtanal.h. */
void ta_AddLongWord(void *t, uint8_t *word, int16_t n)
{
    uint8_t *at = DS_AT(t, TA_LONGWORD)
                  + DS_B(t, TA_LONGWORDS) * TA_LONGWORD_SIZE;
    int8_t   i;

    for (i = 0; i < n; i++)
        at[i] = word[i];
    DS_B(t, TA_LONGWORDS)++;
}

/* Hand a reading to the owner and record in the entry which one it is.
 *
 * The store holds thirty and nothing checks a second time, so a reading
 * arriving when it is full is dropped and the entry keeps whatever number was
 * in it. */
void ds_SetLongWord(void *d, int16_t n, void *e, uint8_t *word)
{
    uint8_t *owner = DS_OWNER_OF(d);

    if ((int8_t)DS_B(owner, TA_LONGWORDS) >= TA_LONGWORD_N)
        return;
    DE_B(e, DE_KANA) = DS_B(owner, TA_LONGWORDS);
    ta_AddLongWord(owner, word, n);
}

/* How many of the next n characters are hiragana, counting from where the
   lookup began. */
int32_t ds_CountHrgn(void *d, int32_t n)
{
    uint8_t *in = DS_INPUT(d);
    int32_t  at;
    int32_t  count = 0;

    for (at = DS_W(d, DS_FROM); at < DS_W(d, DS_FROM) + n; at++)
        if (IC_KIND_AT(in, (int16_t)at) == KIND_HIRAGANA)
            count++;
    return count;
}

/* Where in the word dictionary one entry is, or nothing if it is past the end
   of its page. Two dictionaries, chosen by the same flag that runs through
   the whole lookup: one is the words proper and the other the single kanji. */
const uint8_t *ds_ReadGWDict(void *d, int16_t page, int16_t at, int16_t which)
{
    const uint8_t *base;
    int16_t        room;

    (void)d;
    if (which == 1) {
        room = 0x1000;
        base = jajp_s_apszNormal[(uint16_t)page];
    } else {
        room = 0xc8;
        base = jajp_s_apszTankan[(uint16_t)page];
    }
    if (at >= room)
        return NULL;
    return base + at;
}

/* ---- the three that were written first ------------------------------- */

/* Whether the character at `at' is the case marker. */
int32_t ds_CheckCaseMarker(void *d, int16_t at)
{
    return ju_DbCmp(IC_CHAR(DS_INPUT(d), at), CASE_MARKER) ? 1 : 0;
}

/* A long-vowel mark after a vowel becomes the vowel doubled.
 *
 * The codes are read as a row and a column -- over eight and the remainder --
 * and a mark is row 0x1f. It doubles when the two are in the same column, and
 * for two pairs of columns that sound the same: three after one, and four
 * after two. Nothing happens when what came before is itself row 0x1e. */
void ds_CheckCnvChoon(void *d, uint8_t code, uint8_t *next)
{
    int16_t before;
    int16_t mark;

    (void)d;
    if (code / 8 == 0x1e)
        return;
    if (*next / 8 != 0x1f)
        return;

    mark = (int16_t)(*next % 8);
    before = (int16_t)(code % 8);

    if (before == mark) {
        *next = (uint8_t)(mark + 0xf0);
        return;
    }
    if ((before == 3 && mark == 1) || (before == 4 && mark == 2))
        *next = (uint8_t)(before + 0xf0);
}

/* Copy the run of text starting at `from' into the lookup buffer.
 *
 * It takes hiragana and katakana freely and at most one kanji, stops at
 * anything else, and never takes more than five characters. That shape is the
 * ordinary Japanese word: a kanji with its okurigana trailing off it. A case
 * marker ends the run -- and if the very first character is one, there is
 * nothing to look up and it answers nought.
 *
 * Answers one when there is something worth looking up, which means at least
 * two characters, and writes where the run began and ended. */
int32_t ds_GetTextBuf(void *d, int16_t from)
{
    uint8_t *in = DS_INPUT(d);
    int16_t  at = from;
    int16_t  n = 0;
    int16_t  kanji = 0;

    for (; at < IC_COUNT_AT(in) && n < TEXT_MOST; at++) {
        int32_t kind = IC_KIND_AT(in, at);

        if (!((kind == KIND_KANJI && kanji == 0)
              || kind == KIND_HIRAGANA
              || kind == KIND_KATAKANA))
            break;

        if (ju_DbCmp(IC_CHAR(in, at), CASE_MARKER)) {
            if (at == from)
                return 0;
            break;
        }

        ju_DbCpy((char *)DS_AT(d, DS_TEXT + n * 2), IC_CHAR(in, at));
        n++;
        if (IC_KIND_AT(in, at) == KIND_KANJI)
            kanji++;
    }

    DS_W(d, DS_COPIED) = n;
    if (n <= 1)
        return 0;

    DS_B(d, DS_TEXT + n * 2) = 0;
    DS_W(d, DS_FROM) = from;
    DS_W(d, DS_TO) = (int16_t)(from + n);
    return 1;
}

/* ---- spelling a kana out --------------------------------------------- */

/* A kana followed by a small one, as one sound.
 *
 * The pair is looked up in the yomi table, whose rows are the kana that can
 * take a small one after them and whose columns are the small kana; `base' is
 * the first kana's yomi index and picks the row. A katakana index is folded
 * onto its hiragana twin by taking ten off it. Answers minus one where there
 * is no such row, which means the caller has to spell the two out separately.
 *
 * Two rows are worth noticing. 0x05 answers only when the flag is set, which
 * is how the caller says that what came before makes the combination possible;
 * and 0x21 reads the same row as 0x17, which is IBM's own arithmetic and not a
 * copying mistake here. */
int16_t ds_ConvertYoonDict(void *d, int16_t base, int16_t yoon, uint8_t flag)
{
    const uint8_t *yomi = dm_GetYomiDataPtr();
    int32_t        row;

    (void)d;
    if (yoon >= 10)
        yoon = (int16_t)(uint8_t)(yoon - 10);

    switch (base) {
    case 0x05: row = flag == 1 ? 0x214 : -1; break;
    case 0x0c: row = 0x0f4; break;
    case 0x0d: row = 0x104; break;
    case 0x16: row = 0x114; break;
    case 0x17: row = 0x124; break;
    case 0x20: row = 0x134; break;
    case 0x21: row = 0x124; break;
    case 0x23: row = 0x144; break;
    case 0x25: row = 0x154; break;
    case 0x26: row = 0x164; break;
    case 0x27: row = 0x174; break;
    case 0x28: row = 0x184; break;
    case 0x2a: row = 0x194; break;
    case 0x31: row = 0x1a4; break;
    case 0x32: row = 0x1b4; break;
    case 0x33: row = 0x1c4; break;
    case 0x34: row = 0x1d4; break;
    case 0x3e: row = 0x1e4; break;
    case 0x4a: row = 0x1f4; break;
    case 0x54: row = 0x204; break;
    default:   row = -1; break;
    }
    if (row < 0)
        return -1;
    return *(const int16_t *)(yomi + row + yoon * 2);
}

/* Spell a run of hiragana out as one candidate.
 *
 * One character, and whatever joins it: a small kana after it makes one sound
 * if the table has that pair and two sounds if it has not, a small tsu becomes
 * the doubling code unless a kana follows it, in which case it is spelt out as
 * itself, and a long-vowel bar after any of that doubles the vowel just
 * written. The entry
 * then gets how many characters were used and how many bytes of kana came out,
 * and the kana themselves -- inside the entry if they fit, in the owner's
 * store if they do not.
 *
 * A detail worth writing down because it looks like a bug and is not one:
 * IBM's version increments the entry's character count inside the small-kana
 * arm and then writes the real count over it at the end, so that increment can
 * never be seen. It is not reproduced. */
void ds_ProcessHiragana(void *d, int16_t at, void *e)
{
    uint8_t *in = DS_INPUT(d);
    uint8_t  buf[64];
    int16_t  room;
    int16_t  i = at;
    int16_t  n = 0;
    int16_t  y0;
    int16_t  y1;
    uint8_t  c;
    int16_t  j;

    room = (int8_t)DS_B(DS_OWNER_OF(d), TA_LONGWORDS) < TA_LONGWORD_N
           ? 0x18 : 0x8;

    c = (uint8_t)((int8_t)IC_CHAR(in, i)[1] - HIRAGANA_BASE);
    if ((uint8_t)IC_CHAR(in, i)[1] >= 0xde)
        c++;

    y0 = ds_GetYoonIndex(d, IC_CHAR(in, i));
    y1 = ds_GetYoonIndex(d, IC_CHAR(in, i + 1));

    if (y0 < 0 && y1 >= 0 && y1 <= 7) {
        int16_t v = ds_ConvertYoonDict(d, (int16_t)c, y1, 0);

        if (v < 0) {
            buf[n++] = dm_GetYomiDataPtr()[c];
            i++;
            c = (uint8_t)((int8_t)IC_CHAR(in, i)[1] - HIRAGANA_BASE);
            if ((uint8_t)IC_CHAR(in, i)[1] >= 0xde)
                c++;
            buf[n++] = dm_GetYomiDataPtr()[c];
        } else {
            buf[n++] = (uint8_t)v;
            i++;
        }
    } else if (y0 == 9) {
        if (y1 < 0 && (IC_KIND_AT(in, i + 1) == KIND_KATAKANA
                       || IC_KIND_AT(in, i + 1) == KIND_HIRAGANA))
            buf[n++] = dm_GetYomiDataPtr()[c];
        else
            buf[n++] = YOMI_SOKUON;
    } else {
        buf[n++] = dm_GetYomiDataPtr()[c];
    }

    y1 = ds_GetYoonIndex(d, IC_CHAR(in, i + 1));
    if (y1 == 8 && n < room + 1) {
        int16_t col = (int16_t)(buf[n - 1] % 8);

        if (col > 4)
            col = 0;
        buf[n++] = dm_GetYomiDataPtr()[YOMI_CHOON + col];
        i++;
    }
    i++;

    DE_B(e, DE_KANALEN) = (uint8_t)n;
    DE_B(e, DE_CHARS) = (uint8_t)(i - at);
    DE_B(e, DE_HIRAGANA) = DE_B(e, DE_CHARS);
    DE_B(e, DE_POS) = 0x7a;
    DE_W(e, DE_AT) = at;
    DE_L(e, DE_MARK) = IC_MARK_AT(in, at);
    DE_L(e, DE_COST) = 0;
    DE_W(e, DE_OFFSET) = IC_OFFSET_AT(in, at);

    if (DE_B(e, DE_KANALEN) > KANA_INLINE) {
        ds_SetLongWord(d, (int16_t)DE_B(e, DE_KANALEN), e, buf);
    } else {
        for (j = 0; j < DE_B(e, DE_KANALEN); j++)
            DE_B(e, DE_KANA + j) = buf[j];
    }
}

/* Spell a run of katakana out as one candidate.
 *
 * Unlike the hiragana case this takes the whole run at once, because a
 * katakana word is written as one and has no okurigana to end it. The run is
 * katakana, long-vowel bars and middle dots; two bars in a row end it, and so
 * does a trailing dot.
 *
 * The middle dot is what separates the parts of a foreign name, and it is
 * where the accent goes: the position of the last one is remembered and the
 * accent is counted from there. Where there is no dot the accent falls two
 * moras from the end for a word of four or more, on the first for a word of
 * one, and on none at all for a word of two or three -- and wherever it lands,
 * it steps back off a mora that is a sound rather than a kana. */
void ds_ProcessKatakana(void *d, int16_t at, void *e)
{
    uint8_t *in = DS_INPUT(d);
    uint8_t  buf[64];
    int16_t  room;
    int16_t  i;
    int16_t  end;
    int16_t  lastDot = 0;
    int16_t  prev = 0;
    int16_t  mark = 0;
    int16_t  n = 0;
    uint8_t  tail;
    int16_t  j;

    room = (int8_t)DS_B(DS_OWNER_OF(d), TA_LONGWORDS) < TA_LONGWORD_N
           ? 0x18 : 0x8;

    for (i = at; i < IC_COUNT_AT(in); i++) {
        int16_t kind = (int16_t)IC_KIND_AT(in, i);

        if (kind != KIND_KATAKANA && kind != KIND_CHOON
            && kind != KIND_NAKAGURO)
            break;
        if (kind == KIND_NAKAGURO)
            lastDot = i;
        if (kind == KIND_CHOON && prev == KIND_CHOON) {
            i--;
            break;
        }
        prev = kind;
    }
    if (IC_KIND_AT(in, i - 1) == KIND_NAKAGURO)
        i--;
    end = i;

    for (i = at; i < end && n < room; i++) {
        int16_t y0;
        int16_t y1;
        uint8_t c;

        if (IC_KIND_AT(in, i) == KIND_NAKAGURO) {
            if (i == lastDot)
                mark = n;
            continue;
        }

        c = (uint8_t)((int8_t)IC_CHAR(in, i)[1] - KATAKANA_BASE);
        y0 = ds_GetYoonIndex(d, IC_CHAR(in, i));
        y1 = ds_GetYoonIndex(d, IC_CHAR(in, i + 1));

        if (y0 < 0 && y1 >= 0xa && y1 <= 0x11) {
            uint8_t flag = (n > 0 && buf[n - 1] % 8 == 2) ? 1 : 0;
            int16_t v = ds_ConvertYoonDict(d, (int16_t)c, y1, flag);

            if (v < 0) {
                buf[n++] = dm_GetYomiDataPtr()[c];
                i++;
                c = (uint8_t)((int8_t)IC_CHAR(in, i)[1] - KATAKANA_BASE);
                buf[n++] = dm_GetYomiDataPtr()[c];
            } else {
                buf[n++] = (uint8_t)v;
                i++;
            }
        } else if (y0 == 0x12) {
            if (y1 < 0 && (IC_KIND_AT(in, i + 1) == KIND_KATAKANA
                           || IC_KIND_AT(in, i + 1) == KIND_HIRAGANA))
                buf[n++] = dm_GetYomiDataPtr()[c];
            else
                buf[n++] = YOMI_SOKUON;
        } else {
            buf[n++] = dm_GetYomiDataPtr()[c];
        }

        y1 = ds_GetYoonIndex(d, IC_CHAR(in, i + 1));
        if (y1 == 8 && n < room + 1) {
            int16_t col = (int16_t)(buf[n - 1] % 8);

            if (col > 4)
                col = 0;
            buf[n++] = dm_GetYomiDataPtr()[YOMI_CHOON + col];
            i++;
        }
    }

    DE_B(e, DE_KANALEN) = (uint8_t)n;
    DE_B(e, DE_CHARS) = (uint8_t)(i - at);
    DE_L(e, DE_COST) = 0;

    tail = (uint8_t)(DE_B(e, DE_KANALEN) - mark);
    if (mark == 0) {
        if (tail > 3)
            DE_W(e, DE_ACCENT) = (int16_t)(tail - 2);
        else if (tail == 1)
            DE_W(e, DE_ACCENT) = 1;
        else
            DE_W(e, DE_ACCENT) = 0;
    } else {
        if (tail > 2)
            DE_W(e, DE_ACCENT) = (int16_t)(tail - 2);
        else
            DE_W(e, DE_ACCENT) = 1;
    }
    DE_W(e, DE_ACCENT) = (int16_t)(DE_U(e, DE_ACCENT) + mark);
    if (DE_U(e, DE_ACCENT) > 0 && ds_IsOnin(buf[DE_U(e, DE_ACCENT) - 1]))
        DE_W(e, DE_ACCENT)--;

    DE_B(e, DE_POS) = 0;
    DE_B(e, DE_ATTR) = 0x18;
    DE_B(e, DE_ATTR2) = 0;
    DE_B(e, DE_HIRAGANA) = 0;
    DE_W(e, DE_AT) = at;
    DE_L(e, DE_MARK) = IC_MARK_AT(in, at);
    DE_W(e, DE_OFFSET) = IC_OFFSET_AT(in, at);

    if (DE_B(e, DE_KANALEN) > KANA_INLINE) {
        ds_SetLongWord(d, (int16_t)DE_B(e, DE_KANALEN), e, buf);
    } else {
        for (j = 0; j < DE_B(e, DE_KANALEN); j++)
            DE_B(e, DE_KANA + j) = buf[j];
    }
}

/* ---- reading the kana dictionary ------------------------------------- */

/* Copy one node's readings into the slots that hold what a kanji can be read
   as. Five at most, at a base of nought or five, so two calls fill the ten. */
int16_t ds_WriteKanaData(void *d, const uint8_t *head, int16_t chars,
                         int16_t unused, int16_t base)
{
    const uint8_t *p = head + TH_READING;
    int16_t        count = (int16_t)(head[TH_FLAGS] >> 4);
    int16_t        i;

    (void)unused;
    for (i = 0; i < count && i < TEXT_MOST; i++) {
        int16_t len = (int16_t)(p[TR_LEN] & 0xf);
        int16_t j;

        for (j = 0; j < len; j++)
            DS_KANA_AT(d, base + i)[j] = p[TR_KANA + j];
        DS_KANA_LEN_AT(d, base + i) = (uint8_t)len;
        DS_KANA_CHARS_AT(d, base + i) = (uint8_t)chars;
        p += TR_KANA + len;
    }
    return i;
}

/* Every way the kanji at `at' -- and the kanji after it, and after that --
   can be read.
 *
 * Two searches, one after the other. The first is a binary walk over a hash of
 * first characters, which is IBM's own shape: a step that halves each time
 * from 0x100 and a position that starts at 0xff, so the walk covers 511 and
 * the step reaching nought is what ends it. The second is a walk down the
 * trie that starts wherever the first landed, one node per character, taking
 * every node's readings as it goes: so a single kanji and a two-kanji compound
 * beginning with it both answer, and the caller tells them apart by how many
 * characters each reading says it took. */
int16_t ds_LookupKanaDict(void *d, int16_t at)
{
    uint8_t       *in = DS_INPUT(d);
    const uint8_t *p;
    int16_t        i = at;
    int16_t        depth = 1;
    int16_t        total = 0;
    int16_t        step = 0x100;
    int16_t        pos = 0xff;
    uint16_t       key = ju_MakeUshort(IC_CHAR(in, i));

    while (step != 0) {
        uint16_t here;

        step = (int16_t)(step / 2);
        here = ju_MakeUshort((char *)dm_GetKDictHashAt((uint16_t)pos));
        if (key > here) {
            if (step)
                pos = (int16_t)(pos + step);
            else
                pos++;
        } else if (step) {
            pos = (int16_t)(pos - step);
        }
    }
    if (pos >= (int16_t)jajp_s_apszKana_n)
        return 0;

    p = jajp_s_apszKana[(uint16_t)pos];
    for (;;) {
        uint16_t here = (uint16_t)((p[TH_KEY] << 8) + p[TH_KEY + 1]);

        if (key == here) {
            if ((p[TH_FLAGS] & 0xf0) != 0)
                total = (int16_t)(total
                                  + ds_WriteKanaData(d, p, depth, at, total));
            if (p[TH_CHILD] == 0)
                break;
            i++;
            key = ju_MakeUshort(IC_CHAR(in, i));
            depth++;
            p += p[TH_CHILD];
            continue;
        }
        if (key > here) {
            uint16_t skip = (uint16_t)(((p[TH_FLAGS] & 0xf) << 8)
                                       + p[TH_SIBLING]);

            if (skip == 0)
                break;
            p += skip;
            continue;
        }
        break;
    }
    return total;
}

/* ---- turning a run of text into candidate readings ------------------- */

/* Every reading the run in the text buffer can have.
 *
 * The candidates are grown a character at a time. Hiragana and katakana add
 * one sound to every reading being built; a kanji may answer with several, and
 * each existing reading is then copied once per answer so that the product of
 * all the choices is present. The mark array is what keeps a copy made this
 * round from being copied again in the same round.
 *
 * The four per-candidate arrays are cleared thirty bytes each, which is the
 * whole of the first and half of each of the others -- IBM's own memset, kept
 * because a slot past the fifteenth then starts out holding what was there
 * before, and only the count keeps the reads inside what was cleared.
 *
 * Answers nought when the walk finished or gave up, and minus one when a kanji
 * was not in the dictionary at all, which is the caller's signal that this run
 * cannot be read. */
int16_t ds_GenerateKanaString(void *d)
{
    uint8_t *in = DS_INPUT(d);
    int32_t  entRoom[DS_ENTRY_SIZE / 4];
    uint8_t *ent = (uint8_t *)entRoom;
    int16_t  prev = 0;
    int16_t  i;
    int16_t  j;
    int16_t  k;
    int16_t  m;

    DS_W(d, DS_NCAND) = 0;
    DS_W(d, DS_RUNS) = 0;
    memset(DS_AT(d, DS_LEN), 0, DS_CAND_N);
    memset(DS_AT(d, DS_MARK), 0, DS_CAND_N);
    memset(DS_AT(d, DS_CHARS), 0, DS_CAND_N);
    memset(DS_AT(d, DS_TAKEN), 0, DS_CAND_N);
    memset(DS_AT(d, DS_READING), 0, DS_READING_N * DS_READING_SIZE);

    for (i = DS_W(d, DS_FROM); i < DS_W(d, DS_TO); i++) {
        /* Anything that has come exactly this far is available again. */
        for (j = 0; j < DS_W(d, DS_NCAND); j++)
            if (DS_CHARS_AT(d, j) == i - DS_W(d, DS_FROM))
                DS_MARK_AT(d, j) = 0;

        switch (IC_KIND_AT(in, i)) {

        case KIND_KATAKANA:
            /* A katakana run is a word on its own, so it is only ever the
               whole of one and the walk ends with it either way. */
            if (prev != 0)
                return 0;
            ds_ProcessKatakana(d, i, ent);
            k = DS_W(d, DS_NCAND);
            for (j = 0; j < DE_B(ent, DE_KANALEN); j++)
                DS_READ_AT(d, k)[j] = DE_B(ent, DE_KANA + j);
            DS_LEN_AT(d, k) = DE_B(ent, DE_KANALEN);
            DS_CHARS_AT(d, k) = DE_B(ent, DE_CHARS);
            for (j = 1; j < DS_LEN_AT(d, k); j++)
                ds_CheckCnvChoon(d, DS_READ_AT(d, k)[j - 1],
                                 &DS_READ_AT(d, k)[j]);
            i = (int16_t)(i + DE_B(ent, DE_CHARS) - 1);
            DS_W(d, DS_TOTAL) = (int16_t)(DS_W(d, DS_TOTAL)
                                          + DE_B(ent, DE_CHARS));
            DS_TAKEN_AT(d, k) = 1;
            DS_W(d, DS_NCAND)++;
            return 0;

        case KIND_CHOON:
            /* A bar on its own lengthens whatever every candidate ended on. */
            for (j = 0; j < DS_W(d, DS_NCAND); j++) {
                if (DS_LEN_AT(d, j) == 0)
                    continue;
                DS_READ_AT(d, j)[DS_LEN_AT(d, j)] =
                    (uint8_t)(DS_READ_AT(d, j)[DS_LEN_AT(d, j) - 1] % 8 + 0xf0);
                DS_LEN_AT(d, j)++;
            }
            DS_W(d, DS_TOTAL)++;
            prev = KIND_CHOON;
            break;

        case KIND_HIRAGANA:
            if (prev == KIND_KATAKANA)
                return 0;
            ds_ProcessHiragana(d, i, ent);
            if (prev == 0) {
                /* Nothing has been built yet, so there is one candidate and
                   this is the start of it. */
                DS_W(d, DS_NCAND)++;
                for (j = 0; j < DS_W(d, DS_NCAND); j++) {
                    for (m = 0; m < DE_B(ent, DE_KANALEN); m++) {
                        DS_READ_AT(d, j)[DS_LEN_AT(d, j)] =
                            DE_B(ent, DE_KANA + m);
                        DS_LEN_AT(d, j)++;
                    }
                    DS_CHARS_AT(d, j) = (int16_t)(DS_CHARS_AT(d, j)
                                                  + DE_B(ent, DE_CHARS));
                }
            } else {
                int16_t added = 0;

                for (j = 0; j < DS_W(d, DS_NCAND); j++) {
                    int16_t to;

                    if (DS_MARK_AT(d, j))
                        continue;
                    to = (int16_t)(DS_W(d, DS_NCAND) + added);
                    for (m = 0; m < DS_LEN_AT(d, j); m++)
                        DS_READ_AT(d, to)[m] = DS_READ_AT(d, j)[m];
                    DS_LEN_AT(d, to) = DS_LEN_AT(d, j);
                    DS_CHARS_AT(d, to) = DS_CHARS_AT(d, j);
                    ds_CheckCnvChoon(d, DS_READ_AT(d, to)[DS_LEN_AT(d, j) - 1],
                                     (uint8_t *)&DE_B(ent, DE_KANA));
                    for (m = 0; m < DE_B(ent, DE_KANALEN); m++) {
                        DS_READ_AT(d, to)[DS_LEN_AT(d, to)] =
                            DE_B(ent, DE_KANA + m);
                        DS_LEN_AT(d, to)++;
                    }
                    DS_CHARS_AT(d, to) = (int16_t)(DS_CHARS_AT(d, to)
                                                   + DE_B(ent, DE_CHARS));
                    DS_MARK_AT(d, j) = 1;
                    added++;
                }
                DS_W(d, DS_NCAND) = (int16_t)(DS_W(d, DS_NCAND) + added);
            }
            i = (int16_t)(i + DE_B(ent, DE_CHARS) - 1);
            DS_W(d, DS_RUNS)++;
            prev = KIND_HIRAGANA;
            break;

        case KIND_KANJI: {
            int16_t found;

            if (prev == KIND_KATAKANA)
                return 0;
            found = ds_LookupKanaDict(d, i);
            if (found == 0) {
                DS_W(d, DS_NCAND) = 0;
                return -1;
            }
            if (prev == 0) {
                /* The first thing in the run, so each reading is a candidate
                   in its own right. */
                for (j = 0; j < found; j++) {
                    for (k = 0; k < DS_KANA_LEN_AT(d, j); k++)
                        DS_READ_AT(d, j)[k] = DS_KANA_AT(d, j)[k];
                    DS_LEN_AT(d, j) = k;
                    DS_CHARS_AT(d, j) = (int16_t)(DS_CHARS_AT(d, j)
                                                  + DS_KANA_CHARS_AT(d, j));
                    if (DS_KANA_CHARS_AT(d, j) > 1)
                        DS_MARK_AT(d, j) = 1;
                    DS_TAKEN_AT(d, j) = 2;
                }
                DS_W(d, DS_NCAND) = (int16_t)(DS_W(d, DS_NCAND) + found);
            } else {
                int16_t alive = 0;
                int16_t b = 0;
                int16_t r;

                for (j = 0; j < DS_W(d, DS_NCAND); j++)
                    if (!DS_MARK_AT(d, j))
                        alive++;

                /* One copy of every unfinished candidate per reading, laid
                   out reading by reading so that the block for reading r
                   starts at count + alive * r. */
                for (j = 0; j < DS_W(d, DS_NCAND); j++) {
                    if (DS_MARK_AT(d, j))
                        continue;
                    for (r = 0; r < found; r++) {
                        int16_t to = (int16_t)(DS_W(d, DS_NCAND)
                                               + alive * r + b);

                        for (k = 0; k < DS_LEN_AT(d, j); k++)
                            DS_READ_AT(d, to)[k] = DS_READ_AT(d, j)[k];
                        DS_LEN_AT(d, to) = DS_LEN_AT(d, j);
                        DS_CHARS_AT(d, to) = DS_CHARS_AT(d, j);
                    }
                    b++;
                    DS_MARK_AT(d, j) = 1;
                }

                for (r = 0; r < found; r++) {
                    for (j = 0; j < alive; j++) {
                        int16_t to = (int16_t)(DS_W(d, DS_NCAND)
                                               + alive * r + j);

                        for (k = 0; k < DS_KANA_LEN_AT(d, r); k++)
                            DS_READ_AT(d, to)[DS_LEN_AT(d, to) + k] =
                                DS_KANA_AT(d, r)[k];
                        DS_LEN_AT(d, to) = (int16_t)(DS_LEN_AT(d, to) + k);
                        DS_CHARS_AT(d, to) = (int16_t)(DS_CHARS_AT(d, to)
                                                    + DS_KANA_CHARS_AT(d, r));
                        if (DS_KANA_CHARS_AT(d, r) > 1)
                            DS_MARK_AT(d, to) = 1;
                        DS_TAKEN_AT(d, to) = 2;
                    }
                }
                DS_W(d, DS_NCAND) = (int16_t)(DS_W(d, DS_NCAND)
                                              + (int16_t)(alive * found));
            }
            prev = KIND_KANJI;
            break;
        }

        default:
            return 0;
        }
    }
    return 0;
}

/* ---- reading the word dictionary ------------------------------------- */

/* Whether the kanji in the stretch of input are this entry's own.
 *
 * The reading matched, and a reading is shared by many words: the entry spells
 * one of them out in kanji, and this is what says the input is that word and
 * not another with the same sound. Every kanji in the span has to appear
 * somewhere in the entry -- anywhere, not in order, because the span may also
 * hold hiragana the entry does not carry. Answers nought when they all do and
 * minus one at the first that does not. */
int16_t ds_CompareKanji(void *d, const uint8_t *ent, int16_t which)
{
    uint8_t *in = DS_INPUT(d);
    char     kanji[64];
    int16_t  count = (int16_t)(ent[DB_COUNT] >> 4);
    int16_t  i;
    int16_t  end;
    int32_t  ok;

    for (i = 0; i < count; i++) {
        kanji[i * 2] = (char)ent[DB_KANJI + i * 2];
        kanji[i * 2 + 1] = (char)ent[DB_KANJI + i * 2 + 1];
    }
    kanji[i * 2] = 0;

    ok = 1;
    end = (int16_t)(DS_W(d, DS_FROM) + DS_CHARS_AT(d, which));
    for (i = DS_W(d, DS_FROM); i < end; i++) {
        if (IC_KIND_AT(in, i) == KIND_KANJI) {
            int16_t j;

            ok = 0;
            for (j = 0; j < count; j++)
                if (ju_DbCmp(&kanji[j * 2], IC_CHAR(in, i))) {
                    ok = 1;
                    break;
                }
        }
        if (!ok)
            return -1;
    }
    return 0;
}

/* Copy one dictionary word into the candidate entry array.
 *
 * Answers nought, or minus one when the array is full, which is what stops
 * the walk above. */
int16_t ds_WriteGWDict(void *d, const uint8_t *word, int16_t which,
                       int16_t base, int16_t at, int16_t mode)
{
    uint8_t *in = DS_INPUT(d);
    uint8_t *e;
    int16_t  j;

    if (base + DS_W(d, DS_CURSOR) >= DS_ENTRY_N)
        return -1;
    e = DS_ENTRY_AT(d, base + DS_W(d, DS_CURSOR));

    DE_B(e, DE_CHARS) = (uint8_t)DS_CHARS_AT(d, which);
    DE_B(e, DE_HIRAGANA) = (uint8_t)ds_CountHrgn(d, DE_B(e, DE_CHARS));
    DE_W(e, DE_ACCENT) = (int16_t)(word[DW_HEAD] >> 4);
    DE_B(e, DE_KANALEN) = (uint8_t)(word[DW_HEAD] & 0xf);
    DE_B(e, DE_POS) = word[DW_POS];
    DE_W(e, DE_AT) = at;
    DE_L(e, DE_MARK) = IC_MARK_AT(in, at);
    DE_L(e, DE_COST) = mode == 1 ? 5 : 6;
    DE_W(e, DE_OFFSET) = IC_OFFSET_AT(in, at);

    for (j = 0; j < 2; j++)
        DE_B(e, DE_ATTR + j) = word[DW_ATTR + j];

    /* A candidate the kanji dictionary itself produced keeps the word's own
       attributes; anything else is marked as having been guessed at. */
    if (DS_TAKEN_AT(d, which) != 1) {
        DE_B(e, DE_ATTR) = (uint8_t)(DE_B(e, DE_ATTR) & 0xe7);
        DE_B(e, DE_ATTR) = (uint8_t)(DE_B(e, DE_ATTR) | 0x80);
        DE_B(e, DE_ATTR2) = (uint8_t)(DE_B(e, DE_ATTR2) | 0x41);
    }

    for (j = 0; j < DE_B(e, DE_KANALEN) && j < KANA_INLINE; j++)
        DE_B(e, DE_KANA + j) = word[DW_KANA + j];

    DS_W(d, DS_CURSOR)++;
    return 0;
}

/* Every word hanging off one node of the kanji trie.
 *
 * In the word-dictionary mode the entry's kanji have to be the input's, and
 * running out of entry array or of dictionary page ends the walk; in the
 * single-kanji mode neither test applies. Answers how many entries the node
 * had, whether or not they were all written. */
int16_t ds_WriteDictTableData(void *d, const uint8_t *head, int16_t which,
                              int16_t mode, int16_t at, int16_t base)
{
    const uint8_t *p = head + DH_ENTRY;
    int16_t        count = (int16_t)(head[DH_FLAGS] >> 4);
    int16_t        i;

    for (i = 0; i < count; i++) {
        int16_t        page = (int16_t)(((p[DB_COUNT] & 0xf) << 8)
                                        + p[DB_PAGE]);
        int16_t        off = *(const int16_t *)(p + DB_OFFSET);
        const uint8_t *word;

        /* IBM copies the entry's kanji into a local buffer here and never
           reads it. Not reproduced -- and its buffer is twenty bytes against
           an entry that may hold fifteen characters, so reproducing it would
           mean reproducing a stack overrun as well. */

        if (mode == 1) {
            if (DS_TAKEN_AT(d, which) != 1
                && ds_CompareKanji(d, p, which) != 0)
                goto next;
            word = ds_ReadGWDict(d, page, off, mode);
            if (word == NULL)
                return count;
            if (ds_WriteGWDict(d, word, which, base, at, mode) < 0)
                return count;
        } else {
            word = ds_ReadGWDict(d, page, off, mode);
            if (word != NULL)
                ds_WriteGWDict(d, word, which, base, at, mode);
        }
    next:
        p += DB_KANJI + (p[DB_COUNT] >> 4) * 2;
    }
    return count;
}

/* Walk one candidate's reading down the kanji trie, writing out every word
   whose reading is exactly as long as the candidate's. */
int16_t ds_GetDictEntry(void *d, int16_t which, int16_t at, int16_t base,
                        const uint8_t *head, int16_t mode)
{
    int16_t i = 0;
    int16_t depth = 1;
    int16_t total = 0;
    uint8_t c = DS_READ_AT(d, which)[i];

    for (;;) {
        if (c == head[DH_BYTE]) {
            if ((head[DH_FLAGS] & 0xf0) != 0 && depth == DS_LEN_AT(d, which))
                total = (int16_t)(total
                                  + ds_WriteDictTableData(d, head, which,
                                                          mode, at, base));
            if (head[DH_CHILD] == 0)
                break;
            i++;
            c = DS_READ_AT(d, which)[i];
            depth++;
            head += head[DH_CHILD];
            continue;
        }
        if (c > head[DH_BYTE]) {
            int16_t skip = (int16_t)(((head[DH_FLAGS] & 0xf) << 8)
                                     + head[DH_SIBLING]);

            if (skip == 0)
                break;
            head += skip;
            continue;
        }
        break;
    }
    return total;
}

/* Look one candidate's reading up in the single-kanji table.
 *
 * The same halving walk LookupKanaDict uses, over a hash of the reading's
 * first two bytes rather than of a character. */
int16_t ds_SearchTankanTable(void *d, int16_t which, int16_t at, int16_t base)
{
    int16_t step = 0x100;
    int16_t pos = 0xff;
    int16_t k0 = (int16_t)DS_READ_AT(d, which)[0];

    while (step != 0) {
        int16_t t0;

        step = (int16_t)(step / 2);
        t0 = (int16_t)(dm_GetKTDictHashAt((uint16_t)pos, 0) - 1);
        if (k0 < t0) {
            if (step)
                pos = (int16_t)(pos - step);
        } else if (k0 > t0) {
            if (step)
                pos = (int16_t)(pos + step);
            else
                pos++;
        } else {
            int16_t k1 = (int16_t)DS_READ_AT(d, which)[1];
            int16_t t1 = (int16_t)(dm_GetKTDictHashAt((uint16_t)pos, 1) - 1);

            if (k1 > t1) {
                if (step)
                    pos = (int16_t)(pos + step);
                else
                    pos++;
            } else if (step) {
                pos = (int16_t)(pos - step);
            }
        }
    }
    if (pos >= (int16_t)jajp_s_apszTankanKana_n)
        return 0;
    return ds_GetDictEntry(d, which, at, base,
                           jajp_s_apszTankanKana[(uint16_t)pos], 2);
}

/* Every word the run of text at `at' could be.
 *
 * Copy the run out, work out every way it can be read, and look each reading
 * that the kanji dictionary did not itself produce up in the single-kanji
 * table. Answers how many entries were written. */
int16_t ds_GenerateWord(void *d, int16_t at, int16_t base)
{
    int16_t j;

    DS_W(d, DS_CURSOR) = 0;
    if (!ds_GetTextBuf(d, at))
        return 0;
    ds_GenerateKanaString(d);
    for (j = 0; j < DS_W(d, DS_NCAND); j++)
        if (DS_TAKEN_AT(d, j) < 2)
            ds_SearchTankanTable(d, j, at, base);
    return DS_W(d, DS_CURSOR);
}

/* ---- reading the dictionaries proper --------------------------------- */

/* Everything below this line is dictapi.obj: the five dictionaries a stretch
 * of text is looked up in and the three writers that put what they find into
 * the candidate array. They share a shape. Each walks a hash to find where in
 * its dictionary to start, then walks a trie or a block of records from
 * there, and hands every match to a writer that fills in a candidate entry.
 * What differs is the dictionary: the word dictionary is a trie over whole
 * characters, the single-kanji one another, the supplement and English ones
 * are flat blocks of self-delimiting records.
 *
 * The user-dictionary context runs through all of them. When
 * DS_USERDICT_MODE is one, the analysis is not reading the sentence but
 * checking one particular word, and every match has to agree with what
 * DS_USERDICT_WORD names before it is taken. */

/* Reaching the context, whose own record is not mapped past two fields. */
#define DS_CONTEXT(d)   (*(uint8_t **)DS_AT(d, DS_USERDICT_WORD))
#define DS_INCONTEXT(d) (DS_L(d, DS_USERDICT_MODE) == 1)

/* Whether a kanji is one of the variant forms the itaiji table covers. */
int32_t ds_IsItaiji(void *d, uint16_t code)
{
    (void)d;
    if (code < ju_MakeUshort((const char *)dm_GetItaijiAt(0, 0)))
        return 0;
    if (code > ju_MakeUshort((const char *)dm_GetItaijiAt(0, 0x3d2)))
        return 0;
    return 1;
}

/* The standard form of a variant kanji, or the kanji itself.
 *
 * The table is grouped by lead byte: the hash gives, for each of thirty-seven
 * lead bytes, which byte it is and how many entries it has, so the search only
 * has to look at one group. Inside the group the entries are in order, so a
 * character smaller than the first or larger than the last is not there. */
uint16_t ds_SwapKanji(void *d, uint16_t code)
{
    int32_t at = 0;
    int32_t base = 0;
    int32_t count = 0;
    int32_t i;

    if (code == 0)
        return code;
    if (!ds_IsItaiji(d, code))
        return code;

    for (at = 0; at < 0x25; at++) {
        uint16_t lead = (uint16_t)((code & 0xff00) >> 8);

        if (lead < dm_GetItaijiHashAt((uint16_t)at, 0))
            return code;
        if (lead == dm_GetItaijiHashAt((uint16_t)at, 0)) {
            count = dm_GetItaijiHashAt((uint16_t)at, 1);
            break;
        }
        base += dm_GetItaijiHashAt((uint16_t)at, 1);
    }
    if (at == 0x25)
        return code;

    if (code < ju_MakeUshort((const char *)dm_GetItaijiAt(0, (uint16_t)base)))
        return code;
    if (code > ju_MakeUshort((const char *)
                             dm_GetItaijiAt(0, (uint16_t)(base + count - 1))))
        return code;

    for (i = base; i < base + count; i++) {
        uint16_t here = ju_MakeUshort((const char *)
                                      dm_GetItaijiAt(0, (uint16_t)i));

        if (code < here)
            return code;
        if (code == here)
            return ju_MakeUshort((const char *)
                                 dm_GetItaijiAt(1, (uint16_t)i));
    }
    return code;
}

/* A placeholder candidate for a character no dictionary knew.
 *
 * The path search has to have something to choose, or a sentence with one
 * unknown kanji in it would have no path at all. A marker of 0xff in the first
 * kana byte is what says the reading is not real. */
int16_t ds_ErrorDummy(void *d, int16_t slot, int16_t at)
{
    uint8_t *in = DS_INPUT(d);
    uint8_t *e;

    if (slot >= DS_ENTRY_N)
        return 0;
    e = DS_ENTRY_AT(d, slot);

    DE_B(e, DE_KANA) = 0xff;
    DE_B(e, DE_KANALEN) = 0;
    DE_B(e, DE_CHARS) = 1;
    DE_W(e, DE_AT) = at;
    DE_B(e, DE_POS) = 0x75;
    DE_L(e, DE_MARK) = IC_MARK_AT(in, at);
    DE_L(e, DE_COST) = 0;
    DE_W(e, DE_OFFSET) = IC_OFFSET_AT(in, at);
    return 1;
}

/* Copy the written form of a match into a buffer, which is what the context
   is compared against. */
static void ds_written(void *d, char *out, int16_t at, int16_t chars)
{
    uint8_t *in = DS_INPUT(d);
    int16_t  i;

    for (i = 0; i < chars; i++) {
        out[i * 2] = IC_CHAR(in, at + i)[0];
        out[i * 2 + 1] = IC_CHAR(in, at + i)[1];
    }
    out[i * 2] = 0;
}

/* Every word hanging off one node of the word dictionary.
 *
 * A word may not end where the character after it would join it -- a small
 * kana joins the sound before it, and a long bar after a katakana or another
 * bar lengthens it -- so those are refused outright before anything is
 * written. Answers how many entries were written, or how far it got when the
 * array filled.
 *
 * A caution kept from IBM: the reading is copied out at its own length, which
 * is a nibble and so may be fifteen, into the ten bytes a candidate holds. A
 * record longer than ten writes over the position and the mark that were put
 * in just above it. Reproduced; whether the shipped dictionary has such a
 * record is what the sweep answers. */
int16_t ds_WriteData(void *d, const uint8_t *head, int16_t chars,
                     int16_t hiragana, int16_t base, int16_t last, int16_t at)
{
    uint8_t       *in = DS_INPUT(d);
    char           want[256];
    const uint8_t *p;
    int16_t        count;
    int16_t        skipped = 0;
    int16_t        i;

    if (last + 1 < IC_COUNT_AT(in)) {
        int16_t y = ds_GetYoonIndex(d, IC_CHAR(in, last + 1));

        if ((y >= 0 && y <= 7) || (y >= 0xa && y <= 0x11))
            return 0;
        if ((IC_KIND_AT(in, last) == KIND_KATAKANA
             || IC_KIND_AT(in, last) == KIND_CHOON)
            && IC_KIND_AT(in, last + 1) == KIND_CHOON)
            return 0;
    }

    if (DS_INCONTEXT(d))
        ds_written(d, want, at, chars);

    p = head;
    if ((head[NH_FLAGS] & 0x80) != 0) {
        count = (int16_t)head[NH_COUNT];
        p = head + NH_WORD;
    } else {
        count = 0;
    }

    for (i = 0; i < count; i++) {
        uint8_t *e;
        int16_t  j;

        if (base + i >= DS_ENTRY_N)
            return i;
        if (DS_INCONTEXT(d)
            && strcmp((const char *)*(char **)(DS_CONTEXT(d) + SN_KEY_AT),
                      want) != 0) {
            p += NW_KANA + (p[NW_HEAD] & 0xf);
            skipped++;
            continue;
        }

        e = DS_ENTRY_AT(d, base + i);
        DE_B(e, DE_CHARS) = (uint8_t)chars;
        DE_B(e, DE_HIRAGANA) = (uint8_t)hiragana;
        DE_W(e, DE_ACCENT) = (int16_t)(p[NW_HEAD] >> 4);
        DE_B(e, DE_KANALEN) = (uint8_t)(p[NW_HEAD] & 0xf);
        DE_B(e, DE_POS) = p[NW_POS];
        DE_W(e, DE_AT) = at;
        DE_L(e, DE_MARK) = IC_MARK_AT(in, at);
        DE_W(e, DE_OFFSET) = IC_OFFSET_AT(in, at);
        for (j = 0; j < 2; j++)
            DE_B(e, DE_ATTR + j) = p[NW_ATTR + j];
        if (DE_B(e, DE_KANALEN) == DE_B(e, DE_HIRAGANA)
            && DE_B(e, DE_KANALEN) <= 3 && DE_B(e, DE_POS) == 0)
            DE_B(e, DE_ATTR2) = (uint8_t)(DE_B(e, DE_ATTR2) | 0x41);
        for (j = 0; j < DE_B(e, DE_KANALEN); j++)
            DE_B(e, DE_KANA + j) = p[NW_KANA + j];
        DE_L(e, DE_COST) = 1;

        p += NW_KANA + DE_B(e, DE_KANALEN);
    }
    return (int16_t)(count - skipped);
}

/* The same for the single-kanji dictionary, whose records carry no part of
   speech of their own worth a flag and whose cost is two rather than one. */
int16_t ds_WriteTankanData(void *d, const uint8_t *head, int16_t chars,
                           int16_t base, int16_t at)
{
    uint8_t       *in = DS_INPUT(d);
    char           want[256];
    const uint8_t *p = head + TH_READING;
    int16_t        count = (int16_t)(head[TH_FLAGS] >> 4);
    int16_t        skipped = 0;
    int16_t        i;

    if (DS_INCONTEXT(d))
        ds_written(d, want, at, chars);

    for (i = 0; i < count; i++) {
        uint8_t *e;
        int16_t  j;

        if (base + i >= DS_ENTRY_N)
            return i;
        if (DS_INCONTEXT(d)
            && strcmp((const char *)*(char **)(DS_CONTEXT(d) + SN_KEY_AT),
                      want) != 0) {
            p += TR_KANA + (p[TR_LEN] & 0xf);
            skipped++;
            continue;
        }

        e = DS_ENTRY_AT(d, base + i);
        DE_B(e, DE_CHARS) = (uint8_t)chars;
        DE_W(e, DE_ACCENT) = (int16_t)(p[TR_LEN] >> 4);
        DE_B(e, DE_KANALEN) = (uint8_t)(p[TR_LEN] & 0xf);
        for (j = 0; j < 2; j++)
            DE_B(e, DE_ATTR + j) = p[2 + j];
        for (j = 0; j < DE_B(e, DE_KANALEN); j++)
            DE_B(e, DE_KANA + j) = p[TR_KANA + j];
        DE_B(e, DE_POS) = p[1];
        DE_W(e, DE_AT) = at;
        DE_L(e, DE_MARK) = IC_MARK_AT(in, at);
        DE_W(e, DE_OFFSET) = IC_OFFSET_AT(in, at);
        DE_L(e, DE_COST) = 2;

        p += TR_KANA + DE_B(e, DE_KANALEN);
    }
    return (int16_t)(count - skipped);
}

/* And for the supplement and English dictionaries, whose record holds one
   whole word rather than a trie node, so there is only ever one entry to
   write and the reading may be long enough to want the owner's store. */
int16_t ds_WriteUserData(void *d, const uint8_t *head, int16_t slot,
                         int16_t at)
{
    uint8_t       *in = DS_INPUT(d);
    char           want[256];
    const uint8_t *kana;
    uint8_t       *e;
    int16_t        i;
    int16_t        j;

    if (slot > DS_ENTRY_N)
        return 0;
    if (DS_INCONTEXT(d)) {
        ds_written(d, want, at, (int16_t)head[UH_CHARS]);
        if (strcmp((const char *)*(char **)(DS_CONTEXT(d) + SN_KEY_AT),
                   want) != 0)
            return 0;
    }

    e = DS_ENTRY_AT(d, slot);
    kana = head + UH_TEXT + head[UH_CHARS] * 2;

    DE_B(e, DE_CHARS) = head[UH_CHARS];
    DE_W(e, DE_ACCENT) = (int16_t)(uint16_t)head[UH_ACCENT];
    DE_B(e, DE_KANALEN) = head[UH_KANALEN];

    if ((int8_t)DS_B(DS_OWNER_OF(d), TA_LONGWORDS) >= TA_LONGWORD_N
        && DE_B(e, DE_KANALEN) > 9)
        return 0;

    if (DE_B(e, DE_KANALEN) > 9) {
        ds_SetLongWord(d, (int16_t)DE_B(e, DE_KANALEN), e, (uint8_t *)kana);
        i = (int16_t)DE_B(e, DE_KANALEN);
    } else {
        for (i = 0; i < head[UH_KANALEN]; i++)
            DE_B(e, DE_KANA + i) = kana[i];
    }

    DE_B(e, DE_POS) = kana[i];
    i++;
    for (j = 0; j < 2; j++, i++)
        DE_B(e, DE_ATTR + j) = kana[i];

    DE_W(e, DE_AT) = at;
    DE_L(e, DE_MARK) = IC_MARK_AT(in, at);
    DE_W(e, DE_OFFSET) = IC_OFFSET_AT(in, at);
    DE_L(e, DE_COST) = 9;
    return 1;
}

/* Every word of the supplement dictionary that starts here.
 *
 * The supplement is flat rather than a trie: blocks of a thousand bytes, each
 * a run of self-delimiting records in order, and an index that says which
 * first character each block starts at. The walk finds the first block that
 * could hold the word and reads forward from there. */
int16_t ds_LookupUserDict(void *d, const uint8_t *dict, char *text,
                          int16_t slot, const uint8_t *index, int16_t at,
                          int16_t unused)
{
    int32_t  incontext = DS_INCONTEXT(d);
    int16_t  written = 0;
    int16_t  ti = 0;
    uint16_t key;
    int16_t  i = 0;
    int16_t  found = 0;
    int16_t  block = 0;
    int16_t  end;
    int16_t  b;
    uint16_t here = 0;

    (void)unused;
    if (slot >= DS_ENTRY_N)
        return 0;

    key = ju_MakeUshort(text);
    do {
        here = ju_MakeUshort((const char *)(index + i * 2));
        if (!found && here >= key) {
            block = i;
            found = 1;
        }
        i++;
    } while (key >= here && i < 0x78);
    if (i == 0x78)
        block = (int16_t)(i - 1);
    end = i;

    for (b = block; b < end; b++) {
        const uint8_t *p = dict + b * 0x3e8;

        while (p[UH_LEN] != 0) {
            int16_t        ti2 = ti;
            const uint8_t *q = p + UH_TEXT;
            uint16_t       recKey = ju_MakeUshort((const char *)q);
            int16_t        n;

            key = ju_MakeUshort(&text[ti2 * 2]);
            if (key != recKey) {
                if (key <= recKey)
                    return written;
                p += p[UH_LEN];
                continue;
            }

            for (n = 0; key == recKey && n < p[UH_CHARS]; n++) {
                q += 2;
                recKey = ju_MakeUshort((const char *)q);
                ti2++;
                key = ju_MakeUshort(&text[ti2 * 2]);
            }

            if (n != p[UH_CHARS]) {
                if (key <= recKey)
                    return written;
                p += p[UH_LEN];
                continue;
            }

            if (!incontext || DS_CONTEXT(d)[SN_CHARS] == p[UH_CHARS]) {
                if (ds_WriteUserData(d, p, slot, at) == 1) {
                    written++;
                    slot++;
                }
            }
            p += p[UH_LEN];
        }
    }
    return written;
}

/* Every word of the English dictionary that the romaji in `roman' spells.
 *
 * The mark argument is what says the word ran up to a character of the kind
 * the walk above stops on, and it makes the entry carry a part of speech of
 * its own rather than the record's. */
int16_t ds_LookupEngWordDict(void *d, uint8_t *roman, int16_t slot,
                             int16_t at, int16_t want, int32_t mark)
{
    int32_t  incontext;
    int16_t  written = 0;
    int16_t  ti = 0;
    uint16_t key;
    int16_t  i = 0;
    int16_t  found = 0;
    int16_t  block = 0;
    int16_t  end;
    int16_t  b;
    uint16_t here = 0;

    if (slot >= DS_ENTRY_N)
        return 0;
    incontext = DS_INCONTEXT(d);

    key = ju_MakeUshort((const char *)roman);
    do {
        here = ju_MakeUshort((const char *)dm_GetEDictHashAt((uint16_t)(i * 2)));
        if (!found && here >= key) {
            block = i > 0 ? (int16_t)((uint16_t)i + (uint16_t)i - 1) : 0;
            found = 1;
        }
        i++;
    } while (key >= here && i * 2 < 0x78);
    /* IBM tests for 0x78 here as well, and the loop above cannot reach it:
       its own bound is i twice that. Left as it is. */
    if (i == 0x78)
        block = (int16_t)(i - 1);
    end = (int16_t)(i * 2);

    for (b = block; b < end; b++) {
        const uint8_t *p = jajp_s_apszEng[(uint16_t)b];

        while (p != NULL && p[UH_LEN] != 0) {
            int16_t        ti2 = ti;
            const uint8_t *q = p + UH_TEXT;
            uint16_t       recKey = ju_MakeUshort((const char *)q);
            int16_t        n;

            key = ju_MakeUshort((const char *)&roman[ti2 * 2]);
            if (key != recKey) {
                if (key <= recKey)
                    return written;
                p += p[UH_LEN];
                continue;
            }

            for (n = 0;;) {
                if (key != recKey || n >= p[UH_CHARS])
                    break;
                n++;
                if (n >= p[UH_CHARS])
                    break;
                q += 2;
                recKey = ju_MakeUshort((const char *)q);
                ti2++;
                key = ju_MakeUshort((const char *)&roman[ti2 * 2]);
            }

            if (n != p[UH_CHARS]) {
                if (key <= recKey)
                    return written;
                p += p[UH_LEN];
                continue;
            }

            if (n == want
                && (!incontext || DS_CONTEXT(d)[SN_CHARS] == p[UH_CHARS])
                && ds_WriteUserData(d, p, slot, at) == 1) {
                if (mark)
                    DE_B(DS_ENTRY_AT(d, slot), DE_POS) = 0xe;
                written++;
                slot++;
            }
            p += p[UH_LEN];
        }
    }
    return written;
}

/* The run of full-width letters starting here, as one English word.
 *
 * It lowercases as it goes and stops at a capital following a small letter,
 * which is how a name written as one run comes apart into its words. Two
 * hashes are appended, which is what the dictionary's own records end with. */
int32_t ds_LookupEngWordDictFromText(void *d, int16_t slot, int16_t at)
{
    uint8_t *in = DS_INPUT(d);
    uint8_t  roman[64];
    int32_t  mark = 0;
    uint8_t  upper = 1;
    uint8_t  stop = 0;
    int16_t  i = at;
    int16_t  n = 0;

    for (; i < IC_COUNT_AT(in) && !stop && n != 0x11; i++, n++) {
        uint8_t c;

        if (IC_KIND_AT(in, i) != KIND_LATIN
            && IC_KIND_AT(in, i) != KIND_ENGWORD
            && !ju_DbCmp(IC_CHAR(in, i), ENG_ROMAN_MARK))
            break;

        ju_DbCpy((char *)&roman[n * 2], IC_CHAR(in, i));
        c = (uint8_t)((int8_t)IC_CHAR(in, i)[1] - 0x60);
        if (c > 0x19) {
            upper = 0;
        } else if (!upper) {
            stop = 1;
            n--;
        } else {
            roman[n * 2 + 1] = (uint8_t)(roman[n * 2 + 1] + 0x21);
            upper = 1;
        }

        if (IC_KIND_AT(in, i) == KIND_ENGWORD) {
            n++;
            mark = 1;
            break;
        }
    }

    roman[n * 2] = '#';
    roman[n * 2 + 1] = '#';
    roman[n * 2 + 2] = 0;
    return ds_LookupEngWordDict(d, roman, slot, at, n, mark);
}

/* Every single-kanji reading for the character here.
 *
 * The same trie the compound dictionary uses, keyed by one character at a
 * time, and every kanji is put through the variant table first so that a
 * variant form finds its standard form's readings. Where nothing at all is
 * found the character still gets a placeholder, or the path search would have
 * no way through the sentence. */
int16_t ds_LookupTankanDict(void *d, int16_t base, int16_t at)
{
    uint8_t       *in = DS_INPUT(d);
    int32_t        incontext;
    int16_t        i = at;
    int16_t        chars = 1;
    int16_t        total = 0;
    int16_t        step = 0x100;
    int16_t        pos = 0xff;
    uint16_t       key;
    const uint8_t *p;
    int32_t        done = 0;

    if (DS_INCONTEXT(d)) {
        if (DS_CONTEXT(d)[SN_CHARS] != 1)
            return 0;
        incontext = 1;
    } else {
        incontext = 0;
    }

    if (base >= DS_ENTRY_N)
        return 0;
    if (IC_KIND_AT(in, i) != KIND_KANJI && IC_KIND_AT(in, i) != KIND_DIGIT)
        return 0;

    key = ds_SwapKanji(d, ju_MakeUshort(IC_CHAR(in, i)));
    while (step != 0) {
        uint16_t h;

        step = (int16_t)(step / 2);
        h = ju_MakeUshort((const char *)dm_GetTDictHashAt((uint16_t)pos));
        if (key > h) {
            if (step)
                pos = (int16_t)(pos + step);
            else
                pos++;
        } else if (step) {
            pos = (int16_t)(pos - step);
        }
    }
    if (pos >= (int16_t)jajp_s_apszTankan_n)
        return 0;

    p = jajp_s_apszTankan[(uint16_t)pos];
    while (!done) {
        uint16_t nodeKey = (uint16_t)((p[TH_KEY] << 8) + p[TH_KEY + 1]);

        if (key == nodeKey) {
            if ((p[TH_FLAGS] & 0xf0) != 0) {
                int16_t n = 0;

                if (!incontext || DS_CONTEXT(d)[SN_CHARS] == chars)
                    n = ds_WriteTankanData(d, p, chars, base, at);
                total = (int16_t)(total + n);
                base = (int16_t)(base + n);
            }
            if (p[TH_CHILD] == 0) {
                done = 1;
                continue;
            }
            i++;
            key = ds_SwapKanji(d, ju_MakeUshort(IC_CHAR(in, i)));
            chars++;
            p += p[TH_CHILD];
            continue;
        }
        if (key > nodeKey) {
            uint16_t skip = (uint16_t)(((p[TH_FLAGS] & 0xf) << 8)
                                       + p[TH_SIBLING]);

            if (skip == 0)
                done = 1;
            else
                p += skip;
            continue;
        }
        done = 1;
    }

    if (total == 0 && !DS_INCONTEXT(d))
        total = (int16_t)(total + ds_ErrorDummy(d, base, at));
    return total;
}

/* Every compound word of the main dictionary that starts here.
 *
 * The hash is over two characters rather than one, which is what makes a
 * dictionary of this size searchable: the walk halves a range of 512 on the
 * first character and, where that ties, on the second. Then the trie is walked
 * one character at a time, taking every word that ends on the way down, so a
 * two-character word and a five-character one starting in the same place both
 * come back.
 *
 * Two retries follow. The hash lands on a block boundary, so a word may sit in
 * the block before the one it points at -- hence stepping back one while
 * anything at all matched. And where the walk went deeper than one character
 * it is worth trying the block after, once. Last of all, a run that found
 * nothing is tried again with the variant table on, which is the `swap'
 * argument: a variant kanji then finds its standard form's words. */
int16_t ds_LookupNormalWordDict(void *d, int16_t base, int16_t at,
                                int32_t swap)
{
    uint8_t *in = DS_INPUT(d);
    int16_t  firstBase = base;
    int32_t  incontext = DS_INCONTEXT(d);
    int16_t  chars;
    int16_t  hira;
    int16_t  total = 0;
    int16_t  i = at;
    int16_t  step = 0x200;
    int16_t  pos = 0x1ff;
    int16_t  saved;
    int32_t  hit;
    int32_t  retry;
    int32_t  first;
    uint16_t key;

    if (base >= DS_ENTRY_N)
        return 0;

    chars = 1;
    hira = 0;
    key = ju_MakeUshort(IC_CHAR(in, i));
    if (swap)
        key = ds_SwapKanji(d, key);
    if (IC_KIND_AT(in, i) == KIND_HIRAGANA)
        hira++;

    while (step != 0) {
        uint16_t h;
        uint16_t key2;
        uint16_t h2;

        step = (int16_t)(step / 2);
        h = ju_MakeUshort((const char *)dm_GetNDictHashAt((uint16_t)pos, 0));
        if (key < h) {
            if (step)
                pos = (int16_t)(pos - step);
            continue;
        }
        if (key > h) {
            if (step)
                pos = (int16_t)(pos + step);
            else
                pos++;
            continue;
        }
        key2 = ju_MakeUshort(IC_CHAR(in, i + 1));
        if (swap)
            key2 = ds_SwapKanji(d, key2);
        h2 = ju_MakeUshort((const char *)dm_GetNDictHashAt((uint16_t)pos, 2));
        if (key2 <= h2) {
            if (step)
                pos = (int16_t)(pos - step);
        } else if (step) {
            pos = (int16_t)(pos + step);
        } else {
            pos++;
        }
    }

    hit = 0;
    retry = 1;
    first = 1;
    saved = pos;

    for (;;) {
        const uint8_t *p;
        int32_t        done = 0;

        if (pos >= (int16_t)jajp_s_apszNormal_n)
            return 0;
        p = jajp_s_apszNormal[(uint16_t)pos];

        while (!done) {
            uint16_t nodeKey = (uint16_t)((p[NH_KEY] << 8) + p[NH_KEY + 1]);
            uint16_t child = (uint16_t)(((p[NH_FLAGS] & 0x7f) << 4)
                                        + p[NH_CHILD]);

            if (key == nodeKey) {
                if (first) {
                    hit = 1;
                    first = 0;
                }
                if ((p[NH_FLAGS] & 0x80) != 0) {
                    int16_t n = 0;

                    if (!incontext || DS_CONTEXT(d)[SN_CHARS] == chars)
                        n = ds_WriteData(d, p, chars, hira, base, i, at);
                    total = (int16_t)(total + n);
                    base = (int16_t)(base + n);
                }
                if (child == 0) {
                    done = 1;
                    continue;
                }
                i++;
                key = ju_MakeUshort(IC_CHAR(in, i));
                if (swap)
                    key = ds_SwapKanji(d, key);
                if (IC_KIND_AT(in, i) == KIND_HIRAGANA)
                    hira++;
                chars++;
                p += child;
                continue;
            }

            if (key < nodeKey && first) {
                hit = 1;
                done = 1;
                continue;
            }
            first = 0;
            if (key <= nodeKey) {
                done = 1;
                continue;
            }
            {
                uint16_t sib = (uint16_t)((p[NH_SIBLING] << 8)
                                          + p[NH_SIBLING + 1]);

                if (sib == 0)
                    done = 1;
                else
                    p += sib;
            }
        }

        if (pos > 0 && hit == 1) {
            /* Something matched at the very first node, so the word may
               begin in the block before this one. */
            pos--;
            hit = 0;
            first = 1;
        } else if (chars > 1 && retry == 1) {
            /* And where the walk went deeper than one character, the block
               after is worth one try. */
            pos = (int16_t)(saved + 1);
            retry = 0;
        } else {
            break;
        }
        chars = 1;
        hira = 0;
        i = at;
        key = ju_MakeUshort(IC_CHAR(in, i));
        if (swap)
            key = ds_SwapKanji(d, key);
    }

    if (!incontext && total == 0 && swap == 0)
        return ds_LookupNormalWordDict(d, firstBase, at, 1);
    return total;
}

/* ---- the function words ---------------------------------------------- */

/* This is fdictapi.obj, and it is a different kind of lookup from the five
 * above. A function word -- a particle, an ending, an auxiliary -- is not
 * chosen on its spelling alone but on what it may attach to, so the dictionary
 * carries a bit vector per word saying which kinds of phrase it can follow,
 * and the search is handed the vector of what actually precedes it. A word is
 * taken only where the two agree.
 *
 * What comes out is not a candidate entry but a row of the function-word
 * array, one per match; `LookupFuncWordDict' is the pass that turns those into
 * candidate entries afterwards, working out each one's part of speech by
 * looking its four descriptive bytes up in the phrase-type table.
 */

/* Reaching a row of that array. */
#define DS_FZK_AT(d, i)    DS_AT(d, DS_FZK + (i) * DS_FZK_SIZE)
#define FZ_B(f, off)       (*((uint8_t *)(f) + (off)))
#define FZ_W(f, off)       (*(int16_t *)((uint8_t *)(f) + (off)))

/* The kana a phrase may not begin with: a doubled consonant, the three small
   y-kana, and the syllabic n. A word whose flag says it starts a phrase does
   not, if the character after it is one of these. */
static const char *const NOT_INITIAL[] = {
    "\x82\xc1",                 /* the small tsu */
    "\x82\xe1", "\x82\xe3", "\x82\xe5",   /* small ya, yu, yo */
    "\x82\xf1",                 /* n */
    NULL
};

/* Where the hiragana begin, which is what the function dictionary's index is
   measured from, and the first code past the end of it. */
static const char HIRAGANA_FIRST[] = "\x82\xa0";
#define HIRAGANA_PAST   0x82ff

/* The long-vowel bar, which continues a function word rather than ending it. */
static const char LONG_BAR[] = "\x81\x5b";

/* Where the index and the records live inside the function dictionary. */
#define FD_INDEX        0        /* one int16 per hiragana */
#define FD_INDEX_LAST   0xa6     /* and one more for everything past them */
#define FD_RECORDS      0xa8

/* Every word of one trie node that the preceding phrase can take.
 *
 * The node's words are tried in turn and each against all fourteen of the bit
 * vector's bytes; a word is taken on the first byte where its own vector and
 * the caller's agree. What is written is a row of the function-word array
 * rather than a candidate entry, because what a function word is worth cannot
 * be settled until the pass above has looked its phrase type up.
 *
 * Answers how many rows were written. */
int16_t ds_HitFuncWordDict(void *d, const uint8_t *head, int16_t slot,
                           int16_t at, int16_t count, int16_t run,
                           int16_t hiragana, const uint8_t *vec,
                           const uint8_t *dict, int16_t flag)
{
    uint8_t       *in = DS_INPUT(d);
    const uint8_t *p = head + FN_WORD;
    uint16_t       next = ju_MakeUshort(IC_CHAR(in, at + 1));
    int16_t        written = 0;
    int16_t        i;

    for (i = 0; i < count; i++, p += p[FW_LEN]) {
        int16_t j;

        if (p[FW_KEY] == 0x5e)
            continue;

        for (j = 0; j < 14; j++) {
            uint8_t  mine;
            uint8_t  theirs;
            uint8_t *f;

            if (p[FW_PHRVEC] < 1)
                break;
            mine = dm_GetPhrVectorAt((uint16_t)((p[FW_PHRVEC] - 1) * 14 + j));
            theirs = vec[j];
            if ((mine & theirs) == 0)
                continue;

            /* Where the caller asked for phrase starts only, a word that is
               not marked as one is passed over. */
            if (flag != 0
                && (flag != 1 || (p[FW_KAKARI] & 0x40) == 0))
                break;

            if (slot >= DS_FZK_N)
                return written;
            f = DS_FZK_AT(d, slot);

            FZ_B(f, FZ_CHARS) = (uint8_t)run;
            FZ_B(f, FZ_HIRAGANA) = (uint8_t)hiragana;
            FZ_W(f, FZ_KEY) = (int16_t)(uint16_t)p[FW_KEY];
            FZ_W(f, FZ_WORD) = (int16_t)(p - dict);
            FZ_W(f, FZ_AT) = at;
            FZ_W(f, FZ_OFFSET) = IC_OFFSET_AT(in, at - run + 1);

            /* Whether this word begins a phrase. It says so itself, and the
               character after it can still say otherwise: nothing may begin
               with a doubled consonant, a small y-kana or an n. */
            {
                int32_t starts = (p[FW_KAKARI] & 0x80) != 0;
                int     k;

                for (k = 0; starts && NOT_INITIAL[k] != NULL; k++)
                    if (next == ju_MakeUshort(NOT_INITIAL[k]))
                        starts = 0;
                FZ_B(f, FZ_FLAGS) = (uint8_t)(starts ? 1 : 0);
            }
            if ((dm_GetPenaltyAt((uint16_t)((p[FW_PENALTY] - 1) * 14 + j))
                 & theirs) != 0)
                FZ_B(f, FZ_FLAGS) = (uint8_t)(FZ_B(f, FZ_FLAGS) | 2);

            written++;
            slot++;
            break;
        }
    }
    return written;
}

/* Walk the function-word trie from the character at `at'.
 *
 * The index is one entry per hiragana, so the first character picks where in
 * the dictionary to start without any search at all; everything at or past the
 * end of the hiragana shares the last entry. From there it is an ordinary trie
 * walk, except that a long-vowel bar does not end a word -- the same node is
 * asked again with the bar counted in.
 *
 * Answers how many rows of the function-word array were written. */
int16_t ds_SearchFuncWordDict(void *d, const uint8_t *vec, int16_t at,
                              int16_t slot, const uint8_t *dict, int16_t flag)
{
    uint8_t       *in = DS_INPUT(d);
    int16_t        run = 1;
    int16_t        hira = 0;
    int16_t        written = 0;
    uint16_t       idx;
    uint16_t       key = ju_MakeUshort(IC_CHAR(in, at));
    const uint8_t *p;
    int32_t        done = 0;
    int16_t        count = 0;

    if (key < ju_MakeUshort(HIRAGANA_FIRST))
        return written;

    if (key < HIRAGANA_PAST)
        idx = ju_MakeUshort((const char *)
                            (dict + (key - ju_MakeUshort(HIRAGANA_FIRST) + 1)
                             * 2));
    else
        idx = ju_MakeUshort((const char *)(dict + FD_INDEX_LAST));
    if (idx == 0xffff || idx == 0)
        return written;

    p = dict + FD_RECORDS + idx;
    if (IC_KIND_AT(in, at) == KIND_HIRAGANA)
        hira++;

    while (!done) {
        uint16_t nodeKey = (uint16_t)((p[FN_KEY] << 8) + p[FN_KEY + 1]);

        if (key == nodeKey) {
            count = (int16_t)(p[FN_FLAGS] >> 4);
            if (count != 0) {
                int16_t w = ds_HitFuncWordDict(d, p, slot, at, count, run,
                                               hira, vec, dict, flag);

                slot = (int16_t)(slot + w);
                written = (int16_t)(written + w);
            }
            at++;
            key = ju_MakeUshort(IC_CHAR(in, at));
            if (IC_KIND_AT(in, at) == KIND_HIRAGANA)
                hira++;
            run++;

            /* A bar after it lengthens the same word rather than starting a
               new one, so the node is asked again with the bar counted in. */
            if (key == ju_MakeUshort(LONG_BAR) && count != 0) {
                int16_t w = ds_HitFuncWordDict(d, p, slot, at, count, run,
                                               hira, vec, dict, flag);

                slot = (int16_t)(slot + w);
                written = (int16_t)(written + w);
                at++;
                key = ju_MakeUshort(IC_CHAR(in, at));
                if (IC_KIND_AT(in, at) == KIND_HIRAGANA)
                    hira++;
                run++;
            }

            if (p[FN_CHILD] == 0)
                done = 1;
            else
                p += 2 + p[FN_CHILD];
            continue;
        }

        if (key > nodeKey) {
            uint16_t skip = (uint16_t)(((p[FN_FLAGS] & 0xf) << 8)
                                       + p[FN_SIBLING]);

            if (skip == 0)
                done = 1;
            else
                p += 3 + skip;
            continue;
        }
        done = 1;
    }
    return written;
}

/* Every function word that can start at `at', as candidate entries.
 *
 * The vector handed to the search is all ones, which is to say anything at
 * all may precede: this is the pass that finds what is there rather than the
 * one that chooses. What comes back is a row per match, and each becomes a
 * candidate entry here -- the reading copied out, the accent read from its own
 * table, and the part of speech worked out by describing the word in four
 * bytes and finding the row of the phrase-type table that matches.
 *
 * Those four bytes are the whole of what a function word is to the path
 * search: nothing, a fixed one, what its kakari row says about what it
 * attaches to, and the key byte its dictionary record carries. */
int16_t ds_LookupFuncWordDict(void *d, int16_t base, int16_t at)
{
    uint8_t       *in = DS_INPUT(d);
    const uint8_t *dict = dm_GetFuncDictEx();
    uint8_t        vec[14];
    int16_t        found;
    int16_t        i;

    for (i = 0; i < 14; i++)
        vec[i] = 0xff;
    for (i = 0; i < DS_FZK_N * DS_FZK_SIZE; i++)
        DS_B(d, DS_FZK + i) = 0;
    for (i = 0; i < DS_FZK_N; i++) {
        FZ_B(DS_FZK_AT(d, i), FZ_MARK) = 0xff;
        FZ_B(DS_FZK_AT(d, i), FZ_FLAGS) = 0;
    }

    found = ds_SearchFuncWordDict(d, vec, at, 0, dict, 1);
    if (found == 0)
        return 0;

    for (i = 0; i < found; i++) {
        uint8_t       *e;
        uint8_t       *f;
        int16_t        off;
        const uint8_t *accent;
        uint8_t        kakari;
        uint8_t        tg[4];
        int16_t        j;

        if (base + i >= DS_ENTRY_N)
            return i;
        e = DS_ENTRY_AT(d, base + i);
        f = DS_FZK_AT(d, i);

        DE_B(e, DE_CHARS) = FZ_B(f, FZ_CHARS);
        DE_B(e, DE_HIRAGANA) = FZ_B(f, FZ_HIRAGANA);

        off = FZ_W(f, FZ_WORD);
        accent = dm_GetAccentAt((uint16_t)(dict[off + FW_ACCENT] - 1));
        DE_W(e, DE_ACCENT) = (int16_t)(accent[2] & 0xf);
        DE_B(e, DE_KANALEN) = (uint8_t)(dict[off + FW_LEN] - FW_KANA);
        DE_W(e, DE_AT) = at;
        DE_L(e, DE_MARK) = IC_MARK_AT(in, at);
        DE_W(e, DE_OFFSET) = IC_OFFSET_AT(in, at);
        DE_L(e, DE_COST) = 3;

        kakari = dm_GetKakariAt((uint16_t)
                                (((dict[off + FW_KAKARI] - 1) & 0x3f) * 6 + 4));

        for (j = 0; j < 4; j++)
            tg[j] = 0;
        tg[1] = 1;
        tg[3] = FZ_B(f, FZ_KEY);
        switch (kakari) {
        case 1:            tg[2] = 0x20; break;
        case 2: case 4:    tg[2] = 0x40; break;
        case 8:            tg[1] = (uint8_t)(tg[1] | 0x10); break;
        default:           break;
        }

        for (j = 0x80; j < 0xb4; j++)
            if (tg[0] == dm_GetTGAt2((uint8_t)j, 0)
                && tg[1] == dm_GetTGAt2((uint8_t)j, 1)
                && tg[2] == dm_GetTGAt2((uint8_t)j, 2)
                && tg[3] == dm_GetTGAt2((uint8_t)j, 3))
                break;
        DE_B(e, DE_POS) = (uint8_t)j;

        for (j = 0; j < DE_B(e, DE_KANALEN); j++)
            DE_B(e, DE_KANA + j) = dict[off + FW_KANA + j];

        /* An accent past the end of the reading is brought back to it, and
           then off a mora that cannot carry one. */
        if (DE_U(e, DE_ACCENT) > DE_B(e, DE_KANALEN)) {
            uint8_t last;

            DE_W(e, DE_ACCENT) = (int16_t)(uint16_t)DE_B(e, DE_KANALEN);
            last = DE_B(e, DE_KANA + DE_B(e, DE_KANALEN) - 1);
            if ((last / 8 == 0x1e || last == 0xfd || last == 0xfe)
                && DE_B(e, DE_KANALEN) > 1)
                DE_W(e, DE_ACCENT)--;
        }
    }
    return found;
}

/* ---- English spelling into romaji ------------------------------------ */

/* This is engread.obj, and it is the one part of DictSearch that is not a
 * dictionary at all. An English word written in the text has no entry
 * anywhere -- a Japanese dictionary cannot hold English -- so it is spelled
 * out by rule instead: a table of substitutions turns the letters into romaji,
 * a second table turns the romaji into kana codes, and what falls out is a
 * reading the rest of the analyser can use like any other.
 *
 * A rule is five parallel arrays with one entry each: what to match, what to
 * put in its place, what to leave behind for the next pass to see, and two
 * that say where the accent goes. The position arrays give each entry's start,
 * so an entry's length is the next start less its own -- which is why every
 * loop here reads two of them.
 *
 * Three characters in a rule are not literal. `!' anchors the match to the end
 * of the word; `@' matches any consonant and remembers which; `*' marks, in
 * the replacement, where the accent may fall.
 */

/* The consonants `@' stands for, and the end of the word, which it also
   matches. */
static const char ENG_CONSONANTS[] = "bcdfgjklmnpstvz#";

/* What a rule may not exceed, and the sentinel in front of the word so that a
   rule matching at the start has something to look back at. */
#define ENG_MOST        0x1b
#define ENG_GUARD       '%'

/* Every letter uppercased, which is what a word that does not look like
   English gets instead of the rules: spelled out letter by letter. */
int16_t ds_EngRulesUppercase(void *d, const uint8_t *in, uint8_t *out)
{
    int16_t i = 0;
    int16_t o = 0;

    (void)d;
    while (in[i] != '#') {
        if (in[i] >= 'a' && in[i] <= 'z')
            out[o++] = (uint8_t)(in[i] - 0x20);
        else
            out[o++] = in[i];
        i++;
    }
    out[o++] = '#';
    out[o++] = '#';
    return 0;
}

/* Every letter lowercased, and a judgement on whether the word is English at
 * all.
 *
 * Four things say it is not: no vowel in a word longer than three letters, no
 * vowel at all in a short one, a capital in the middle of a word of four or
 * fewer, and a word of one letter. Any of them answers six, which is what
 * makes the caller spell it out instead. `y' counts as half a vowel -- enough
 * for a short word, not enough for a long one. */
int16_t ds_EngRulesNormalize(void *d, const uint8_t *in, uint8_t *out)
{
    int16_t i = 0;
    int16_t o = 0;
    int16_t vowel = 0;
    int16_t midCap = 0;
    int16_t rc = 0;

    (void)d;
    while (in[i] != '#') {
        uint8_t c;

        if (in[i] >= 'A' && in[i] <= 'Z') {
            if (o != 0)
                midCap = 1;
            out[o++] = (uint8_t)(in[i] + 0x20);
            i++;
        } else {
            out[o++] = in[i];
            i++;
        }
        c = out[o - 1];
        if (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u')
            vowel = 2;
        if (c == 'y' && vowel == 0)
            vowel = 1;
    }

    if (o > 3 && vowel != 2)
        rc = 6;
    if (o <= 3 && vowel == 0)
        rc = 6;
    if (o <= 4 && midCap == 1)
        rc = 6;
    if (o == 1)
        rc = 6;

    out[o++] = '#';
    out[o++] = '#';
    return rc;
}

/* Run one table of substitutions over a word.
 *
 * The word is walked from the left, and at each position every rule is tried
 * in turn until one matches. What the rule says to put in its place is
 * appended to the output, what it says to leave behind is written back over
 * the input so the next position sees it, and the walk moves on by however
 * much the match consumed less what was left behind.
 *
 * The accent is carried along beside it. A rule may name a weight and a way of
 * choosing: minus one takes the last place a vowel was seen, nought takes the
 * place this rule's own star marked, and one carries the weight forward to the
 * next rule instead of settling anything. The heaviest rule wins, and a tie is
 * broken in favour of the later one only above a weight of eight.
 *
 * Answers nought, twenty for a word longer than the buffer, or sixteen where
 * no rule matched at all. */
int16_t ds_EngRulesApplyRule(void *d, const uint8_t *in, uint8_t *out,
                             DictManRules *r, int16_t *accent)
{
    const int16_t *fromPos = (const int16_t *)r->fromPos;
    const int16_t *toPos = (const int16_t *)r->toPos;
    const int16_t *remainPos = (const int16_t *)r->remainPos;
    const int16_t *accentValue = (const int16_t *)r->accentValue;
    const int16_t *accentPos = (const int16_t *)r->accentPos;
    uint8_t  buf[128];
    uint8_t  tail[80];
    uint8_t  capture = 0;
    int16_t  i = 0;
    int16_t  o = 0;
    int16_t  tailLen = 0;
    int16_t  len;
    int16_t  best = 0;
    int16_t  carry = 0;
    int16_t  where = -1;
    int16_t  lastVowel = -1;
    int16_t  matched = 0;
    int16_t  j;

    (void)d;
    buf[0] = ENG_GUARD;
    while (in[i] != '#') {
        buf[i + 1] = in[i];
        i++;
        if (i == ENG_MOST)
            return 0x14;
    }
    len = (int16_t)(i + 1);
    buf[i + 1] = '#';
    buf[i + 2] = '#';

    i = 0;
    while (buf[i] != '#') {
        int16_t rule;

        matched = 0;
        for (rule = 0; rule < (int16_t)r->count - 1 && !matched; rule++) {
            int16_t flen = (int16_t)(fromPos[rule + 1] - fromPos[rule]);
            int16_t saved = i;
            int16_t tlen;
            int16_t star = 0;
            int16_t starAt = -1;

            matched = 1;

            /* An anchored rule matches against the end of the word rather
               than against where the walk has got to. */
            if (r->from[fromPos[rule]] == '!' && len > flen)
                i = (int16_t)(len - flen + 1);

            for (j = 0; j < flen; j++) {
                uint8_t c = r->from[fromPos[rule] + j];

                if (c == '@') {
                    uint8_t k = buf[i + j];

                    if (strchr(ENG_CONSONANTS, (int)k) != NULL && k != 0)
                        capture = k;
                    else
                        matched = 0;
                } else if (c != buf[i + j] && c != '!') {
                    matched = 0;
                }
            }
            i = saved;
            if (matched != 1)
                continue;

            if (r->from[fromPos[rule]] == '!') {
                /* The word ends here: cut it short and keep the replacement
                   to be laid down last of all, after everything in front. */
                len = (int16_t)(len - (flen - 2));
                buf[len] = '#';
                buf[len + 1] = '#';
                tlen = (int16_t)(toPos[rule + 1] - toPos[rule]);
                for (j = 1; j < tlen; j++)
                    tail[tailLen++] = r->to[toPos[rule + 1] - j];
                goto leave;
            }

            if (i - 1 <= *accent && *accent < i + flen - 1)
                where = o;
            i = (int16_t)(i + flen);

            tlen = (int16_t)(toPos[rule + 1] - toPos[rule]);
            for (j = 0; j < tlen; j++) {
                uint8_t c = r->to[toPos[rule] + j];

                if (c == '*') {
                    starAt = (int16_t)(o - 1);
                    star = 1;
                } else {
                    out[o++] = c;
                }
            }

            if (accentValue[rule] != 0) {
                /* No star of its own: the last vowel it wrote will do. */
                if (starAt == -1)
                    for (j = 0; j < tlen; j++) {
                        uint8_t c = out[o - j - 1];

                        if (c == 'a' || c == 'i' || c == 'u' || c == 'e'
                            || c == 'o') {
                            starAt = (int16_t)(o - j - 1);
                            star = 1;
                        }
                    }

                if (star == 1 || r->accentPos != NULL) {
                    int16_t w = accentValue[rule];

                    if (carry > w)
                        w = carry;
                    if (w > best || (w == best && w > 8)) {
                        best = w;
                        switch (accentPos[rule]) {
                        case -1: where = lastVowel; carry = 0; break;
                        case 0:  where = starAt;    carry = 0; break;
                        case 1:  carry = accentValue[rule]; break;
                        default: break;
                        }
                    }
                }
            }
            if (starAt != -1)
                lastVowel = starAt;

        leave:
            /* And what the rule leaves behind, written back over the input so
               that the next position sees it. */
            {
                int16_t rlen = (int16_t)(remainPos[rule + 1]
                                         - remainPos[rule]);

                i = (int16_t)(i - rlen);
                for (j = 0; j < rlen; j++) {
                    uint8_t c = r->remain[remainPos[rule] + j];

                    buf[i + j] = c == '@' ? capture : c;
                }
            }
        }
        if (matched == 0)
            return 0x10;
    }

    for (j = 0; j < tailLen; j++)
        out[o++] = tail[tailLen - j - 1];
    out[o++] = '#';
    out[o++] = '#';
    *accent = where;
    return 0;
}

/* An English word as the engine's own kana codes.
 *
 * Three passes: lowercase it and decide whether it is English at all, then the
 * letters into romaji, then the romaji into kana. What the last pass leaves is
 * three characters per kana -- a row, a column and an accent -- and this turns
 * each triple into the one byte a reading is made of.
 *
 * The `$' in an accent position means the same as the one before it, which is
 * substituted in place before the switch reads it; the switch has a case for
 * `$' all the same, and it can only be reached where the previous one was a
 * `$' as well. IBM's, and left as it is. */
int16_t ds_EngRulesConvert(void *d, const uint8_t *in, uint8_t *out,
                           DictManRules *eng, DictManRules *kana,
                           int16_t *outLen, int16_t *count)
{
    uint8_t lower[128];
    uint8_t roman[256];
    uint8_t kanaBuf[256];
    uint8_t prev = 0;
    int16_t rc;
    int16_t k;

    *count = -1;

    rc = ds_EngRulesNormalize(d, in, lower);
    if (rc == 6)
        rc = ds_EngRulesUppercase(d, in, lower);
    if (rc > 0)
        return rc;

    rc = ds_EngRulesApplyRule(d, lower, roman, eng, count);
    if (rc > 0)
        return rc;
    rc = ds_EngRulesApplyRule(d, roman, kanaBuf, kana, count);
    if (rc > 0)
        return rc;

    *outLen = 0;
    for (k = 0; kanaBuf[k] != '#'; k = (int16_t)(k + 3)) {
        uint8_t v;
        int32_t column;

        switch (kanaBuf[k]) {
        case '0': v = 0x00; break;
        case '1': v = 0x50; break;
        case '2': v = 0xa0; break;
        case '3': v = 0xf0; break;
        default:  return 0x10;
        }

        column = kanaBuf[k + 1] - '0';
        if ((uint32_t)column > 9)
            return 0x10;
        v = (uint8_t)(v + column * 8);

        if (kanaBuf[k + 2] == '$')
            kanaBuf[k + 2] = prev;
        switch (kanaBuf[k + 2]) {
        case '0': break;
        case '1': v = (uint8_t)(v + 1); break;
        case '2': v = (uint8_t)(v + 2); break;
        case '3': v = (uint8_t)(v + 3); break;
        case '4': v = (uint8_t)(v + 4); break;
        case '5': v = (uint8_t)(v + 5); break;
        case '6': v = (uint8_t)(v + 6); break;
        case '7': v = (uint8_t)(v + 7); break;
        case '$': v = (uint8_t)(v + prev); break;
        default:  return 0x10;
        }
        prev = kanaBuf[k + 2];

        out[*outLen] = v;
        (*outLen)++;
    }

    *count = (int16_t)(*count / 3 + 1);
    return 0;
}

/* ---- what happens when nothing was found ----------------------------- */

/* The last of dictsearch.obj that Do does not itself need, and the answer to
 * a question every dictionary above leaves open: what to do with a character
 * no dictionary knew.
 *
 * Something has to be produced or the path search has no way through the
 * sentence at all, so `HandleError' looks at what kind of character it is and
 * falls back accordingly -- katakana and hiragana are spelled out by the rules
 * that already exist, a full-width letter goes through the English rules, and
 * anything else gets a placeholder. Then it looks for a number counter, which
 * is a separate little dictionary of its own, and last of all it hands back
 * the character itself where nothing readable came of any of it.
 */

/* Reaching the parse marks, which are TextAnalysis's and are what says where
   a candidate ended. */
#define TA_MARK_AT(t, i)   (*((uint8_t *)(t) + TA_MARKS + (i)))

/* And the three records DictSearch keeps for a number counter. */
#define DS_REC_AT(d, i)    DS_AT(d, DS_REC + (i) * DS_REC_SIZE)

/* What a counter record holds, which SetJCC fills in from the number
   dictionary and HandleError reads straight back out. */
#define JC_ACCENT       0        /* int16 */
#define JC_KANALEN      2        /* uint8 */
#define JC_CHARS        3        /* uint8 */
#define JC_KANA         6

/* And what one of that dictionary's own records holds. */
#define JM_KEY          0        /* two bytes, the first character */
#define JM_CHARS        2        /* uint8 */
#define JM_KANALEN      3        /* uint8 */
#define JM_TEXT         4        /* the characters, then the reading */

/* A placeholder for a character that is not a word at all.
 *
 * The marker of 0xff in the first kana byte is what says the reading is not
 * real, the same as ErrorDummy's; what differs is the part of speech. */
void ds_SetDummySymbol(void *d, int16_t at, void *e)
{
    uint8_t *in = DS_INPUT(d);

    DE_B(e, DE_KANA) = 0xff;
    DE_B(e, DE_CHARS) = 1;
    DE_B(e, DE_KANALEN) = 0;
    DE_B(e, DE_POS) = 9;
    DE_W(e, DE_AT) = at;
    DE_L(e, DE_MARK) = IC_MARK_AT(in, at);
    DE_W(e, DE_OFFSET) = IC_OFFSET_AT(in, at);
}

/* A single letter as its own name -- ay, bee, see -- which is what an English
   word gets when the rules refuse it. The yomi table carries the name of each
   letter, six bytes to a row: how long it is and then the codes. */
void ds_SetDummyRomanAlphabet(void *d, int16_t at, void *e)
{
    uint8_t       *in = DS_INPUT(d);
    const uint8_t *yomi;
    uint8_t        c;
    int16_t        n;
    int16_t        i;

    c = (uint8_t)((int8_t)IC_CHAR(in, at)[1] - 0x60);
    if (c > 0x19)
        c = (uint8_t)(c - 0x21);

    yomi = dm_GetYomiDataPtr();
    n = (int16_t)(uint8_t)yomi[YOMI_LETTER + c * 6];
    for (i = 1; i <= n; i++)
        DE_B(e, DE_KANA + i - 1) = dm_GetYomiDataPtr()[YOMI_LETTER + c * 6 + i];

    DE_W(e, DE_ACCENT) = 1;
    DE_B(e, DE_KANALEN) = (uint8_t)n;
    DE_B(e, DE_CHARS) = 1;
    DE_B(e, DE_POS) = 0;
    DE_B(e, DE_ATTR) = 0x18;
    DE_B(e, DE_ATTR2) = 0;
    DE_W(e, DE_AT) = at;
    DE_L(e, DE_MARK) = IC_MARK_AT(in, at);
    DE_W(e, DE_OFFSET) = IC_OFFSET_AT(in, at);
}

/* A run of full-width letters as one English word.
 *
 * The same run the English dictionary would have taken -- letters, stopping at
 * a capital that follows a small one -- but read by rule rather than looked
 * up. Anything the rules refuse, and anything too short or too long, falls
 * back to spelling the first letter out.
 *
 * IBM sets the part of speech to nought in both arms of a test on whether the
 * run ended on a marked character; the two arms are the same instruction, so
 * only one is written here. */
void ds_ProcessRomanAlphabet(void *d, int16_t at, void *e)
{
    uint8_t *in = DS_INPUT(d);
    uint8_t *ta = DS_OWNER_OF(d);
    uint8_t *rz = *(uint8_t **)(ta + TA_OWNER_AT);
    uint8_t  word[64];
    uint8_t  kana[128];
    int16_t  room;
    int16_t  upper = 1;
    int16_t  marked = 0;
    int16_t  n = 0;
    int16_t  len;
    int16_t  outLen = 0;
    int16_t  count = 0;
    int16_t  rc;
    int16_t  i;

    if (*(int32_t *)(rz + RZ_SPELL_ENGLISH) > 0) {
        ds_SetDummyRomanAlphabet(d, at, e);
        return;
    }

    room = (int8_t)DS_B(ta, TA_LONGWORDS) < TA_LONGWORD_N ? 0x18 : 0x8;

    for (i = at; i < IC_COUNT_AT(in); i++, n++) {
        uint8_t c;

        if (IC_KIND_AT(in, i) != KIND_LATIN
            && IC_KIND_AT(in, i) != KIND_ENGWORD)
            break;
        if (n == 0x11) {
            ds_SetDummyRomanAlphabet(d, at, e);
            return;
        }

        c = (uint8_t)((int8_t)IC_CHAR(in, i)[1] - 0x60);
        if (c > 0x19) {
            c = (uint8_t)(c - 0x21);
            word[n] = (uint8_t)(c + 'a');
            upper = 0;
        } else {
            if (!upper)
                break;
            word[n] = (uint8_t)(c + 'A');
            upper = 1;
        }

        if (IC_KIND_AT(in, i) == KIND_ENGWORD) {
            n++;
            marked = 1;
            break;
        }
    }

    len = n;
    word[n++] = '#';
    word[n++] = '#';
    word[n++] = 0;

    /* Two letters or fewer, all of one case: not worth the rules. */
    if (n <= 5 && upper == 1) {
        ds_SetDummyRomanAlphabet(d, at, e);
        return;
    }

    rc = ds_EngRulesConvert(d, word, kana, &dm_EngToRomanRule,
                            &dm_RomanToKanaRule, &outLen, &count);
    if (rc != 0 || outLen > room) {
        ds_SetDummyRomanAlphabet(d, at, e);
        return;
    }

    DE_B(e, DE_KANALEN) = (uint8_t)outLen;
    if (DE_B(e, DE_KANALEN) > 9)
        ds_SetLongWord(d, (int16_t)DE_B(e, DE_KANALEN), e, kana);
    else
        for (i = 0; i < DE_B(e, DE_KANALEN); i++)
            DE_B(e, DE_KANA + i) = kana[i];

    DE_W(e, DE_ACCENT) = count;
    DE_B(e, DE_CHARS) = (uint8_t)len;
    DE_B(e, DE_POS) = 0;
    DE_B(e, DE_ATTR) = 0x18;
    DE_B(e, DE_ATTR2) = 0x40;
    DE_W(e, DE_AT) = at;
    DE_L(e, DE_MARK) = IC_MARK_AT(in, at);
    DE_W(e, DE_OFFSET) = IC_OFFSET_AT(in, at);
    (void)marked;
}

/* Whether the candidates from `base' are worth taking as katakana.
 *
 * One that is a noun by its phrase type, has a reading, and is not marked as
 * guessed at is good enough to keep, and answers nought -- do not analyse.
 * Minus one where there are no candidates at all. */
int32_t ds_NeedKatakanaAnalysis(void *d, int16_t base, int16_t n)
{
    int16_t i;

    if (n == 0)
        return -1;
    for (i = base; i < base + n; i++) {
        uint8_t *e = DS_ENTRY_AT(d, i);

        if ((dm_GetTGAt2(DE_B(e, DE_POS), 2) & 0x20) == 0)
            continue;
        if (DE_B(e, DE_KANALEN) == 0)
            continue;
        if ((DE_B(e, DE_ATTR2) & 4) != 0)
            continue;
        return 0;
    }
    return 1;
}

/* Mark where every candidate ends, and say whether they are all short.
 *
 * The mark is what the pass above reads to know that a character has been
 * accounted for; a candidate that ends past the end of the text, or that is
 * the placeholder, does not set one. Answers minus one where the array is
 * full, one where every candidate is short, nought otherwise. */
int16_t ds_CheckJrtTable(void *d, int16_t base, int16_t n)
{
    uint8_t *in = DS_INPUT(d);
    uint8_t *ta = DS_OWNER_OF(d);
    int32_t  allShort = 1;
    int16_t  i;

    for (i = base; i < base + n; i++) {
        uint8_t *e = DS_ENTRY_AT(d, i);
        int16_t  end = (int16_t)(DE_W(e, DE_AT) + DE_B(e, DE_CHARS));

        if (TA_MARK_AT(ta, end) == 0
            && end < IC_COUNT_AT(in)
            && DE_B(e, DE_POS) != 0x75)
            TA_MARK_AT(ta, end) = 1;

        if (DE_B(e, DE_CHARS) >= 4 || DE_B(e, DE_KANALEN) >= 3)
            allShort = 0;
    }

    if (base + n >= DS_ENTRY_N)
        return -1;
    return allShort ? 1 : 0;
}

/* Whether the characters at `at' are the ones this number-counter record
   spells. */
int16_t ds_CompareJMD(void *d, uint8_t *p, int16_t at, int16_t n)
{
    uint8_t *in = DS_INPUT(d);
    int16_t  i;

    for (i = 0; i < n; i++, p += 2)
        if (ju_MakeUshort(IC_CHAR(in, at + i)) != ju_MakeUshort((char *)p))
            return 0;
    return 1;
}

/* Keep one counter's reading in the record array, where HandleError will read
   it back. */
void ds_SetJCC(void *d, const uint8_t *m, int16_t slot)
{
    uint8_t       *r = DS_REC_AT(d, slot);
    const uint8_t *kana = m + JM_TEXT + m[JM_CHARS] * 2;
    int16_t        i;

    *(int16_t *)(r + JC_ACCENT) = *(const int16_t *)(m + JM_KEY);
    r[JC_KANALEN] = m[JM_KANALEN];
    r[JC_CHARS] = m[JM_CHARS];
    for (i = 0; i < m[JM_KANALEN]; i++)
        r[JC_KANA + i] = kana[i];
}

/* Every number counter that starts at `at'.
 *
 * The counters are a flat run of self-delimiting records in order of their
 * first character, so the walk stops at the first one past it. The guard of
 * 600 is IBM's own and is what keeps a corrupt record from running away. */
int16_t ds_JoSuusiSearch(void *d, int16_t at)
{
    uint8_t       *in = DS_INPUT(d);
    const uint8_t *p = dm_GetNumJMDPtr();
    int16_t        count = 0;
    int16_t        guard = 1;
    uint16_t       key = ju_MakeUshort(IC_CHAR(in, at));

    for (;;) {
        uint8_t *q = (uint8_t *)p + JM_TEXT;
        uint16_t here = ju_MakeUshort((char *)q);

        if (here == key) {
            if (ds_CompareJMD(d, q, at, (int16_t)p[JM_CHARS]) == 1) {
                ds_SetJCC(d, p, count);
                count++;
            }
        } else if (key <= here) {
            break;
        }
        guard++;
        if (guard > 0x258)
            break;
        p = p + JM_TEXT + p[JM_CHARS] * 2 + p[JM_KANALEN];
    }
    return count;
}

/* What to do with a character no dictionary knew.
 *
 * Whatever kind it is, something is written, because a sentence with a hole in
 * it has no path through it at all. A katakana run is analysed unless what has
 * already been found is good enough; a full-width letter goes through the
 * English rules; hiragana is spelled out; the long-vowel bar is a placeholder
 * unless a kana came before it, in which case it belongs to that and nothing
 * is written; and anything else is a placeholder.
 *
 * Then the number counters, which are their own small dictionary and only
 * looked at where the parse marks say a number ended here.
 *
 * Last, the character itself is handed back through `out' when nothing
 * readable came of any of it -- which is what the caller says aloud instead.
 *
 * Answers how many candidates there are now, or one past that where the array
 * filled. */
int16_t ds_HandleError(void *d, int16_t at, int16_t written, int16_t base,
                       char *out)
{
    uint8_t *in = DS_INPUT(d);
    uint8_t *ta = DS_OWNER_OF(d);
    uint8_t *e = DS_ENTRY_AT(d, base + written);
    int32_t  marked = 0;
    int32_t  allEmpty;
    int16_t  i;

    switch (IC_KIND_AT(in, at)) {

    case KIND_KATAKANA:
        if (!ds_NeedKatakanaAnalysis(d, base, written))
            break;
        /* Where nothing has been found yet, a mark further along the text
           says some other candidate already covers this. */
        if (written == 0)
            for (i = (int16_t)(at + 1); i < IC_COUNT_AT(in); i++)
                if (TA_MARK_AT(ta, i) != 0) {
                    marked = 1;
                    break;
                }
        if (marked == 0) {
            ds_ProcessKatakana(d, at, e);
            if (ds_CheckJrtTable(d, (int16_t)(base + written), 1) < 0)
                return (int16_t)(written + 1);
            written++;
        }
        break;

    case KIND_LATIN:
    case KIND_ENGWORD:
        ds_ProcessRomanAlphabet(d, at, e);
        if (ds_CheckJrtTable(d, (int16_t)(base + written), 1) < 0)
            return (int16_t)(written + 1);
        written++;
        break;

    case KIND_HIRAGANA:
        ds_ProcessHiragana(d, at, e);
        if (base + written + 1 >= DS_ENTRY_N)
            return (int16_t)(written + 1);
        written++;
        break;

    case KIND_DIGIT:
    case KIND_KANJI:
        break;

    case KIND_CHOON:
        /* A bar after a kana belongs to that kana and is not its own word. */
        if (at != 0 && at > 0
            && (IC_KIND_AT(in, at - 1) == KIND_KATAKANA
                || IC_KIND_AT(in, at - 1) == KIND_HIRAGANA))
            break;
        ds_SetDummySymbol(d, at, e);
        DE_B(e, DE_POS) = 9;
        if (ds_CheckJrtTable(d, (int16_t)(base + written), 1) < 0)
            return (int16_t)(written + 1);
        written++;
        break;

    default:
        ds_SetDummySymbol(d, at, e);
        if (ds_CheckJrtTable(d, (int16_t)(base + written), 1) < 0)
            return (int16_t)(written + 1);
        written++;
        break;
    }

    /* And the number counters, where a number ended here. */
    if (TA_MARK_AT(ta, at) == 3) {
        int16_t found;

        e = DS_ENTRY_AT(d, written + base);
        found = ds_JoSuusiSearch(d, at);
        for (i = 0; i < found; i++) {
            uint8_t *r = DS_REC_AT(d, i);
            int16_t  j;

            DE_W(e, DE_ACCENT) = *(int16_t *)(r + JC_ACCENT);
            DE_B(e, DE_KANALEN) = r[JC_KANALEN];
            DE_B(e, DE_CHARS) = r[JC_CHARS];
            for (j = 0; j < 9; j++)
                DE_B(e, DE_KANA + j) = r[JC_KANA + j];
            DE_B(e, DE_POS) = 0x7d;
            DE_W(e, DE_AT) = at;
            DE_L(e, DE_MARK) = IC_MARK_AT(in, at);
            DE_W(e, DE_OFFSET) = IC_OFFSET_AT(in, at);
            DE_L(e, DE_COST) = 0;

            if (base + written + 1 >= DS_ENTRY_N)
                return (int16_t)(written + 1);
            written++;
            e = DS_ENTRY_AT(d, written + base);
        }
    }

    TA_MARK_AT(ta, at) = 0;

    allEmpty = 1;
    for (i = base; i < base + written; i++)
        if (DE_B(DS_ENTRY_AT(d, i), DE_KANALEN) != 0) {
            allEmpty = 0;
            break;
        }

    /* Nothing readable came of it: hand the character itself back, which is
       what the caller will say aloud instead of a reading. */
    if (allEmpty && marked == 0)
        ju_DbCpy(out, IC_CHAR(in, at));
    else
        ju_DbSet(out, 0, 0);
    return written;
}

/* ---- numbers ----------------------------------------------------------- */

/* A run of digits is not a word any dictionary holds, so it is read by rule
 * instead: this is that rule, and it is the last part of DictSearch that
 * `Do` needs.
 *
 * A Japanese number is spoken by its places rather than by its digits -- the
 * places are ten, hundred, thousand and then the four-digit steps man, oku and
 * chou -- so what the reader must work out is not which digits are there but
 * which place words go with them, and whether what is written is a number at
 * all. Four tables in the counter data say which characters are which: the
 * kanji digits, the full-width digits, the small places and the large ones.
 *
 * The codes these leave behind are not kana. They are the numbers the reading
 * rules further down take: nought to nine for the digits, ten to twelve and
 * nineteen upwards for the places, and 0x1a for the mark that says a place
 * word was left out and must be spoken anyway. */

/* Where each of the four tables sits in the counter data, and how many
 * two-byte entries it has. Read off the four callers rather than guessed. */
#define NUM_KANJI_AT    0x00     /* the kanji digits, zero to nine */
#define NUM_KANJI_N     10
#define NUM_FULL_AT     0x14     /* the full-width digits */
#define NUM_FULL_N      10
#define NUM_KETA_AT     0x28     /* ten, hundred, thousand and the rest */
#define NUM_KETA_N      9
#define NUM_SYMB_AT     0x3c     /* the counters that may follow a number */
#define NUM_SYMB_N      9

/* What the codes mean where they are not a digit. */
#define NUM_MISSING     0x1a     /* a place word that was not written */
#define NUM_SYMB_BASE   0x13     /* what a counter's index is offset by */
#define NUM_COMMA       0x18     /* the two marks a thousands separator
                                    makes */
#define NUM_COMMA2      0x1b

/* Which two-byte character of a table this is, or minus one. */
int16_t ds_IsMember(void *d, uint8_t *p, const uint8_t *table, int16_t n)
{
    int16_t i;

    (void)d;
    for (i = 0; i < n; i++)
        if (ju_DbCmp((const char *)p, (const char *)(table + i * 2)))
            return i;
    return -1;
}

int16_t ds_IsZKNum(void *d, uint8_t *p)
{
    return ds_IsMember(d, p, dm_GetNumberDataPtr() + NUM_KANJI_AT,
                       NUM_KANJI_N);
}

int16_t ds_IsZSNum(void *d, uint8_t *p)
{
    return ds_IsMember(d, p, dm_GetNumberDataPtr() + NUM_FULL_AT, NUM_FULL_N);
}

int16_t ds_IsZKeta(void *d, uint8_t *p)
{
    return ds_IsMember(d, p, dm_GetNumberDataPtr() + NUM_KETA_AT, NUM_KETA_N);
}

int16_t ds_IsZSymb(void *d, uint8_t *p)
{
    return ds_IsMember(d, p, dm_GetNumberDataPtr() + NUM_SYMB_AT, NUM_SYMB_N);
}

/* Whether the thousands marks in a run of codes are in the wrong places.
 *
 * Two questions, and either one answers yes. Is there anything in the run that
 * is neither a digit nor a mark at all; and, reading backwards, does a mark
 * fall anywhere but every fourth place, or fail to fall there. Answers one for
 * wrong and nought for right, so the caller reads it as "these commas do not
 * group this number". */
int32_t ds_IsCommaPosition(void *d, char *p, int32_t n)
{
    int32_t i;
    int32_t k;

    (void)d;
    for (i = 0; i < n; i++) {
        if (p[i] <= 9)
            continue;
        if (p[i] == NUM_COMMA || p[i] == NUM_COMMA2)
            continue;
        return 1;
    }

    for (i = n - 1, k = 1; i >= 0; i--, k++) {
        int32_t mark = (p[i] == NUM_COMMA || p[i] == NUM_COMMA2);

        if (k % 4 == 0) {
            if (!mark)
                return 1;
        } else if (mark) {
            return 1;
        }
    }
    return 0;
}

/* Whether the character at an index closes a quotation or a bracket. Only the
 * second byte is looked at, the first having been settled by the kind. */
int32_t ds_IsEndOfQuote(void *d, int16_t at)
{
    uint8_t *in = DS_INPUT(d);
    uint8_t  c;

    if (IC_KIND_AT(in, at) != KIND_PUNCT)
        return 0;
    c = (uint8_t)IC_CHAR(in, at)[1];
    return c == 0x43 || c == 0x44 || (c >= 0x46 && c <= 0x49)
           || c == 0x66 || c == 0x68 || c == 0x6a || c == 0x6e || c == 0x70
           || c == 0x72 || c == 0x74 || c == 0x76 || c == 0x78 || c == 0x7a;
}

/* Whether the places already read make a number, and what to do where they do
 * not.
 *
 * The digits and place words read so far are in buf. A place word out of order
 * -- a hundred after a thousand, say -- means what is being read is not one
 * number after all, and the counts are put back to where the last good one
 * ended. Where the order is right but a place word was left out, the mark that
 * says so is appended so that the reading rules speak it anyway.
 *
 * Answers one where the place word was appended and nought where the run was
 * given up on. Either way the counts it was given are saved for next time. */
int16_t ds_CheckKetaOrder(void *d, int16_t *n, int16_t *chars,
                          int16_t *keepN, int16_t *keepChars,
                          int16_t keta, uint8_t *buf)
{
    int16_t bad = 0;
    int16_t digits = 0;
    int16_t bigKeta = 0x63;
    int16_t smallKeta = 0x63;
    int16_t bigAt = -1;
    int16_t i;
    int16_t o;
    int16_t rc = 0;
    uint8_t tmp[20];

    if (keta >= 0 && keta <= 5) {
        for (i = 0; i < *n; i++)
            if (buf[i] >= 0xd && buf[i] <= 0xf) {
                bigKeta = buf[i];
                bigAt = i;
            }

        if (keta <= 2) {
            if (*n > 1) {
                for (i = (int16_t)(bigAt + 1); i < *n; i++)
                    if (buf[i] >= 0xa && buf[i] <= 0xc)
                        smallKeta = buf[i];
                if (smallKeta <= keta + 0xa) {
                    bad = 1;
                    *n = *keepN;
                    *chars = *keepChars;
                } else {
                    for (i = (int16_t)(*n - 2); i < *n; i++)
                        if (buf[i] <= 9)
                            digits++;
                    if (digits == 2) {
                        bad = 1;
                        buf[*n] = NUM_MISSING;
                        (*n)++;
                    }
                }
            } else if (*n == 1 && buf[0] == 0) {
                bad = 1;
                buf[*n] = NUM_MISSING;
                (*n)++;
            }
        } else {
            for (i = 0; i < *n; i++)
                if (buf[i] <= 9)
                    digits++;
            if (bigKeta <= keta + 0xa || bigAt == *n - 1) {
                bad = 1;
                *n = *keepN;
                *chars = *keepChars;
            } else if (digits > 24 - keta * 4) {
                bad = 1;
                buf[*n] = NUM_MISSING;
                (*n)++;
            }
        }
    }

    if (!bad) {
        /* The thousands marks come out before the place word goes in, but only
           where they were in the right places to begin with. */
        if (!ds_IsCommaPosition(d, (char *)buf, *n)) {
            o = 0;
            for (i = 0; i < *n; i++)
                if (buf[i] != NUM_COMMA && buf[i] != NUM_COMMA2)
                    tmp[o++] = buf[i];
            *n = o;
            for (o = 0; o < *n; o++)
                buf[o] = tmp[o];
        }
        rc = 1;
        buf[*n] = (uint8_t)(keta + 0xa);
        (*n)++;
    }
    *keepN = *n;
    *keepChars = *chars;
    return rc;
}

/* Read a run of digits and place words into one candidate entry.
 *
 * It walks forward from a character taking one at a time, and keeps two
 * counts: how far it has got, and how far it had got the last time what it had
 * was a whole number. When a character says the run is not a number after all,
 * the second pair is what the entry is written from -- so "three thousand and"
 * gives back the three thousand and leaves the rest.
 *
 * The counters that may follow a number -- the things a number counts, which
 * is what a Japanese counter word is -- are read here too, and which of them
 * it is decides whether the run goes on. That is what the switch over the nine
 * of them is. The romanizer's number mode changes it: mode two, which is what
 * an English-spelling caller gets, refuses a bare place word.
 *
 * Answers one where an entry was written and nought where the run was refused
 * outright. */
int16_t ds_SetSuushiWord(void *d, int16_t slot, int16_t at)
{
    uint8_t *in = DS_INPUT(d);
    uint8_t *e;
    uint8_t *ta;
    uint8_t *rom;
    uint8_t  buf[20];
    char     pair[2];
    uint16_t numMode = 0;
    int16_t  most;
    int16_t  n = 0, chars = 0;
    int16_t  keepN = 0, keepChars = 0;
    int16_t  backN = 0, backChars = 0;
    int16_t  digits = 0;
    int16_t  idx = 0, i;
    int16_t  lastSymb = -1;
    uint8_t  take = 1, prevTake = 0, state = 0, lastKind = 0, mode = 0x13;
    uint8_t  again = 0;

    if (slot >= DS_ENTRY_N)
        return 0;

    e = DS_ENTRY_AT(d, slot);
    ta = DS_OWNER_OF(d);
    rom = *(uint8_t **)(ta + TA_OWNER_AT);

    numMode = *(uint16_t *)(rom + RZ_NUMBER_MODE);
    if (numMode == 0 && *(int32_t *)(rom + RZ_SPELL_ENGLISH) > 0)
        numMode = 2;

    /* How many codes there is room for, which is fewer once the long-reading
       store is more than half full. */
    most = (int8_t)ta[TA_LONGWORDS] < 0x1e ? 0x10 : 9;

    memset(buf, 0x10, sizeof buf);

    while (take && n < most) {
        if (at + chars > IC_COUNT_AT(in))
            break;
        ju_DbCpy(pair, IC_CHAR(in, at + chars));
        chars++;
        prevTake = take;
        take = 0;
        again = 0;

        idx = ds_IsZKNum(d, (uint8_t *)pair);
        if (idx >= 0) {
            buf[n++] = (uint8_t)idx;
            if (lastKind != 0x11) {
                if (mode == 0x12 && buf[n - 2] <= 9) {
                    keepN = (int16_t)(n - 2);
                    keepChars = (int16_t)(chars - 2);
                } else {
                    take = 1;
                    keepN = n;
                    keepChars = chars;
                    lastKind = 0x10;
                }
            }
            continue;
        }

        idx = ds_IsZSNum(d, (uint8_t *)pair);
        if (idx >= 0) {
            buf[n++] = (uint8_t)idx;
            if (lastKind != 0x10 && mode != 0x12) {
                take = 1;
                keepN = n;
                keepChars = chars;
                lastKind = 0x11;
            }
            continue;
        }

        idx = ds_IsZKeta(d, (uint8_t *)pair);
        if (idx >= 0) {
            if (numMode == 2) {
                if (chars == 1 && idx >= 0 && idx <= 8)
                    return 0;
                continue;
            }
            if (chars == 1 && idx >= 3 && idx <= 5)
                return 0;

            if ((state == 1 && (lastSymb == 0 || lastSymb == 1))
                || state == 2) {
                if (state == 2) {
                    keepN = backN;
                    keepChars = backChars;
                }
            } else if (buf[0] == 0) {
                buf[n++] = NUM_MISSING;
                keepN = n;
                keepChars = chars;
            } else if (ds_CheckKetaOrder(d, &n, &chars, &keepN, &keepChars,
                                         idx, buf) == 1) {
                take = 2;
                mode = 0x12;
            }
            continue;
        }

        idx = ds_IsZSymb(d, (uint8_t *)pair);
        if (idx < 0)
            continue;

        if (numMode != 0 && numMode != 2)
            continue;

        buf[n++] = (uint8_t)(idx + NUM_SYMB_BASE);
        lastSymb = idx;

        if (prevTake != 3) {
            switch (idx) {
            case 0: case 1: case 2:
                if (numMode == 2 && idx == 2)
                    break;
                if (state == 1) {
                    take = 0;
                } else {
                    mode = 0x13;
                    take = 3;
                    state = 1;
                }
                again = 1;
                break;
            case 3: case 5: case 8:
                take = 3;
                state = 1;
                again = 1;
                break;
            case 4: case 6:
                if (state == 1) {
                    take = 0;
                } else {
                    state = 2;
                    if (mode != 0x12) {
                        take = 3;
                        backN = (int16_t)(n - 1);
                        backChars = (int16_t)(chars - 1);
                    }
                }
                again = 1;
                break;
            case 7:
                state = 2;
                if (mode != 0x12) {
                    keepN = n;
                    keepChars = chars;
                    backN = (int16_t)(n - 1);
                    backChars = (int16_t)(chars - 1);
                    take = 0;
                }
                again = 1;
                break;
            default:
                break;
            }
            if (again && chars == most)
                keepChars = chars;
        }

        if (numMode == 1) {
            buf[n++] = (uint8_t)(idx + NUM_SYMB_BASE);
            lastSymb = idx;
            if (prevTake != 3) {
                if (idx == 5 || idx == 8) {
                    take = 3;
                    state = 1;
                    again = 1;
                }
                if (again && chars == most)
                    keepChars = chars;
            }
        }
    }

    /* A run that filled the buffer is cut back to the last large place word in
       it, so that what is written is a number rather than the first sixteen
       codes of one. */
    digits = 0;
    if (n == 0x10) {
        for (i = 0; i < 0x10; i++)
            if (buf[i] > 0xc && buf[i] < 0x10)
                digits = (int16_t)(i + 1);
        if (digits != 0) {
            keepN = digits;
            keepChars = digits;
        }
    }

    if (keepN > 9)
        ds_SetLongWord(d, keepN, e, buf);
    else
        for (i = 0; i < keepN; i++)
            DE_B(e, DE_KANA + i) = buf[i];

    DE_B(e, DE_KANALEN) = (uint8_t)keepN;
    DE_B(e, DE_CHARS) = (uint8_t)keepChars;
    if (buf[keepN - 1] == NUM_MISSING)
        DE_B(e, DE_CHARS)--;
    DE_B(e, DE_POS) = 0x7c;
    DE_W(e, DE_AT) = at;
    DE_L(e, DE_MARK) = IC_MARK_AT(in, at);
    DE_W(e, DE_OFFSET) = IC_OFFSET_AT(in, at);
    ta[TA_MARKS + (int16_t)(keepChars + at)] = 3;
    return 1;
}

/* The user dictionary, which the search reaches through the romanizer two
 * objects up rather than holding itself. */
struct RomUserDict *ds_getPtrOfUserDict(void *d)
{
    uint8_t *rom = *(uint8_t **)(DS_OWNER_OF(d) + TA_OWNER_AT);

    return *(struct RomUserDict **)(rom + RZ_USERDICT_AT);
}

/* A candidate that stands for a character nothing could read, so that the path
 * search has something to step over. */
int16_t ds_SetDummyWord(void *d, int16_t slot, int16_t at)
{
    uint8_t *in = DS_INPUT(d);
    uint8_t *e = DS_ENTRY_AT(d, slot);

    DE_B(e, DE_KANA) = 0xff;
    DE_B(e, DE_CHARS) = 0;
    DE_B(e, DE_KANALEN) = 0;
    DE_B(e, DE_POS) = 0;
    DE_W(e, DE_AT) = at;
    DE_W(e, DE_OFFSET) = IC_OFFSET_AT(in, at);
    DE_B(e, DE_ATTR) = 0;
    DE_B(e, DE_ATTR2) = 0x41;
    DE_L(e, DE_MARK) = IC_MARK_AT(in, at);
    return 1;
}

/* ---- the search itself ------------------------------------------------- */

/* One candidate copied over another, which the two filters below both do.
 * Everything but the link is carried across, the reading last because how much
 * of it there is comes from the entry being copied. */
static void copyEntry(void *d, int16_t slot, int32_t from, int32_t to)
{
    uint8_t *src = DS_ENTRY_AT(d, (int16_t)(slot + from));
    uint8_t *dst = DS_ENTRY_AT(d, (int16_t)(slot + to));
    int32_t  i;

    DE_B(dst, DE_POS)     = DE_B(src, DE_POS);
    DE_B(dst, DE_ATTR)    = DE_B(src, DE_ATTR);
    DE_B(dst, DE_ATTR2)   = DE_B(src, DE_ATTR2);
    DE_W(dst, DE_AT)      = DE_W(src, DE_AT);
    DE_L(dst, DE_MARK)    = DE_L(src, DE_MARK);
    DE_W(dst, DE_OFFSET)  = DE_W(src, DE_OFFSET);
    DE_L(dst, DE_COST)    = DE_L(src, DE_COST);
    DE_B(dst, DE_CHARS)   = DE_B(src, DE_CHARS);
    DE_B(dst, DE_KANALEN) = DE_B(src, DE_KANALEN);
    DE_W(dst, DE_ACCENT)  = DE_W(src, DE_ACCENT);
    for (i = 0; i < DE_B(src, DE_KANALEN); i++)
        DE_B(dst, DE_KANA + i) = DE_B(src, DE_KANA + i);
}

/* Every way of reading the sentence, written into the candidate array.
 *
 * This is what the whole of the rest of DictSearch is for. It walks the text
 * one character at a time and, at each place a parse mark says a word may
 * begin, asks every dictionary there is: the caller's own taught words, the
 * built-in one loaded from a file, the function words, the single kanji, the
 * ordinary words, and the English rules. What comes back is a run of candidate
 * entries beginning at the slot the last character finished on, and JPath
 * afterwards picks a way through them.
 *
 * Three things are worth knowing before reading it.
 *
 * A character with no parse mark is skipped outright. The marks are written by
 * CheckJrtTable and by SetSuushiWord as the walk goes, so the walk is deciding
 * as it goes which characters can still start a word; a character no candidate
 * reached is not a place a word may begin.
 *
 * The caller's own text can carry marks of its own, and the byte it holds at
 * this character's place becomes the mode. One means the caller gave a reading
 * for this stretch -- the SNLK chain -- and two means leave the stretch alone
 * altogether. In mode one the candidates are filtered twice afterwards, first
 * to those whose reading matches what the caller asked for and then, if more
 * than one survives, to those the caller's own dictionary produced.
 *
 * And an annotation that names a parameter takes effect here rather than on
 * the output side, because here is where the text position and the annotation
 * position are both known.
 *
 * Answers how many candidates the last character produced, or a negative from
 * the user-dictionary lookup. */
int16_t ds_Do(void *d)
{
    uint8_t     *in = DS_INPUT(d);
    uint8_t     *ta = DS_OWNER_OF(d);
    uint8_t     *rom = *(uint8_t **)(ta + TA_OWNER_AT);
    Annotation  *anno = *(Annotation **)(ta + TA_ANNOTATION_AT);
    const char  *got = NULL;
    const char  *lastGot = NULL;
    char         want[2];
    int16_t      slot = 0;
    int16_t      n = 0;
    int16_t      at;
    int16_t      dummy = 0;
    int16_t      rc;
    int32_t      i;

    want[0] = 0;
    want[1] = want[0];

    memset(DS_AT(d, DS_ENTRY), 0, DS_ENTRY_N * DS_ENTRY_SIZE);
    for (i = 0; i < DS_ENTRY_N; i++)
        DE_W(DS_ENTRY_AT(d, (int16_t)i), DE_AT) = -1;
    DS_W(d, DS_COUNT) = 0;

    for (at = 0; at < IC_COUNT_AT(in); at++, n = 0) {
        const char   *raw;
        RomUserDict  *udict;
        uint8_t      *node;
        int32_t       kind;
        int32_t       j, k, kept;

        /* An annotation standing before this character, taken once. */
        if ((int8_t)anno->count > 0) {
            got = an_GetLastAnno(anno, IC_OFFSET_AT(in, at), 0);
            if (got != NULL && got != lastGot) {
                if (rp_isAnnotationsInText(*(RomInstParam **)
                                           (rom + RZ_PARAM_AT)))
                    rz_GetParameter(rom,
                                    (char *)(uintptr_t)(const void *)got);
                lastGot = got;
            }
        }

        /* What the caller's own text says about this character. */
        raw = *(const char **)(ta + TA_RAW_AT);
        DS_L(d, DS_USERDICT_MODE) =
            (uint8_t)raw[(int16_t)(IC_LENGTH_AT(in)
                                   + IC_OFFSET_AT(in, at))];
        if (DS_L(d, DS_USERDICT_MODE) == 1) {
            DS_P(d, DS_USERDICT_WORD) =
                ic_GetSnlkTableAt(in, IC_OFFSET_AT(in, at));
            if (DS_P(d, DS_USERDICT_WORD) == NULL)
                DS_L(d, DS_USERDICT_MODE) = 0;
        }
        node = (uint8_t *)DS_P(d, DS_USERDICT_WORD);

        if (ta[TA_MARKS + at] == 0 || DS_L(d, DS_USERDICT_MODE) == 2
            || ju_DbCmp(IC_CHAR(in, at), want))
            continue;

        dummy = 0;
        if (at == 0)
            n = ds_SetDummyWord(d, slot, 0);
        if (at > 0 && ds_IsEndOfQuote(d, (int16_t)(at - 1))) {
            n = ds_SetDummyWord(d, slot, at);
            dummy = 1;
        }

        if (DS_L(d, DS_USERDICT_MODE) == 0
            || (DS_L(d, DS_USERDICT_MODE) == 1
                && SN_B(node, SN_TRANS) == 0xff)) {
            if (DS_L(d, DS_USERDICT_MODE) == 0
                && IC_KIND_AT(in, at) == KIND_DIGIT)
                n = (int16_t)(n + ds_SetSuushiWord(d, (int16_t)(slot + n),
                                                   at));

            udict = ds_getPtrOfUserDict(d);
            kind = IC_KIND_AT(in, at);
            if (*(int32_t *)(rom + RZ_SPELL_ENGLISH) == 0
                || (kind != KIND_LATIN && kind != KIND_ENGWORD)) {
                if (udict->dict != NULL) {
                    rc = rud_lookup(udict, (uint8_t *)IC_CHAR(in, at), at,
                                    (int16_t)(slot + n));
                    if (rc < 0)
                        return rc;
                    n = (int16_t)(n + rc);
                }
                /* The dictionary loaded from a file, which this port has no
                   way of setting: both pointers stay null. */
                if (dm_s_paUserDict != NULL)
                    n = (int16_t)(n + ds_LookupUserDict(d, dm_s_paUserDict,
                                                        IC_CHAR(in, at),
                                                        (int16_t)(slot + n),
                                                        dm_s_paUserDictIdx,
                                                        at,
                                                        IC_COUNT_AT(in)));
            }

            if (DS_L(d, DS_USERDICT_MODE) == 0)
                n = (int16_t)(n + ds_LookupFuncWordDict(d,
                                                        (int16_t)(slot + n),
                                                        at));
            n = (int16_t)(n + ds_LookupTankanDict(d, (int16_t)(slot + n), at));
            n = (int16_t)(n + ds_LookupNormalWordDict(d, (int16_t)(slot + n),
                                                      at, 0));
            if (*(int32_t *)(rom + RZ_SPELL_ENGLISH) == 0)
                n = (int16_t)(n
                              + ds_LookupEngWordDictFromText(d,
                                                    (int16_t)(slot + n), at));

            /* In mode one, keep only what the caller asked for. */
            if (DS_L(d, DS_USERDICT_MODE) == 1) {
                j = (at == 0 || dummy == 1) ? 1 : 0;
                k = j;
                kept = 0;
                for (; j < n; j++) {
                    uint8_t *src = DS_ENTRY_AT(d, (int16_t)(slot + j));

                    if (!ju_YomiCmp(SN_P(node, SN_YOMI),
                                    SN_B(node, SN_YOMI_N),
                                    &DE_B(src, DE_KANA),
                                    DE_B(src, DE_KANALEN)))
                        continue;
                    kept++;
                    copyEntry(d, slot, j, k);
                    k++;
                }
                DE_W(DS_ENTRY_AT(d, (int16_t)(slot + k)), DE_AT) = -1;
                n = (int16_t)kept;
                if (at == 0 || dummy == 1)
                    n++;

                /* And of those, the ones the caller's own dictionary made. */
                if (n > 1) {
                    j = (at == 0 || dummy == 1) ? 1 : 0;
                    k = j;
                    kept = 0;
                    for (; j < n; j++) {
                        uint8_t *src = DS_ENTRY_AT(d, (int16_t)(slot + j));

                        if (DE_B(src, DE_POS) != 0x7e)
                            continue;
                        kept++;
                        copyEntry(d, slot, j, k);
                        k++;
                    }
                    if (kept > 0) {
                        DE_W(DS_ENTRY_AT(d, (int16_t)(slot + k)),
                             DE_AT) = -1;
                        n = (int16_t)kept;
                        if (at == 0 || dummy == 1)
                            n++;
                    } else {
                        n = 1;
                    }
                }
            }
        }

        /* Where the caller gave a reading and nothing else was found, the
           reading itself becomes the candidate. */
        if (DS_L(d, DS_USERDICT_MODE) == 1
            && ((at == 0 && n == 1) || (dummy == 1 && n == 1) || n == 0)) {
            uint8_t *e = DS_ENTRY_AT(d, (int16_t)(slot + n));

            DE_B(e, DE_CHARS) = SN_B(node, SN_CHARS);
            DE_B(e, DE_KANALEN) = SN_B(node, SN_YOMI_N);
            if (DE_B(e, DE_KANALEN) > 9) {
                if ((int8_t)ta[TA_LONGWORDS] >= 0x1e) {
                    for (i = 0; i < 9; i++)
                        DE_B(e, DE_KANA + i) = SN_B(node, SN_YOMI + i);
                } else {
                    ds_SetLongWord(d, DE_B(e, DE_KANALEN), e,
                                   SN_P(node, SN_YOMI));
                }
            } else {
                for (i = 0; i < DE_B(e, DE_KANALEN); i++)
                    DE_B(e, DE_KANA + i) = SN_B(node, SN_YOMI + i);
            }
            DE_W(e, DE_ACCENT) = SN_B(node, SN_TRANS) != 0xff
                                 ? SN_B(node, SN_TRANS) : 0;
            DE_B(e, DE_ATTR) = 0x58;
            DE_B(e, DE_ATTR2) = 1;
            DE_B(e, DE_POS) = 0x7e;
            DE_W(e, DE_AT) = at;
            DE_L(e, DE_MARK) = IC_MARK_AT(in, at);
            DE_W(e, DE_OFFSET) = IC_OFFSET_AT(in, at);
            DE_L(e, DE_COST) = 10;
            n++;
            DE_W(DS_ENTRY_AT(d, (int16_t)(slot + n)), DE_AT) = -1;
        }

        rc = ds_CheckJrtTable(d, slot, n);
        if (rc < 0) {
            DS_W(d, DS_COUNT) = (int16_t)(slot + n);
            return n;
        }

        /* A character the marks say is the second of a pair gets the run
           analysed as a whole, which is what GenerateWord is for. */
        if (DS_L(d, DS_USERDICT_MODE) == 0 && rc == 1
            && ta[TA_MARKS + at] == 2
            && (IC_KIND_AT(in, at) == KIND_KANJI
                || IC_KIND_AT(in, at) == KIND_HIRAGANA
                || IC_KIND_AT(in, at) == KIND_KATAKANA)
            && dummy == 0) {
            n = (int16_t)(n + ds_GenerateWord(d, at, (int16_t)(slot + n)));
            rc = ds_CheckJrtTable(d, slot, n);
            if (rc < 0) {
                DS_W(d, DS_COUNT) = (int16_t)(slot + n);
                return n;
            }
        }

        if (DS_L(d, DS_USERDICT_MODE) == 0)
            n = ds_HandleError(d, at, n, slot, want);

        DS_W(d, DS_COUNT) = (int16_t)(slot + n);
        slot = DS_W(d, DS_COUNT);
    }
    return n;
}

/* ---- the function words read backwards -------------------------------- */

/* The four methods here are the other half of the function-word search, and
 * they are the last of DictSearch.
 *
 * A Japanese phrase ends in a run of function words -- particles, endings,
 * auxiliaries -- and which of them may follow which is not free: the
 * dictionary carries a vector per word saying what kind of thing can come
 * next, and a run is only a run where every step agrees with the one before.
 * ds_SearchFuncWordDict walks that forwards from a place a word was found.
 * These walk it from a character nothing was found at, which is what happens
 * when the analyser has to guess where a phrase ends.
 *
 * The two Parsing methods are the same shape: seed the vector, search once,
 * and then keep searching from the end of everything found in the last round
 * until a round finds nothing. What each found is remembered in FZ_MARK as
 * the index of the entry it grew out of, so the chain can be walked back.
 * FzkParsing goes forward through the ordinary dictionary and
 * FzkParsingReverse backwards through the one with the phrase vectors in it.
 *
 * Nothing in the objects read so far calls either Parsing method; they are
 * entry points for a class above that is not written yet.
 */

#define FZ_UW(f, off)      (*(uint16_t *)((uint8_t *)(f) + (off)))

/* The five characters that stop a function word beginning a phrase: the small
 * tsu, the three small y kana and n. Read out of the object rather than
 * decoded from the names MSVC filed them under. */
static const char SMALL_TSU[] = "\x82\xc1";
static const char SMALL_YA[]  = "\x82\xe1";
static const char SMALL_YU[]  = "\x82\xe3";
static const char SMALL_YO[]  = "\x82\xe5";
static const char KANA_N[]    = "\x82\xf1";
static const char HIRAGANA_A[] = "\x82\xa0";

/* One dictionary node against the vector of what may follow, and a candidate
 * function word written for every bit that agrees.
 *
 * The flag it leaves says whether the word may begin a phrase of its own: the
 * dictionary's own bit says it may, and then the character after it is looked
 * at, because none of the five that join the sound before them can start
 * one. */
int16_t ds_HitFuncWordReverse(void *d, const uint8_t *head, int16_t slot,
                              uint16_t at, int16_t count, uint8_t chars,
                              uint8_t hiragana, uint8_t *vec,
                              const uint8_t *base)
{
    uint8_t       *in = DS_INPUT(d);
    const uint8_t *p;
    uint16_t       next;
    int16_t        found = 0;
    int16_t        i, k;

    if (at + 1 < IC_COUNT_AT(in))
        next = ju_MakeUshort(IC_CHAR(in, (int16_t)(at + 1)));
    else
        next = 0;

    p = head + 5;
    for (i = 0; i < count; i++) {
        for (k = 0; k < 14; k++) {
            uint8_t  key = p[1];
            int16_t  row = (int16_t)((key - 1) / 8);
            int16_t  mask;
            uint8_t *f;

            if (row != k)
                continue;
            mask = (int16_t)(0x80 >> ((key - 1) % 8));
            if ((mask & vec[k]) == 0)
                continue;

            f = DS_FZK_AT(d, slot);
            FZ_B(f, FZ_CHARS) = chars;
            FZ_B(f, FZ_HIRAGANA) = hiragana;
            FZ_W(f, FZ_KEY) = p[1];
            FZ_W(f, FZ_WORD) = (int16_t)(p - base);
            FZ_W(f, FZ_AT) = (int16_t)at;
            FZ_W(f, FZ_OFFSET) =
                IC_OFFSET_AT(in, (int16_t)(at - chars + 1));
            if ((p[3] & 0x80)
                && next != ju_MakeUshort(SMALL_TSU)
                && next != ju_MakeUshort(SMALL_YA)
                && next != ju_MakeUshort(SMALL_YU)
                && next != ju_MakeUshort(SMALL_YO)
                && next != ju_MakeUshort(KANA_N))
                FZ_B(f, FZ_FLAGS) = 1;
            else
                FZ_B(f, FZ_FLAGS) = 0;
            found++;
            slot++;
            break;
        }
        p = p + p[0];
    }
    return found;
}

/* Every function word that can begin at a character, following the chain of
 * nodes the dictionary hangs off that character's first byte.
 *
 * The dictionary is indexed by the hiragana itself: a character below the
 * first hiragana has no chain at all, and everything from the last one upward
 * shares one. Each node holds the character it is for, how many words hang off
 * it, and two lengths -- one to the next node for this character and one to
 * the next character's -- so the walk is a comparison and a step. */
int16_t ds_FzkSearchUnknown(void *d, uint8_t *vec, uint16_t at, int16_t slot,
                            const uint8_t *dict, int16_t unused)
{
    uint8_t       *in = DS_INPUT(d);
    const uint8_t *p;
    uint16_t       key;
    uint16_t       off;
    int16_t        found = 0;
    uint8_t        chars = 1;
    uint8_t        hiragana = 0;
    uint8_t        done = 0;

    (void)unused;
    if (slot >= DS_FZK_N)
        return 0;

    key = ju_MakeUshort(IC_CHAR(in, (int16_t)at));
    if (IC_KIND_AT(in, (int16_t)at) == KIND_HIRAGANA)
        hiragana++;

    if (key < ju_MakeUshort(HIRAGANA_A))
        return found;

    if (key < 0x82ff)
        off = ju_MakeUshort((const char *)(dict
                            + (uint16_t)(key - ju_MakeUshort(HIRAGANA_A) + 1)
                              * 2));
    else
        off = ju_MakeUshort((const char *)(dict + 0xa6));

    if (off == 0xffff || off == 0)
        return found;

    p = dict + 0xa8 + off;
    while (!done) {
        uint16_t here = (uint16_t)((p[0] << 8) + p[1]);

        if (key == here) {
            int16_t n = (int16_t)(p[3] >> 4);

            if (n != 0) {
                int16_t got = ds_HitFuncWordReverse(d, p, slot, at, n, chars,
                                                    hiragana, vec, dict);

                slot = (int16_t)(slot + got);
                found = (int16_t)(found + got);
            }
            if (p[2] == 0)
                done = 1;
            else
                p = p + 2 + p[2];
            at++;
            if (at < (uint16_t)IC_COUNT_AT(in)) {
                key = ju_MakeUshort(IC_CHAR(in, (int16_t)at));
                if (IC_KIND_AT(in, (int16_t)at) == KIND_HIRAGANA)
                    hiragana++;
                chars++;
            } else {
                key = 0;
            }
        } else if (key > here) {
            uint16_t step = (uint16_t)(((p[3] & 0xf) << 8) + p[4]);

            if (step == 0)
                done = 1;
            else
                p = p + 3 + step;
        } else {
            done = 1;
        }
    }
    return found;
}

/* A run of function words from a place, round after round, until a round adds
 * nothing. Each round starts where the last one's finds ended. */
int16_t ds_FzkParsing(void *d, uint8_t *vec, int16_t at)
{
    int16_t n;
    int16_t i, k;
    int16_t zero = 0;
    int16_t total = 0;
    int16_t first;
    int16_t last;
    uint8_t buf[14];

    for (i = 0; i < DS_FZK_N; i++) {
        FZ_B(DS_FZK_AT(d, i), FZ_MARK) = 0xff;
        FZ_B(DS_FZK_AT(d, i), FZ_FLAGS) = 0;
    }

    n = ds_SearchFuncWordDict(d, vec, at, total, dm_GetFuncDictEx(), zero);
    if (n == 0)
        return n;

    total = n;
    first = 0;
    last = n;

    for (;;) {
        for (i = first; i < last; i++) {
            int16_t row;
            int16_t bit;

            for (k = 0; k < 14; k++)
                buf[k] = 0;
            row = (int16_t)((FZ_W(DS_FZK_AT(d, i), FZ_KEY) - 1) / 8);
            bit = (int16_t)((FZ_W(DS_FZK_AT(d, i), FZ_KEY) - 1) % 8);
            if (row >= 14)
                continue;
            buf[row] = (uint8_t)(0x80 >> bit);
            at = (int16_t)(FZ_UW(DS_FZK_AT(d, i), FZ_AT) + 1);
            n = ds_SearchFuncWordDict(d, buf, at, total, dm_GetFuncDictEx(),
                                      zero);
            if (n > 0) {
                for (k = total; k <= total + n - 1; k++)
                    FZ_B(DS_FZK_AT(d, k), FZ_MARK) = (uint8_t)i;
                total = (int16_t)(total + n);
                if (total > DS_FZK_N)
                    return total;
            }
        }
        first = last;
        last = total;
        if (first >= last)
            return last;
    }
}

/* The same walk backwards through the dictionary that carries the phrase
 * vectors, which is what the analyser uses where it has to guess. The vector
 * is seeded from one entry of that dictionary's own table and then taken from
 * each word found, so that every round asks what may follow what was just
 * found. */
int16_t ds_FzkParsingReverse(void *d)
{
    uint8_t *in = DS_INPUT(d);
    uint8_t  vec[14];
    uint8_t  key;
    int16_t  n;
    int16_t  i, k;
    int16_t  at = 0;
    int16_t  total = 0;
    int16_t  zero = 0;
    int16_t  first;
    int16_t  last;

    key = dm_GetFuncDict()[0xb1];
    for (i = 0; i < 14; i++)
        vec[i] = dm_GetPhrVectorAt((uint16_t)((key - 1) * 14 + i));

    memset(DS_AT(d, DS_FZK), 0, DS_FZK_N * DS_FZK_SIZE);
    for (i = 0; i < DS_FZK_N; i++) {
        FZ_B(DS_FZK_AT(d, i), FZ_MARK) = 0xff;
        FZ_B(DS_FZK_AT(d, i), FZ_FLAGS) = 0xff;
    }

    n = ds_FzkSearchUnknown(d, vec, (uint16_t)at, total, dm_GetFuncDict(),
                            zero);
    if (n == 0)
        return n;

    total = n;
    first = 0;
    last = n;

    for (;;) {
        for (i = first; i < last; i++) {
            key = dm_GetFuncDict()[4 + FZ_W(DS_FZK_AT(d, i), FZ_WORD)];
            for (k = 0; k < 14; k++)
                vec[k] = dm_GetPhrVectorAt((uint16_t)((key - 1) * 14 + k));
            at = (int16_t)(FZ_UW(DS_FZK_AT(d, i), FZ_AT) + 1);
            if ((uint16_t)at < (uint16_t)IC_COUNT_AT(in))
                n = ds_FzkSearchUnknown(d, vec, (uint16_t)at, total,
                                        dm_GetFuncDict(), zero);
            else
                n = 0;
            if (n != 0) {
                for (k = total; k <= total + n - 1; k++)
                    FZ_B(DS_FZK_AT(d, k), FZ_MARK) = (uint8_t)i;
                total = (int16_t)(total + n);
                if (total >= DS_FZK_N)
                    return total;
            }
        }
        first = last;
        last = total;
        if (first >= last)
            return last;
    }
}
