/* The dictionary search, as far as it is written.
 *
 * This is the class the rest of the analyser leans on: it turns a stretch of
 * the input into candidate words with readings, and everything above it
 * chooses between what it produced. IBM spreads it over four objects --
 * dictsearch, dictapi, fdictapi and kanastr -- and sixty-four methods, and
 * twenty of them are here: the whole of what it takes to turn one run of text
 * into kana, which is the closure of GenerateWord and reaches nothing outside
 * this file that is not already written.
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
#define DS_P(d, off)       (*(void **)DS_AT(d, off))

#define DS_INPUT(d)        ((uint8_t *)DS_P(d, DS_INPUTCHAR))
#define DS_OWNER_OF(d)     ((uint8_t *)DS_P(d, DS_OWNER))

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

/* The particle wo, which marks the object of a verb. That is what the name
   means -- a case marker in the grammatical sense, not a typographic one -- and
   a run of text stops at it because what follows it is a new word.
 *
 * Read out of the object rather than decoded from the name it is stored
 * under: 0x82f0 is wo, and the mangled form `?$IC?p' reads as 0x8270 to
 * anybody working the encoding out by hand, which is a different character
 * altogether. test/romprims.sh is what caught that. */
static const char CASE_MARKER[] = "\x82\xf0";

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

/* Where the yomi table keeps the doubled vowels the long-vowel bar becomes. */
#define YOMI_CHOON      0x224

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
