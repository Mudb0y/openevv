/* The small things every other part of the romanizer asks.
 *
 * What a byte is -- a digit, a letter, half-width kana, the first or second
 * half of a two-byte character -- and what a two-byte character is: hiragana,
 * katakana, the long-vowel mark, a full stop of either width, a kanji numeral.
 * Then the pieces that turn a kana code into letters, which is the nearest
 * thing in the romanizer to romanisation itself, and a list splice the phrase
 * tables use.
 *
 * Two bytes at a time is the shape of most of it. `Db' in a name means one
 * two-byte character, so DbCmp compares two of them and DbCpy copies one; the
 * predicates that take a pointer rather than a byte are the ones that need
 * both halves.
 *
 * Three things in the original are written down here rather than smoothed
 * over, and one of them is a hang a caller can reach.
 *
 * Hiragana2Katakana scans its table for a match and does not advance the input
 * when it fails, so it walks the same character for ever. Its table holds
 * eighty-two hiragana and IsHiragana accepts eighty-three, and the one it
 * accepts that the table has not got is 0x82ec, hiragana small wa. That is a
 * hang on real text and it is not reproduced: the character is passed through
 * unchanged instead.
 *
 * The same scan's bound is the table's length in bytes rather than in entries
 * -- 492 where the entries are six bytes and there are eighty-two of them --
 * which only stays inside the table because a match always comes first. Ours
 * stops at the end of the table.
 *
 * And WriteRomajiStrBuf's row thirty is not a table at all: it makes the last
 * letter already written a capital, which is how this notation marks a variant
 * of the sound before it.
 *
 * test/romprims.sh holds all of it to IBM's own answer, over every byte and
 * every two-byte character.
 */

#include <stdio.h>
#include <string.h>
#include "jprom.h"

/* Which two-byte characters have names of their own here. */
#define KATAKANA_LEAD 0x83
#define HIRAGANA_LEAD 0x82
#define SYMBOL_LEAD   0x81

/* The four delimiters that are two bytes wide: the ideographic comma and full
   stop, and the full-width comma and full stop. */
static const char DELIM_IDEO_COMMA[] = "\x81\x41";
static const char DELIM_IDEO_STOP[]  = "\x81\x42";
static const char DELIM_WIDE_COMMA[] = "\x81\x43";
static const char DELIM_WIDE_STOP[]  = "\x81\x44";

/* Where the number data holds the kanji numerals, and how many there are. */
#define KANJI_NUM_AT 0x307
#define KANJI_NUM_N  0x13

/* Where the reading data holds what a voicing mark does to a kana. Each is a
   run of five, except the last two, which are one each. */
#define VOICED_K_AT   0x365   /* ka to ko */
#define VOICED_S_AT   0x36a   /* sa to so */
#define VOICED_T_AT   0x36f   /* ta to to */
#define VOICED_H_AT   0x374   /* ha to ho */
#define SEMI_H_AT     0x379   /* ha to ho, the half mark */
#define VOICED_U_AT   0x37e   /* u, which becomes vu */
#define KANA_PAIRS_AT 0x2e7   /* every half-width kana as two bytes */

/* One entry of the hiragana-to-katakana table: two bytes in, a byte of
   nothing, two bytes out, and a byte of nothing again. */
#define H2K_STRIDE 6
#define H2K_OUT    3

/* ---- files ---------------------------------------------------------- */

FILE *ju_ttsOpen(char *name, const char *mode)
{
    return fopen(name, mode);
}

void ju_ttsClose(FILE *f)
{
    if (f != 0)
        fclose(f);
}

long ju_ttsLseek(FILE *f, long to, long whence)
{
    return fseek(f, to, (int)whence);
}

long ju_ttsRead(FILE *f, char *buf, uint32_t n)
{
    return (long)fread(buf, 1, n, f);
}

/* The whole of a file from a given offset, into memory this allocates and
   hands back. A limit of nought means as much as there is. */
long ju_ttsReadAll(FILE *f, char **out, long from, uint32_t most)
{
    long   room;
    char  *buf;
    long   got;

    if (fseek(f, 0, SEEK_END) != 0)
        return 0;
    room = ftell(f) - from;
    if (most > 0 && (long)most <= room)
        room = (long)most;

    buf = (char *)cpp_new((uint32_t)(room + 1));
    if (buf != 0) {
        fseek(f, from, SEEK_SET);
        got = (long)fread(buf, 1, (size_t)room, f);
    } else {
        got = 0;
    }
    *out = buf;
    return got;
}

long ju_ttsWrite(FILE *f, const char *buf, uint32_t n)
{
    return (long)fwrite(buf, 1, n, f);
}

/* ---- two bytes at a time -------------------------------------------- */

/* A two-byte character as one number, the first byte high. */
uint16_t ju_MakeUshort(const char *p)
{
    return (uint16_t)(((uint32_t)(uint8_t)p[0] << 8) + (uint8_t)p[1]);
}

int32_t ju_DbCmp(const char *a, const char *b)
{
    return a[0] == b[0] && a[1] == b[1];
}

int32_t ju_DbCmp2(const char *a, char b0, char b1)
{
    return a[0] == b0 && a[1] == b1;
}

void ju_DbCpy(char *to, const char *from)
{
    to[0] = from[0];
    to[1] = from[1];
}

void ju_DbSet(char *to, char b0, char b1)
{
    to[0] = b0;
    to[1] = b1;
}

int32_t ju_TwoChCmp(char b0, char b1, char *p)
{
    return b0 == p[0] && b1 == p[1];
}

/* The two halves into two places of the caller's choosing. */
void ju_TwoChCpy(char *from, char *to0, char *to1)
{
    *to0 = from[0];
    *to1 = from[1];
}

/* ---- what a byte is ------------------------------------------------- */

int32_t ju_IsSBCSKana(char c)
{
    return (uint8_t)c > 0xa0 && (uint8_t)c < 0xe0;
}

int32_t ju_IsAlphaNumSym(char c)
{
    return (uint8_t)c > 0x20 && (uint8_t)c < 0x80;
}

int32_t ju_IsNum(char c)
{
    return c >= '0' && c <= '9';
}

int32_t ju_IsAlpha(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

int32_t ju_IsDBCSLeadByte(char c)
{
    return ((uint8_t)c >= 0x81 && (uint8_t)c <= 0x9f)
        || ((uint8_t)c >= 0xe0 && (uint8_t)c <= 0xfc);
}

int32_t ju_IsDBCSTrailByte(uint8_t c)
{
    return (c >= 0x40 && c <= 0x7e) || (c >= 0x80 && c <= 0xfc);
}

int32_t ju_IsValidDBCS(const char *p)
{
    return ju_IsDBCSLeadByte(p[0]) && ju_IsDBCSTrailByte((uint8_t)p[1]);
}

/* ---- what a two-byte character is ---------------------------------- */

int32_t ju_IsKatakana(const char *p)
{
    return (uint8_t)p[0] == KATAKANA_LEAD
        && (uint8_t)p[1] >= 0x40 && (uint8_t)p[1] <= 0x96;
}

int32_t ju_IsHiragana(const char *p)
{
    return (uint8_t)p[0] == HIRAGANA_LEAD
        && (uint8_t)p[1] >= 0x9f && (uint8_t)p[1] <= 0xf1;
}

int32_t ju_IsLongVowel(const char *p)
{
    return (uint8_t)p[0] == SYMBOL_LEAD && (uint8_t)p[1] == 0x5b;
}

/* What ends a run for the analyser: white space and the two stops of each
   width. */
int32_t ju_IsSNLKDelim(const char *p)
{
    if (p[0] == ' ' || p[0] == '\r' || p[0] == '\n' || p[0] == '\t'
        || p[0] == ',' || p[0] == '.')
        return 1;
    return ju_DbCmp(p, DELIM_IDEO_STOP) || ju_DbCmp(p, DELIM_IDEO_COMMA)
        || ju_DbCmp(p, DELIM_WIDE_STOP) || ju_DbCmp(p, DELIM_WIDE_COMMA);
}

int32_t ju_IsDBCSNum(const char *p)
{
    if (p == 0 || p[0] == 0)
        return 0;
    /* The full-width digits share hiragana's lead byte. */
    return (uint8_t)p[0] == 0x82
        && (uint8_t)p[1] >= 0x4f && (uint8_t)p[1] <= 0x58;
}

/* The nineteen kanji that stand for numbers, which the number data holds as
   two-byte characters in a row. */
int32_t ju_IsKanjiNum(const char *p)
{
    const uint8_t *data;
    int16_t        i;

    if (p == 0 || p[0] == 0)
        return 0;

    data = dm_GetNumberDataPtr();
    for (i = 0; i < KANJI_NUM_N; i++)
        if (ju_DbCmp(p, (const char *)(data + KANJI_NUM_AT + i * 2)))
            return 1;
    return 0;
}

/* ---- kana into letters ---------------------------------------------- */

/* One syllable's romaji appended to what is there already.
 *
 * The Q is the geminate marker -- a small tsu, which doubles the consonant
 * that follows -- so when one is sitting at the end of what has been written
 * it becomes the first letter of this syllable instead. It is looked for one
 * character back as well, because an apostrophe may have been put after it. */
void ju_GetRomaji(uint8_t *out, const uint8_t *table, int16_t col)
{
    size_t n = strlen((char *)out);

    if (n > 0 && out[n - 1] == 'Q')
        out[n - 1] = table[col * 5];
    else if (n > 1 && out[n - 1] == '\'' && out[n - 2] == 'Q')
        out[n - 2] = table[col * 5];

    strcat((char *)out, (const char *)(table + col * 5));
}

/* Row thirty writes no letters of its own. */
#define ROW_UPPER 30
#define ROW_V     31

/* And past the fifth column only row thirty-one says anything: the geminate
   marker and the syllabic n. */
#define COL_Q 5
#define COL_N 6

static const uint8_t *romajiTable(int row)
{
    switch (row) {
    case 0:  return jajp_k_romaji;
    case 1:  return jajp_ky_romaji;
    case 2:  return jajp_s_romaji;
    case 3:  return jajp_sh_romaji;
    case 4:  return jajp_t_romaji;
    case 5:  return jajp_ch_romaji;
    case 6:  return jajp_ts_romaji;
    case 7:  return jajp_p_romaji;
    case 8:  return jajp_py_romaji;
    case 9:  return jajp_h_romaji;
    case 10: return jajp_hy_romaji;
    case 11: return jajp_f_romaji;
    case 12: return jajp_n_romaji;
    case 13: return jajp_ny_romaji;
    case 14: return jajp_m_romaji;
    case 15: return jajp_my_romaji;
    case 16: return jajp_r_romaji;
    case 17: return jajp_ry_romaji;
    case 18: return jajp_y_romaji;
    case 19: return jajp_w_romaji;
    case 20: return jajp_g_romaji;
    case 21: return jajp_gy_romaji;
    case 22: return jajp_z_romaji;
    case 23: return jajp_j_romaji;
    case 24: return jajp_d_romaji;
    case 25: return jajp_b_romaji;
    case 26: return jajp_by_romaji;
    case 27: return jajp_ty_romaji;
    case 28: return jajp_fy_romaji;
    case 29: return jajp_dy_romaji;
    case ROW_V: return jajp_v_romaji;
    default: return 0;
    }
}

/* Make the letter already written a capital, which is what row thirty is for.
   An N already there is left alone, and an apostrophe is stepped over. */
static void upperLast(uint8_t *out)
{
    size_t n = strlen((char *)out);

    if (n > 0 && out[n - 1] == 'N')
        return;
    if (n > 0 && out[n - 1] != '\'')
        out[n - 1] = (uint8_t)(out[n - 1] - 0x20);
    else if (n > 1)
        out[n - 2] = (uint8_t)(out[n - 2] - 0x20);
}

int32_t ju_WriteRomajiStrBuf(uint8_t code, uint8_t *out)
{
    int16_t row = (int16_t)(code / 8);
    int16_t col = (int16_t)(code % 8);

    if (col >= 5) {
        if (row != ROW_V)
            return 1;
        if (col == COL_Q) {
            strcat((char *)out, "Q");
            return 1;
        }
        if (col == COL_N) {
            strcat((char *)out, "N");
            return 1;
        }
        return 0;
    }

    if (row > ROW_V)
        return 0;
    if (row == ROW_UPPER) {
        upperLast(out);
        return 1;
    }
    ju_GetRomaji(out, romajiTable(row), col);
    return 1;
}

/* ---- a voicing mark ------------------------------------------------- */

/* A half-width kana with a voicing mark after it, as one two-byte katakana.
   The five runs are the five rows a mark can change; anything else keeps the
   kana it already was. */
void ju_ConvertDakuten(char *out, uint8_t kana, uint8_t mark)
{
    const uint8_t *yomi;

    out[0] = (char)KATAKANA_LEAD;

    if (mark == 0xde) {
        yomi = dm_GetYomiDataPtr();
        if (kana >= 0xb6 && kana <= 0xba)
            out[1] = (char)yomi[VOICED_K_AT + (kana - 0xb6)];
        else if (kana >= 0xbb && kana <= 0xbf)
            out[1] = (char)yomi[VOICED_S_AT + (kana - 0xbb)];
        else if (kana >= 0xc0 && kana <= 0xc4)
            out[1] = (char)yomi[VOICED_T_AT + (kana - 0xc0)];
        else if (kana >= 0xca && kana <= 0xce)
            out[1] = (char)yomi[VOICED_H_AT + (kana - 0xca)];
        else if (kana == 0xb3)
            out[1] = (char)yomi[VOICED_U_AT];
        else
            ju_DbCpy(out, (const char *)(yomi + KANA_PAIRS_AT
                                         + (kana - 0xa1) * 2));
        return;
    }

    yomi = dm_GetYomiDataPtr();
    if (kana >= 0xca && kana <= 0xce)
        out[1] = (char)yomi[SEMI_H_AT + (kana - 0xca)];
    else
        ju_DbCpy(out, (const char *)(yomi + KANA_PAIRS_AT
                                     + (kana - 0xa1) * 2));
}

/* ---- hiragana into katakana ---------------------------------------- */

/* Every hiragana becomes its katakana, every other two-byte character is
   copied whole, and a single byte is copied as it is.
 *
 * The bound on the scan is the end of the table. IBM's is the table's length
 * in bytes, which is six times too many and only stays inside it because a
 * match always comes first -- except for 0x82ec, which IsHiragana accepts and
 * the table has not got, and which makes IBM's walk the same character for
 * ever. Here it falls through and is copied. */
void ju_Hiragana2Katakana(const uint8_t *in, uint8_t *out)
{
    const uint8_t *table = jajp_Hrgn2KtknTbl;
    int32_t        entries = jajp_Hrgn2KtknTbl_n / H2K_STRIDE;

    while (*in != 0) {
        if (ju_IsHiragana((const char *)in)) {
            int32_t i;

            for (i = 0; i < entries; i++)
                if (ju_DbCmp((const char *)in,
                             (const char *)(table + i * H2K_STRIDE)))
                    break;
            if (i < entries) {
                *out++ = table[i * H2K_STRIDE + H2K_OUT];
                *out++ = table[i * H2K_STRIDE + H2K_OUT + 1];
                in += 2;
            } else {
                *out++ = *in++;
                *out++ = *in++;
            }
        } else if (ju_IsDBCSLeadByte((char)*in)) {
            *out++ = *in++;
            *out++ = *in++;
        } else {
            *out++ = *in++;
        }
    }
    *out = 0;
}

/* ---- two readings the same ----------------------------------------- */

int32_t ju_YomiCmp(uint8_t *a, uint8_t lenA, uint8_t *b, uint8_t lenB)
{
    int32_t i;

    if (lenA != lenB)
        return 0;
    for (i = 0; i < lenA; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

/* ---- taking an entry out of a chain -------------------------------- */

/* The phrase tables keep their entries on a doubly-linked chain of
   sixteen-bit indices, with one value standing for the end. This unlinks
   entry `which' and puts it at the head of whatever chain `freeHead' names,
   which is how an entry is given back. */
void ju_TableFree(uint16_t *used, uint16_t *tail, uint16_t *freeHead,
                  void *chain, uint16_t nil, uint16_t which)
{
    LinkTable *table = (LinkTable *)chain;
    uint16_t   was = *freeHead;

    *freeHead = which;

    if (*used == which)
        *used = table[which].prev;
    if (table[which].prev != nil)
        table[table[which].prev].next = table[which].next;
    if (table[which].next != nil)
        table[table[which].next].prev = table[which].prev;

    table[which].next = nil;
    table[which].prev = was;
    if (was != nil)
        table[was].next = which;
    if (*tail == nil)
        *tail = *freeHead;
}
