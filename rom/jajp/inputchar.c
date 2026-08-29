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

#define IC_TEXTP_OF(in)     ((const char *)IC_P(in, IC_TEXTP_AT))

#define SN_P(n, off)        ((uint8_t *)(n) + (off))
#define SN_B(n, off)        (*SN_P(n, off))
#define SN_WORD(n, off)     (*(int16_t *)SN_P(n, off))
#define SN_NEXT(n)          (*(void **)SN_P(n, SN_NEXT_AT))
#define SN_KEY(n)           (*(char **)SN_P(n, SN_KEY_AT))
#define SN_VALUE(n)         (*(char **)SN_P(n, SN_VALUE_AT))

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
    IC_L(in, IC_SPARE_278C) = 0;
    IC_P(in, IC_TEXTP_AT) = NULL;
    IC_L(in, IC_AT_END) = 0;
    ic_Init(in);
    IC_W(in, IC_SPARE_1C20) = -1;
    IC_L(in, IC_SPARE_27B4) = 0;
    memset(IC_AT(in, IC_SCRATCH), 0, (size_t)IC_SCRATCH_N * 2);
    memset(IC_AT(in, IC_OFFSET), 0, 0x2d6);
    memset(IC_AT(in, IC_MARK), 0, 0x2d6);
    IC_L(in, IC_POS) = 0;
    IC_L(in, IC_READ) = 0;
    memset(IC_AT(in, IC_SPARE_279E), 0, 4);
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
    IC_L(in, IC_SPARE_2790) = 0;
    IC_L(in, IC_SPARE_2794) = 0;
    IC_W(in, IC_COUNT) = 0;
    IC_W(in, IC_W_279C) = 0;
    IC_L(in, IC_SPARE_27A4) = 0;
}

/* ---- the text ---------------------------------------------------------- */

/* Text with nothing said about where to start, which is a fresh buffer read
   from its first byte. */
void ic_SetText(void *in, const char *text)
{
    IC_P(in, IC_TEXTP_AT) = (void *)(uintptr_t)(const void *)text;
    IC_L(in, IC_AT_END) = 0;
    IC_L(in, IC_READ) = 0;
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
    IC_L(in, IC_READ) = 0;
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
    void *rom = *(void **)((uint8_t *)analysis + TA_OWNER);

    return rp_isAnnotationsInText(*(RomInstParam **)((uint8_t *)rom
                                                     + RM_PARAM));
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
    rom = *(void **)((uint8_t *)analysis + TA_OWNER);
    dict = *(RomUserDict **)((uint8_t *)rom + RM_USERDICT);
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
