/* The front of the Japanese analyser: text in, characters out.
 *
 * Everything else in the analyser indexes InputChar's three parallel arrays --
 * the characters, what each one is, and where each began in the bytes the
 * caller sent -- so this class is what makes the input the rest of it reads.
 * rom/jajp/inputchar.h is the record, and it is IBM's own offsets because
 * DictSearch and RomUserDict reach into it rather than call.
 *
 * This file is the object's setup and its side table. The reading of a
 * sentence, which is the other half of inputchar.obj, is not here yet.
 *
 * The side table is the SNLK chain, and it is worth saying what it is for. A
 * caller may tell the romanizer how a particular stretch of the text it is
 * about to send should be read -- not a dictionary entry, which would apply
 * everywhere, but this occurrence. ic_AddSnlkTable turns that pair into the
 * same normalised key and yomi codes the user dictionary uses, hangs it on a
 * chain in the order given, and DictSearch::Do asks ic_GetSnlkTableAt for the
 * one sitting at the character it has reached. Nothing searches the chain: it
 * is kept in position order and walked until the position is passed.
 *
 * Held to IBM's answer by test/romprims.sh.
 */

#include <stdlib.h>
#include <string.h>
#include "jprom.h"
#include "inputchar.h"
#include "txtanal.h"

#define IC_AT(in, off)      ((uint8_t *)(in) + (off))
#define IC_W(in, off)       (*(int16_t *)IC_AT(in, off))
#define IC_L(in, off)       (*(int32_t *)IC_AT(in, off))
#define IC_P(in, off)       (*(void **)IC_AT(in, off))

#define IC_CHAR(in, i)      ((char *)IC_AT(in, IC_TEXT + (i) * 2))
#define IC_SCRAP(in, i)     ((char *)IC_AT(in, IC_SCRATCH + (i) * 2))
#define IC_KIND_AT(in, i)   (*(int32_t *)IC_AT(in, IC_KIND + (i) * 4))
#define IC_OFFSET_AT(in, i) (*(int16_t *)IC_AT(in, IC_OFFSET + (i) * 2))
#define IC_MARK_AT(in, i)   (*(int32_t *)IC_AT(in, IC_MARK + (i) * 4))

#define IC_B(in, off)       (*(uint8_t *)IC_AT(in, off))
#define IC_TEXTP_OF(in)     ((const char *)IC_P(in, IC_TEXTP_AT))
#define IC_TEXTW_OF(in)     ((char *)IC_P(in, IC_TEXTP_AT))

/* The characters this file names. Every one is read out of the object's own
   data rather than worked out from the name MSVC filed it under: two of them
   -- the repeat mark and the topic particle -- decode to something else
   entirely by hand, which is the trap rom/jajp/dictsearch.c records. */
static const char IDEO_SPACE[]    = "\x81\x40";
static const char IDEO_COMMA[]    = "\x81\x41";
static const char IDEO_STOP[]     = "\x81\x42";
static const char COMMA[]         = "\x81\x43";
static const char PERIOD[]        = "\x81\x44";
static const char NAKAGURO[]      = "\x81\x45";   /* the middle dot */
static const char QUESTION[]      = "\x81\x48";
static const char BANG[]          = "\x81\x49";
static const char REPEAT_MARK[]   = "\x81\x58";
static const char CHOON[]         = "\x81\x5b";   /* the long-vowel bar */
static const char BRACKET_CLOSE[] = "\x81\x6e";
static const char PARTICLE_WA[]   = "\x82\xcd";   /* the topic particle */
static const char CRLF[]          = "\x0d\x0a";

/* The tables this file indexes, at the offsets rom/jajp/userdict.c and
   rom/jajp/jpnutil.c already name them by. */
#define YOMI_ASCII      0x229    /* two bytes each, from 0x21 */
#define YOMI_HALFKANA   0x2e7    /* two bytes each, from 0xa1 */
#define KANJI_NUM_AT    0x307
#define KANJI_NUM_N     0x13

/* How long a sentence may get. IBM's three bounds differ by one and are kept
   apart because nothing says they are the same number. */
#define CHARS_MOST      0x3e     /* where ic_CheckContext gives up */
#define RECOVER_SCAN    0x40     /* how far the first two recovery walks go */
#define RECOVER_LAST    0x3f     /* the third, and the bracket bound */

/* What ic_GetUnknownKanji steps over rather than collects: the ideographic
   space, comma and full stop, the full-width question and exclamation marks,
   and the full-width comma and period. IBM's order is kept although nothing
   observes it, since ju_DbCmp2 has no effect to observe. */
static const char *const NOT_COLLECTED[] = {
    "\x81\x40", "\x81\x41", "\x81\x42", "\x81\x48",
    "\x81\x49", "\x81\x43", "\x81\x44"
};

/* ---- making one ------------------------------------------------------ */

/* Everything the constructor does after ic_Init is a clear, and three of the
   four memsets clear less than the array they name; rom/jajp/inputchar.h says
   why that is IBM's and is kept. */
void *ic_ctor(void *in, void *analysis)
{
    IC_P(in, IC_OWNER_AT) = analysis;
    IC_L(in, IC_RESUME) = 0;
    IC_P(in, IC_TEXTP_AT) = NULL;
    IC_L(in, IC_AT_END) = 0;
    ic_Init(in);
    IC_W(in, IC_RAWPOS) = -1;
    IC_L(in, IC_MORE) = 0;
    memset(IC_AT(in, IC_SCRATCH), 0, (size_t)IC_SCRATCH_N * 2);
    memset(IC_AT(in, IC_OFFSET), 0, 0x2d6);
    memset(IC_AT(in, IC_MARK), 0, 0x2d6);
    IC_L(in, IC_POS) = 0;
    IC_L(in, IC_ENDED) = 0;
    memset(IC_AT(in, IC_ENDMARK), 0, 4);
    IC_P(in, IC_SNLK_AT) = NULL;
    IC_W(in, IC_LENGTH) = 0;
    return in;
}

/* What is done again between one sentence and the next. The kinds are filled
   with a byte rather than an int32, so what each covered entry gets is
   0x0c0c0c0c and not KIND_OTHER. */
void ic_Init(void *in)
{
    memset(IC_AT(in, IC_TEXT), 0, (size_t)IC_TEXT_N * 2);
    memset(IC_AT(in, IC_KIND), KIND_OTHER, 0x2d6);
    IC_L(in, IC_ENGRUN) = 0;
    IC_L(in, IC_NUMRUN) = 0;
    IC_W(in, IC_COUNT) = 0;
    IC_W(in, IC_BRACKET_AT) = 0;
    IC_L(in, IC_PAUSE) = 0;
}

/* ---- the text ---------------------------------------------------------- */

/* Text with nothing said about where to start, which is a fresh buffer read
   from its first byte. */
void ic_SetText(void *in, const char *text)
{
    IC_P(in, IC_TEXTP_AT) = (void *)(uintptr_t)(const void *)text;
    IC_L(in, IC_AT_END) = 0;
    IC_L(in, IC_ENDED) = 0;
}

/* And text with a byte to carry on from, which is what an appended buffer
   wants. Before it takes the new one it counts the characters of the old --
   from its first byte up to where the reader had got to -- into IC_LENGTH, so
   that a position given to ic_GetSnlkTableAt can go on meaning the same thing
   across a buffer that has been replaced. */
void ic_SetTextAt(void *in, const char *text, uint32_t at)
{
    int32_t i;

    if (IC_P(in, IC_TEXTP_AT) != NULL) {
        for (i = 0; i < IC_L(in, IC_POS); i++) {
            if (ju_IsDBCSLeadByte(IC_TEXTP_OF(in)[i]))
                i++;
            IC_W(in, IC_LENGTH) = (int16_t)(IC_W(in, IC_LENGTH) + 1);
        }
    }
    IC_P(in, IC_TEXTP_AT) = (void *)(uintptr_t)(const void *)text;
    IC_L(in, IC_POS) = (int32_t)at;
    IC_L(in, IC_AT_END) = 0;
    IC_L(in, IC_ENDED) = 0;
}

/* The byte the reader is on. It does not advance; every caller does that
   itself, which is how a lead byte and its trail are taken as a pair. */
uint8_t ic_GetNextChar(void *in)
{
    return (uint8_t)IC_TEXTP_OF(in)[IC_L(in, IC_POS)];
}

/* Whether the caller said its text carries annotations, which is a question
   for the parameter block two objects up. */
int32_t ic_IsAnnotationsInText(void *in)
{
    void *analysis = IC_P(in, IC_OWNER_AT);
    void *rom = *(void **)((uint8_t *)analysis + TA_OWNER_AT);

    return rp_isAnnotationsInText(*(RomInstParam **)((uint8_t *)rom
                                                     + RZ_PARAM_AT));
}

/* ---- the SNLK chain --------------------------------------------------- */

/* Take a reading for one stretch of the text. The written form becomes the
   same normalised key the user dictionary is keyed by, and the reading becomes
   yomi codes; both are kept on the node, and the node goes on the end of the
   chain.
 *
 * Two things here are IBM's and are kept. The node is leaked if
 * makeTransValue fails -- the key and the value are freed on that road and the
 * node is not -- and the last argument is tested for not being negative and is
 * then never looked at again. */
int32_t ic_AddSnlkTable(void *in, int16_t at, const char *written,
                        const char *reading, int32_t flag)
{
    RomUserDict *dict;
    void        *analysis, *rom, *node, *tail;
    char        *key, *value;
    const char  *p;
    int32_t      chars, keyLen;

    if (written == NULL || *written == '\0' || reading == NULL
        || *reading == '\0' || at < 0 || flag < 0)
        return -1;

    analysis = IC_P(in, IC_OWNER_AT);
    rom = *(void **)((uint8_t *)analysis + TA_OWNER_AT);
    dict = *(RomUserDict **)((uint8_t *)rom + RZ_USERDICT_AT);
    if (dict == NULL)
        return -1;

    node = cpp_new((uint32_t)SN_ROOM);
    if (node == NULL)
        return -1;

    /* Room for the key: two bytes a character, since every character the key
       may be made of is a double-byte one, and a terminator. */
    chars = 0;
    for (p = written; *p != '\0'; ) {
        chars++;
        p += ju_IsDBCSLeadByte(*p) ? 2 : 1;
    }
    key = cpp_new((uint32_t)(chars * 2 + 1));
    if (key == NULL) {
        cpp_delete(node);
        return -1;
    }
    if (!rud_makeKey(dict, (uint8_t *)(uintptr_t)(const void *)written,
                     (int32_t)strlen(written), key, &keyLen))
        strcpy(key, written);

    /* And the count that goes on the node is of the key rather than of what
       the caller wrote, because normalising can change it. */
    chars = 0;
    for (p = key; *p != '\0'; ) {
        chars++;
        p += ju_IsDBCSLeadByte(*p) ? 2 : 1;
    }
    SN_B(node, SN_CHARS) = (uint8_t)chars;

    value = cpp_new((uint32_t)(strlen(reading) + 1));
    if (value == NULL) {
        cpp_delete(key);
        cpp_delete(node);
        return -1;
    }
    SN_B(node, SN_TRANS) = 0xff;
    if (!rud_makeTransValue(dict, reading, SN_P(node, SN_TRANS), value,
                            (int16_t)(strlen(reading) + 1))) {
        cpp_delete(value);
        cpp_delete(key);
        return -1;
    }

    SN_VALUE(node) = value;
    SN_KEY(node) = key;
    SN_WORD(node, SN_AT) = at;
    SN_NEXT(node) = NULL;
    SN_B(node, SN_YOMI_N) = rud_transKana2Yomi(dict, value,
                                               SN_P(node, SN_YOMI));
    if (SN_B(node, SN_YOMI_N) > 25)
        SN_B(node, SN_YOMI_N) = 25;

    if (IC_P(in, IC_SNLK_AT) == NULL) {
        IC_P(in, IC_SNLK_AT) = node;
    } else {
        tail = IC_P(in, IC_SNLK_AT);
        while (SN_NEXT(tail) != NULL)
            tail = SN_NEXT(tail);
        SN_NEXT(tail) = node;
    }
    return 0;
}

/* The node sitting at a character, counting from the start of everything the
   caller has sent rather than from the start of the buffer in hand. The chain
   is in position order, so passing the position is as good as reaching the
   end. */
void *ic_GetSnlkTableAt(void *in, int16_t at)
{
    void *node = IC_P(in, IC_SNLK_AT);

    while (node != NULL) {
        int32_t want = (int32_t)at + IC_W(in, IC_LENGTH);

        if (SN_WORD(node, SN_AT) > want)
            return NULL;
        if (SN_WORD(node, SN_AT) == want)
            return node;
        node = SN_NEXT(node);
    }
    return NULL;
}

void ic_DeleteSnlkTable(void *in)
{
    void *node = IC_P(in, IC_SNLK_AT);

    while (node != NULL) {
        void *dead = node;

        node = SN_NEXT(dead);
        if (SN_VALUE(dead) != NULL)
            cpp_delete(SN_VALUE(dead));
        if (SN_KEY(dead) != NULL)
            cpp_delete(SN_KEY(dead));
        cpp_delete(dead);
    }
    IC_P(in, IC_SNLK_AT) = NULL;
}

/* ---- the unknown-kanji pass -------------------------------------------- */

/* Every double-byte character between two byte offsets, laid into the record
   backwards.
 *
 * It walks the bytes collecting each double-byte character that is not one of
 * the seven punctuation marks, remembering for each one where it began and
 * which character of the whole text it was; then it copies the collection into
 * IC_TEXT and IC_OFFSET in reverse. The kind written for each is
 * KIND_HIRAGANA, which is not what any of them is -- what the caller wants is
 * a set of characters to look up, and four is what makes the walk that reads
 * them treat every one alike.
 *
 * The bound is one too generous: IC_SCRATCH holds six hundred and ninety-four
 * characters and the guard lets the six hundred and ninety-fifth through, so a
 * text of that many writes two bytes over the start of IC_KIND. It is IBM's
 * and it is kept; no sentence the analyser will accept is that long. */
int16_t ic_GetUnknownKanji(void *in, int16_t at, int32_t from, int32_t to)
{
    int16_t where[696];       /* IBM's is the whole of its frame below ebp */
    int16_t n, j;
    int32_t saved;
    uint8_t lead, trail;
    int     k, skip;

    if (to <= from)
        return -1;

    n = 0;
    saved = IC_L(in, IC_POS);
    IC_L(in, IC_POS) = from;
    memset(IC_AT(in, IC_SCRATCH), 0, (size_t)IC_SCRATCH_N * 2);

    while (IC_L(in, IC_POS) < to) {
        if ((uint16_t)n > IC_SCRATCH_N) {
            IC_W(in, IC_COUNT) = n;
            return -1;
        }
        lead = ic_GetNextChar(in);
        IC_L(in, IC_POS)++;
        at = (int16_t)(at + 1);
        if (lead == '\n' || !ju_IsDBCSLeadByte((char)lead))
            continue;
        trail = ic_GetNextChar(in);
        IC_L(in, IC_POS)++;

        skip = 0;
        for (k = 0; k < (int)(sizeof NOT_COLLECTED / sizeof *NOT_COLLECTED);
             k++) {
            if (ju_DbCmp2(NOT_COLLECTED[k], (char)lead, (char)trail)) {
                skip = 1;
                break;
            }
        }
        if (skip)
            continue;

        ju_DbSet(IC_SCRAP(in, n), (char)lead, (char)trail);
        IC_MARK_AT(in, n) = (uint16_t)(IC_L(in, IC_POS) - 2);
        where[n] = at;
        n++;
    }
    IC_W(in, IC_COUNT) = n;
    IC_L(in, IC_POS) = saved;

    j = (int16_t)(IC_W(in, IC_COUNT) - 1);
    for (n = 0; (int32_t)(uint16_t)n < IC_W(in, IC_COUNT); n++, j--) {
        IC_KIND_AT(in, n) = KIND_HIRAGANA;
        ju_DbCpy(IC_CHAR(in, n), IC_SCRAP(in, j));
        IC_OFFSET_AT(in, n) = where[j];
    }
    return 0;
}

/* ---- what a character is ---------------------------------------------- */

/* Whether the character at an index is one of the nineteen kanji numerals.
 *
 * JpnUtil::IsKanjiNum answers the same question over a pointer and this one
 * over an index into the record; both are IBM's and both are here because
 * ic_GetCharType calls this one and ic_CheckContextForNum the other. */
int32_t ic_IsKanjiNum(void *in, int32_t at)
{
    const uint8_t *num = dm_GetNumberDataPtr();
    int16_t        i;

    for (i = 0; i < KANJI_NUM_N; i++)
        if (ju_DbCmp(IC_CHAR(in, at), (const char *)(num + KANJI_NUM_AT
                                                     + i * 2)))
            return 1;
    return 0;
}

/* Which of the twelve kinds the character at an index is. This is the only
 * place the numbering is stated, and rom/jajp/inputchar.h names all twelve off
 * this method rather than off any of the places that test them.
 *
 * Nine, kanji, is the default: anything the ranges do not recognise is taken
 * for one, and so is an index before the start of the text. */
int32_t ic_GetCharType(void *in, int16_t at)
{
    char lead, trail;

    if (at < 0)
        return KIND_KANJI;

    ju_TwoChCpy(IC_CHAR(in, at), &lead, &trail);

    if ((uint8_t)lead == 0x83) {
        if ((uint8_t)trail >= 0x40 && (uint8_t)trail <= 0x96)
            return KIND_KATAKANA;
        if ((uint8_t)trail >= 0xa0 && (uint8_t)trail <= 0xd6)
            return KIND_GREEK;
        return KIND_OTHER;
    }
    if ((uint8_t)lead == 0x81) {
        if ((uint8_t)trail == 0x5a) return KIND_DIGIT;
        if ((uint8_t)trail == 0x5b) return KIND_CHOON;
        if ((uint8_t)trail == 0x45) return KIND_NAKAGURO;
        if ((uint8_t)trail == 0x6d) return KIND_BRACKET;
        if ((uint8_t)trail >= 0x43 && (uint8_t)trail <= 0xac)
            return KIND_PUNCT;
        return KIND_OTHER;
    }
    if ((uint8_t)lead == 0x82) {
        if (((uint8_t)trail >= 0x60 && (uint8_t)trail <= 0x79)
            || ((uint8_t)trail >= 0x81 && (uint8_t)trail <= 0x9a))
            return KIND_LATIN;
        if ((uint8_t)trail >= 0x9f && (uint8_t)trail <= 0xf1)
            return KIND_HIRAGANA;
        if ((uint8_t)trail >= 0x4f && (uint8_t)trail <= 0x58)
            return KIND_DIGIT;
        return KIND_OTHER;
    }
    if ((uint8_t)lead == 0xfa
        && (uint8_t)trail >= 0x40 && (uint8_t)trail <= 0x5a)
        return KIND_ROMAN;

    return ic_IsKanjiNum(in, at) > 0 ? KIND_DIGIT : KIND_KANJI;
}

/* ---- what surrounds a character --------------------------------------- */

/* Whether an annotation is the next thing in the text, once any run of spaces
 * -- ASCII or ideographic -- has been passed. What marks one is a backtick. */
int32_t ic_CheckNextAnnotation(void *in)
{
    int32_t at = IC_L(in, IC_POS);
    int32_t found = 0;
    char    prev = 0;
    char    c = IC_TEXTP_OF(in)[at];

    for (;;) {
        if (c == 0)
            break;
        if (ju_IsDBCSLeadByte(prev) && ju_IsDBCSTrailByte((uint8_t)c)) {
            if (!ju_TwoChCmp(prev, c, IDEO_SPACE))
                break;
        } else if ((uint8_t)c == 0x60) {
            found = 1;
            break;
        } else if ((uint8_t)c != 0x20) {
            break;
        }
        prev = c;
        at++;
        c = IC_TEXTP_OF(in)[at];
    }
    return found;
}

/* A run of middle dots, which is what a list written with them looks like.
 * Two or more of them in a row are answered with how many; anything less is
 * nought and the caller treats the dot as an ordinary character. */
int16_t ic_CheckCyuTen(void *in, int16_t *at)
{
    int16_t n = 0;
    char    lead, trail;

    if (*at <= 0)
        return 0;

    lead = IC_TEXTP_OF(in)[IC_L(in, IC_POS)];
    trail = IC_TEXTP_OF(in)[IC_L(in, IC_POS) + 1];

    while (ju_TwoChCmp(lead, trail, NAKAGURO)) {
        const char *p = IC_TEXTP_OF(in) + IC_L(in, IC_POS);

        n++;
        lead = p[n * 2];
        if (lead != 0)
            trail = p[n * 2 + 1];
    }
    return n < 2 ? 0 : n;
}

/* Whether the character about to be read carries a number on from the one
 * already in hand, which is what decides that a break in the middle of a
 * number is not a break at all. It sets IC_NUMJOIN rather than answering, and
 * always answers nought. */
int16_t ic_CheckContextForNum(void *in, int16_t *at)
{
    int32_t     kind = 0;
    int32_t     was = 0;
    char        c;
    const char *p;

    if (*at == 0)
        was = 0;
    else
        was = IC_KIND_AT(in, *at - 1);

    c = IC_TEXTP_OF(in)[IC_L(in, IC_POS)];
    p = IC_TEXTP_OF(in) + IC_L(in, IC_POS);

    if (ju_IsDBCSLeadByte(c) && !ju_IsDBCSNum(p) && !ju_IsKanjiNum(p))
        kind = 0;
    else if (ju_IsNum(c) || ju_IsDBCSNum(p) || ju_IsKanjiNum(p))
        kind = KIND_DIGIT;
    else if (c == 0 || c == ' ')
        kind = KIND_ENGWORD;
    else
        kind = 0;

    if (was == KIND_DIGIT && (kind == KIND_DIGIT || kind == KIND_ENGWORD))
        IC_L(in, IC_NUMJOIN) = 1;
    return 0;
}

/* Classify the character just laid down, look at what follows it, and step the
 * count on. Answers one where the sentence has grown too long and had to be
 * cut back, and nought otherwise.
 *
 * Three things are settled here rather than in the reader. A run of full-width
 * letters that began after a space becomes KIND_ENGWORD instead of KIND_LATIN,
 * which is what tells the analyser to spell it out as English. The next
 * character is laid down as well, unless the caller is only looking ahead --
 * that is what the flag is for. And the two run flags are set from the pair,
 * which is what keeps a full stop inside a number or an abbreviation from
 * ending the sentence. */
int16_t ic_CheckContext(void *in, int16_t *at, int32_t peek)
{
    int32_t kind = 0;
    int32_t was = 0;
    char    c0, c1;

    IC_KIND_AT(in, *at) = ic_GetCharType(in, *at);

    if (*at > 0 && IC_KIND_AT(in, *at - 1) == KIND_LATIN) {
        char b = IC_TEXTP_OF(in)[IC_MARK_AT(in, *at) - 1];

        if (b == ' ' || b == '\n' || b == '\t' || b == '\r')
            IC_KIND_AT(in, *at - 1) = KIND_ENGWORD;
    }

    if (ju_DbCmp(IC_CHAR(in, *at), BRACKET_CLOSE))
        IC_W(in, IC_BRACKET_AT) = *at;

    was = (*at == 0) ? 0 : IC_KIND_AT(in, *at - 1);

    c0 = IC_TEXTP_OF(in)[IC_L(in, IC_POS)];
    if (c0 != 0) {
        c1 = IC_TEXTP_OF(in)[IC_L(in, IC_POS) + 1];
        if (ju_TwoChCmp(c0, c1, CRLF)) {
            c0 = IC_TEXTP_OF(in)[IC_L(in, IC_POS) + 2];
            c1 = c0 != 0 ? IC_TEXTP_OF(in)[IC_L(in, IC_POS) + 3] : 0;
        }
    } else {
        c1 = 0;
    }

    if (ju_IsDBCSLeadByte(c0)) {
        if (!peek) {
            ju_DbSet(IC_CHAR(in, *at + 1), c0, c1);
            IC_MARK_AT(in, *at + 1) = IC_L(in, IC_POS);
            IC_OFFSET_AT(in, *at + 1) = IC_W(in, IC_RAWPOS);
        }
        kind = ic_GetCharType(in, (int16_t)(*at + 1));
    } else if (ju_IsNum(c0)) {
        kind = KIND_DIGIT;
    } else if (c0 == 0 || c0 == ' ') {
        kind = KIND_ENGWORD;
    } else {
        kind = 0;
    }

    if (was == KIND_DIGIT && kind == KIND_DIGIT)
        IC_L(in, IC_NUMRUN) = 1;
    if ((was == KIND_LATIN || was == KIND_ENGWORD) && kind != KIND_ENGWORD)
        IC_L(in, IC_ENGRUN) = 1;

    (*at)++;
    if (*at < CHARS_MOST)
        return 0;

    /* Too long. Back off to somewhere a sentence may be cut, and say so. */
    if (IC_TEXTP_OF(in)[IC_L(in, IC_POS) - 1] == ' ')
        (*at)--;
    ic_RecoverOverflow(in, *at);
    return 1;
}

/* ---- laying a character down ------------------------------------------ */

/* A half-width kana and the voicing mark after it, written as the one
 * full-width character they stand for. */
void ic_ConvertDakuten(void *in, int16_t at, uint8_t c, uint8_t mark)
{
    ju_ConvertDakuten(IC_CHAR(in, at), c, mark);
    IC_MARK_AT(in, at) = IC_L(in, IC_POS) - 2;
    IC_OFFSET_AT(in, at) = IC_W(in, IC_RAWPOS);
}

/* One single-byte letter, digit or symbol, written as the full-width form the
 * yomi table holds for it.
 *
 * A comma or a full stop at the very start of a sentence is not a character at
 * all: it is what the sentence before ended on, arriving late, so the reader
 * is told to give up on this one and the mark is kept for the caller. Anywhere
 * else the two bytes go in unchanged rather than as the full-width form, which
 * is what the second copy is for. */
int16_t ic_ProcessASCII(void *in, int16_t at, uint8_t *c0, uint8_t *c1)
{
    uint8_t i = (uint8_t)(*c0 - 0x21);

    ju_DbCpy(IC_CHAR(in, at),
             (const char *)(dm_GetYomiDataPtr() + YOMI_ASCII + i * 2));
    IC_MARK_AT(in, at) = IC_L(in, IC_POS) - 1;
    IC_OFFSET_AT(in, at) = IC_W(in, IC_RAWPOS);

    if (*c0 == ',' || *c0 == '.') {
        if (at == 0) {
            IC_W(in, IC_COUNT) = at;
            IC_L(in, IC_ENDED) = 1;
            IC_B(in, IC_ENDMARK) = *c0;
            IC_B(in, IC_ENDMARK + 1) = 0;
            return -4;
        }
        ju_TwoChCpy(IC_CHAR(in, at), (char *)c0, (char *)c1);
    }
    return 0;
}

/* ---- the annotations --------------------------------------------------- */

/* One annotation, from the backtick that begins it to the character that is
 * not part of it, handed to Annotation to keep until the output passes the
 * place it belonged to.
 *
 * A pause annotation is also added up here, since the reader is the only thing
 * that sees the number. Answers one where the annotation ends the sentence,
 * minus one where there was no room to keep it, and nought where the backtick
 * stood alone and was not an annotation at all. */
int8_t ic_ProcessAnnotation(void *in, int16_t at)
{
    int16_t n = 0;
    int16_t where;
    uint8_t c;
    void   *anno;

    do {
        n++;
        c = ic_GetNextChar(in);
        IC_L(in, IC_POS)++;
        if (c == 0)
            break;
    } while (ju_IsAlpha((char)c) || c == '.' || c == '%' || c == '/'
             || c == '[' || c == ']' || c == '\'' || c == '+' || c == '-');

    while (n > 1 && ju_IsNum((char)c)) {
        n++;
        c = ic_GetNextChar(in);
        IC_L(in, IC_POS)++;
        if (c == 0)
            break;
    }
    IC_L(in, IC_POS)--;

    if (n == 1)
        return 0;

    IC_W(in, IC_RAWPOS) = (int16_t)(IC_W(in, IC_RAWPOS) + n - 1);
    where = at > 0 ? IC_W(in, IC_RAWPOS) : 0;

    if (n >= 3) {
        const char *p = IC_TEXTP_OF(in) + IC_L(in, IC_POS) - n;

        if ((int8_t)p[1] == 'p') {
            int32_t ms;

            if (sscanf(p + 2, "%d", &ms) == 1)
                IC_L(in, IC_PAUSE) += ms;
        }
    }

    anno = *(void **)((uint8_t *)IC_P(in, IC_OWNER_AT) + TA_ANNOTATION_AT);
    return an_Save((Annotation *)anno,
                   (char *)(uintptr_t)(const void *)(IC_TEXTP_OF(in)
                                                     + IC_L(in, IC_POS) - n),
                   n, where) ? 1 : -1;
}

/* ---- backing off a sentence that grew too long ------------------------- */

/* Sixty-two characters in and no end in sight, so the sentence is cut at the
 * best place that has gone by. Four are looked for in turn, each overriding
 * the one before: any kanji that a hiragana led into, the same where that
 * hiragana was the particle no, any punctuation that is not a repeat mark or a
 * long-vowel bar and does not follow a digit, and the last closing bracket.
 * Then, if the text the caller sent carries its own marks, the last one of the
 * first kind before here.
 *
 * Whatever is chosen, an ideographic comma is written over the character there
 * so that the analyser sees a break rather than a truncation, and every
 * annotation past that place is given up. */
void ic_RecoverOverflow(void *in, int16_t at)
{
    const char *raw;
    int32_t     stepped = 0;
    int32_t     p;
    int16_t     i, j;
    void       *anno;

    IC_W(in, IC_COUNT) = at;
    IC_L(in, IC_ENDED) = 0;

    for (i = 1; i < RECOVER_SCAN; i++)
        if (IC_KIND_AT(in, i - 1) == KIND_HIRAGANA
            && IC_KIND_AT(in, i) == KIND_KANJI) {
            IC_L(in, IC_POS) = IC_MARK_AT(in, i);
            IC_W(in, IC_COUNT) = i;
        }

    for (i = 1; i < RECOVER_SCAN; i++)
        if (IC_KIND_AT(in, i - 1) == KIND_HIRAGANA
            && IC_KIND_AT(in, i) == KIND_KANJI
            && ju_DbCmp(IC_CHAR(in, i - 1), PARTICLE_WA)) {
            IC_L(in, IC_POS) = IC_MARK_AT(in, i);
            IC_W(in, IC_COUNT) = i;
        }

    /* The first turn of this one asks what kind the character before nought
       is, which is the last four bytes of IC_SCRATCH rather than a kind at
       all. It is IBM's and it is kept. */
    for (i = 0; i < RECOVER_LAST; i++) {
        char c;

        if (IC_KIND_AT(in, i) != KIND_PUNCT)
            continue;
        if (IC_KIND_AT(in, i - 1) == KIND_DIGIT)
            continue;
        if (ju_DbCmp(IC_CHAR(in, i), REPEAT_MARK))
            continue;
        if (ju_DbCmp(IC_CHAR(in, i), CHOON))
            continue;
        c = IC_TEXTP_OF(in)[IC_MARK_AT(in, i)];
        IC_L(in, IC_POS) = IC_MARK_AT(in, i)
                           + (ju_IsAlphaNumSym(c) || ju_IsSBCSKana(c) ? 1 : 2);
        IC_W(in, IC_COUNT) = (int16_t)(i + 1);
    }

    if (IC_W(in, IC_COUNT) < IC_W(in, IC_BRACKET_AT)
        && IC_W(in, IC_BRACKET_AT) < RECOVER_LAST) {
        IC_W(in, IC_COUNT) = (int16_t)(IC_W(in, IC_BRACKET_AT) + 1);
        IC_L(in, IC_POS) = IC_MARK_AT(in, IC_W(in, IC_BRACKET_AT) + 1);
    }

    raw = *(const char **)((uint8_t *)IC_P(in, IC_OWNER_AT) + TA_RAW_AT);
    j = (int16_t)(at - 1);
    if ((uint8_t)raw[(int16_t)(IC_W(in, IC_LENGTH) + IC_OFFSET_AT(in, at - 1))]
        == 1
        || (uint8_t)raw[(int16_t)(IC_W(in, IC_LENGTH)
                                  + IC_OFFSET_AT(in, at - 1))] == 2) {
        for (; j > 0; j--) {
            if ((uint8_t)raw[(int16_t)(IC_W(in, IC_LENGTH)
                                       + IC_OFFSET_AT(in, j))] == 1) {
                stepped = 1;
                IC_W(in, IC_COUNT) = j;
                IC_L(in, IC_POS) = IC_MARK_AT(in, j);
                break;
            }
        }
    }

    ju_DbCpy(IC_CHAR(in, IC_W(in, IC_COUNT)), IDEO_COMMA);
    IC_W(in, IC_RAWPOS) = IC_OFFSET_AT(in, IC_W(in, IC_COUNT) - 1);

    p = IC_L(in, IC_POS);
    if (stepped) {
        while (p > 0) {
            p--;
            if ((int8_t)IC_TEXTP_OF(in)[p] != ' ')
                break;
            IC_W(in, IC_RAWPOS)++;
        }
    }
    IC_OFFSET_AT(in, IC_W(in, IC_COUNT)) = IC_W(in, IC_RAWPOS);

    anno = *(void **)((uint8_t *)IC_P(in, IC_OWNER_AT) + TA_ANNOTATION_AT);
    an_RemoveAfter((Annotation *)anno, IC_W(in, IC_RAWPOS));
}

/* ---- one sentence ------------------------------------------------------ */

/* Read from wherever the reader stands to the end of the next sentence,
 * filling the three arrays as it goes.
 *
 * The loop is written round the pair of bytes last taken rather than round the
 * one about to be: every turn begins by asking whether what was taken last
 * ends a sentence, so an arm that wants to end one can do it by setting that
 * pair and going round again. The space arm does exactly that.
 *
 * What ends a sentence is a full stop, a question mark or an exclamation mark
 * always; a full-width period or comma only where no run of digits and no run
 * of letters is open; and an ideographic comma only where no run of digits is.
 * That is what keeps a decimal point and an abbreviation from cutting a
 * sentence in half.
 *
 * The answers are IBM's and there are eight: one for a sentence that ended on
 * a comma, two for one cut off by the end of a double-byte character, nought
 * for an ordinary end, minus one for the end of the text with nothing read,
 * minus four for a sentence that was nothing but its own punctuation, and
 * minus five for an annotation there was no room to keep. Anything else is
 * whatever ic_CheckContext answered, which is one when the sentence had to be
 * cut back for length.
 *
 * One thing it does is worth stating plainly: it writes into the caller's own
 * buffer. A full-width space that falls where a break belongs is overwritten
 * with an ideographic comma, in the text as it was handed in. */
int16_t ic_ReadSentence(void *in)
{
    int16_t at;
    int16_t mode = 2;
    int16_t rc;
    int16_t peek;
    uint8_t c0, c1;

    if (IC_L(in, IC_RESUME) == 0) {
        ic_Init(in);
        at = 0;
    } else {
        at = IC_W(in, IC_COUNT);
    }

    if (IC_L(in, IC_MORE) != 0) {
        if (IC_W(in, IC_RAWPOS) > -1)
            IC_W(in, IC_RAWPOS)--;
        IC_L(in, IC_MORE) = 0;
    }

    c1 = 0;
    c0 = c1;
    strcpy((char *)IC_AT(in, IC_ENDMARK), "");

    for (;;) {
        if (ju_TwoChCmp((char)c0, (char)c1, IDEO_STOP))
            break;
        if (ju_TwoChCmp((char)c0, (char)c1, QUESTION))
            break;
        if (ju_TwoChCmp((char)c0, (char)c1, BANG))
            break;
        if (ju_TwoChCmp((char)c0, (char)c1, PERIOD)
            && !IC_L(in, IC_NUMRUN) && !IC_L(in, IC_ENGRUN))
            break;
        if (ju_TwoChCmp((char)c0, (char)c1, IDEO_COMMA)
            && !IC_L(in, IC_NUMRUN))
            break;
        if (ju_TwoChCmp((char)c0, (char)c1, COMMA)
            && !IC_L(in, IC_NUMRUN) && !IC_L(in, IC_ENGRUN))
            break;

        IC_L(in, IC_NUMJOIN) = 0;
        IC_L(in, IC_ENGRUN) = 0;
        IC_L(in, IC_NUMRUN) = 0;

        c0 = ic_GetNextChar(in);
        IC_L(in, IC_POS)++;
        IC_W(in, IC_RAWPOS)++;

        if (c0 == 0) {
            IC_L(in, IC_MORE) = 1;
            IC_W(in, IC_COUNT) = at;
            IC_L(in, IC_ENDED) = 1;
            IC_L(in, IC_AT_END) = 1;
            return at == 0 ? -1 : 1;
        }

        if (c0 == '\n') {
            if (mode & 4) {
                IC_W(in, IC_COUNT) = at;
                IC_L(in, IC_ENDED) = 1;
                return at == 0 ? -4 : 0;
            }
            if (at > 0) {
                peek = at;
                rc = ic_CheckContextForNum(in, &peek);
                if (rc)
                    return rc;
                if (IC_L(in, IC_NUMJOIN)) {
                    IC_W(in, IC_COUNT) = at;
                    IC_L(in, IC_ENDED) = 1;
                    return 0;
                }
            }
            continue;
        }

        if (c0 == '\r')
            continue;

        c1 = ic_GetNextChar(in);

        if (ju_IsDBCSLeadByte((char)c0) && ju_IsDBCSTrailByte(c1)) {
            IC_L(in, IC_POS)++;

            /* A sentence that begins with its own end is the mark the sentence
               before ran out of buffer on, arriving now. It is handed back
               rather than read. */
            if (at == 0) {
                if (ju_TwoChCmp((char)c0, (char)c1, IDEO_COMMA)
                    || ju_TwoChCmp((char)c0, (char)c1, COMMA)) {
                    strcpy((char *)IC_AT(in, IC_ENDMARK), ",");
                    IC_W(in, IC_COUNT) = at;
                    IC_L(in, IC_ENDED) = 1;
                    return -4;
                }
                if (ju_TwoChCmp((char)c0, (char)c1, IDEO_STOP)
                    || ju_TwoChCmp((char)c0, (char)c1, PERIOD)) {
                    strcpy((char *)IC_AT(in, IC_ENDMARK), ".");
                    IC_W(in, IC_COUNT) = at;
                    IC_L(in, IC_ENDED) = 1;
                    return -4;
                }
            }

            if (c1 == 0) {
                IC_W(in, IC_COUNT) = at;
                IC_L(in, IC_ENDED) = 1;
                return at == 0 ? -1 : 2;
            }

            rc = 0;
            if (ju_TwoChCmp((char)c0, (char)c1, NAKAGURO))
                rc = ic_CheckCyuTen(in, &at);

            if (rc > 0) {
                /* A run of middle dots is one full stop, and the whole run is
                   stepped over at once. */
                c0 = 0x81;
                c1 = 0x42;
                ju_DbSet(IC_CHAR(in, at), (char)c0, (char)c1);
                IC_MARK_AT(in, at) = IC_L(in, IC_POS) - 2;
                IC_OFFSET_AT(in, at) = IC_W(in, IC_RAWPOS);
                IC_L(in, IC_POS) += rc * 2;
                IC_W(in, IC_RAWPOS) = (int16_t)(IC_W(in, IC_RAWPOS) + rc * 2);
                at++;
            } else if (ju_TwoChCmp((char)c0, (char)c1, IDEO_SPACE)) {
                if (mode & 8) {
                    IC_W(in, IC_COUNT) = at;
                    IC_L(in, IC_ENDED) = 1;
                    return at == 0 ? -4 : 0;
                }
                if (at <= 0) {
                    IC_W(in, IC_COUNT) = at;
                    IC_L(in, IC_ENDED) = 1;
                    return -4;
                }
                peek = at;
                rc = ic_CheckContextForNum(in, &peek);
                if (rc)
                    return rc;
                if (IC_L(in, IC_NUMJOIN)) {
                    IC_W(in, IC_COUNT) = at;
                    IC_L(in, IC_ENDED) = 1;
                    return 0;
                }
                /* Here is the write into the caller's buffer. */
                c0 = 0x81;
                c1 = 0x41;
                ((char *)(uintptr_t)(const void *)IC_TEXTP_OF(in))
                    [IC_L(in, IC_POS) - 2] = (char)c0;
                ((char *)(uintptr_t)(const void *)IC_TEXTP_OF(in))
                    [IC_L(in, IC_POS) - 1] = (char)c1;
                ju_DbSet(IC_CHAR(in, at), (char)c0, (char)c1);
                IC_MARK_AT(in, at) = IC_L(in, IC_POS) - 2;
                IC_OFFSET_AT(in, at) = IC_W(in, IC_RAWPOS);
                rc = ic_CheckContext(in, &at, 0);
                if (rc)
                    return rc;
            } else {
                ju_DbSet(IC_CHAR(in, at), (char)c0, (char)c1);
                IC_MARK_AT(in, at) = IC_L(in, IC_POS) - 2;
                IC_OFFSET_AT(in, at) = IC_W(in, IC_RAWPOS);
                rc = ic_CheckContext(in, &at, 0);
                if (rc)
                    return rc;
            }
            continue;
        }

        if (c0 == ' ') {
            if (mode & 8) {
                IC_W(in, IC_COUNT) = at;
                IC_L(in, IC_ENDED) = 1;
                return at == 0 ? -4 : 0;
            }
            if (at > 0) {
                peek = at;
                rc = ic_CheckContextForNum(in, &peek);
                if (rc)
                    return rc;
                if (IC_L(in, IC_NUMJOIN)) {
                    IC_W(in, IC_COUNT) = at;
                    IC_L(in, IC_ENDED) = 1;
                    return 0;
                }
                peek = at;
                rc = ic_CheckContext(in, &peek, 1);
                if (rc)
                    return rc;
                /* Nothing is laid down: the pair is set to an ideographic
                   comma and the loop's own test at the top ends the
                   sentence. */
                if (!IC_L(in, IC_ENGRUN) && ic_CheckNextAnnotation(in) == 0) {
                    c0 = 0x81;
                    c1 = 0x41;
                    at = peek;
                }
            }
            continue;
        }

        if (c0 == '\t') {
            if (mode & 2) {
                IC_W(in, IC_COUNT) = at;
                IC_L(in, IC_ENDED) = 1;
                if (at > 0)
                    return 0;
            }
            continue;
        }

        if (ju_IsAlphaNumSym((char)c0)) {
            if (c0 == 0x60) {
                rc = (int16_t)ic_ProcessAnnotation(in, at);
                if (rc < 0)
                    return -5;
                if (rc == 1) {
                    IC_W(in, IC_COUNT) = at;
                    IC_L(in, IC_ENDED) = 1;
                    if (at > 0) {
                        strcpy((char *)IC_AT(in, IC_ENDMARK), "`");
                        return 1;
                    }
                    continue;
                }
            }
            rc = ic_ProcessASCII(in, at, &c0, &c1);
            if (rc == 3)
                continue;
            if (rc)
                return rc;
            rc = ic_CheckContext(in, &at, 0);
            if (rc)
                return rc;
            continue;
        }

        if (ju_IsSBCSKana((char)c0)) {
            if (c1 == 0xde || c1 == 0xdf) {
                IC_L(in, IC_POS)++;
                ic_ConvertDakuten(in, at, c0, c1);
            } else {
                uint8_t i = (uint8_t)(c0 - 0xa1);

                ju_DbCpy(IC_CHAR(in, at),
                         (const char *)(dm_GetYomiDataPtr() + YOMI_HALFKANA
                                        + i * 2));
                IC_MARK_AT(in, at) = IC_L(in, IC_POS) - 1;
                IC_OFFSET_AT(in, at) = IC_W(in, IC_RAWPOS);

                if (c0 == 0xa4 || c0 == 0xa1) {
                    if (at == 0) {
                        strcpy((char *)IC_AT(in, IC_ENDMARK),
                               c0 == 0xa4 ? "," : ".");
                        IC_W(in, IC_COUNT) = at;
                        IC_L(in, IC_ENDED) = 1;
                        return -4;
                    }
                    ju_TwoChCpy(IC_CHAR(in, at), (char *)&c0, (char *)&c1);
                }
            }
            rc = ic_CheckContext(in, &at, 0);
            if (rc)
                return rc;
        }
        /* and anything else is dropped */
    }

    IC_W(in, IC_COUNT) = at;
    IC_W(in, IC_COUNT)--;

    /* Only the trail byte is looked at, the lead having been settled by the
       tests at the top of the loop. */
    if (c1 == 0x41 || c1 == 0x43) {
        IC_L(in, IC_ENDED) = ic_CheckNextAnnotation(in) > 0 ? 1 : 0;
        strcpy((char *)IC_AT(in, IC_ENDMARK), ",");
        return 1;
    }
    if (c1 == 0x42 || c1 == 0x44) {
        IC_L(in, IC_ENDED) = 1;
        if (at == 0)
            strcpy((char *)IC_AT(in, IC_ENDMARK), ".");
        return 0;
    }
    if (c1 == 0x48) {
        IC_L(in, IC_ENDED) = 1;
        strcpy((char *)IC_AT(in, IC_ENDMARK), "?");
        return 0;
    }
    if (c1 == 0x49) {
        IC_L(in, IC_ENDED) = 1;
        strcpy((char *)IC_AT(in, IC_ENDMARK), "!");
        return 0;
    }
    return 0;
}
