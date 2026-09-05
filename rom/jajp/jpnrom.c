/* Romanizer, which is what the engine asks for Japanese.
 *
 * It is the class the whole of `rom/jajp' hangs off: `ConverterInterface' is
 * its base, `TextAnalysis' is the object it makes and every other class in the
 * analyser is handed a reference to, and `IntonPhrase' and `ProsCtrl' turn
 * what the analysis settled into the string the synthesiser reads.
 *
 * `rom/jajp/romanizer.h' is the record. The methods go in from the leaves up,
 * as the rest of the romanizer did.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "jprom.h"
#include "romanizer.h"
#include "txtanal.h"
#include "intonphrase.h"
#include "inputchar.h"

#define RZ_AT(rz, which) (*(void **)((uint8_t *)(rz) + (which)))
#define RZ_L(rz, off)    (*(int32_t *)((uint8_t *)(rz) + (off)))
#define RZ_W(rz, off)    (*(int16_t *)((uint8_t *)(rz) + (off)))

/* ---- the small ones --------------------------------------------------- */

/* How far into the text the caller's own offset has reached, which the reader
   keeps and this only passes on. */
int32_t rz_getOffset(void *rz)
{
    void *ic = *(void **)((uint8_t *)RZ_AT(rz, RZ_TXTANAL_AT)
                          + TA_INPUTCHAR_AT);

    return (int32_t)*(int16_t *)((uint8_t *)ic + IC_RAWPOS);
}

/* Everything waiting thrown away: the text the input manager holds, every
   annotation, the reader's own buffer, the phrase table, the text the analysis
   was given, and the chain of breath groups. */
void rz_ResetBuffer(void *rz)
{
    void *ta = RZ_AT(rz, RZ_TXTANAL_AT);
    void *ip = RZ_AT(rz, RZ_INTON_AT);

    im_remove((InputManager *)RZ_AT(rz, RZ_INPUT_AT));
    an_RemoveAfter((Annotation *)*(void **)((uint8_t *)ta + TA_ANNOTATION_AT),
                   -1);
    *(int32_t *)((uint8_t *)ta + TA_DONE) = 0;
    ta_ClearInputBuf(ta);
    ta_ClearPhraseTable(ta);
    ta_SetText(ta, NULL, 0);
    *(void **)((uint8_t *)ip + IP_CUR_AT)  = NULL;
    *(void **)((uint8_t *)ip + IP_HEAD_AT) = NULL;
    RZ_L(rz, RZ_FRESH) = 1;
    RZ_L(rz, RZ_MORE)  = 0;
}

/* IBM's hook for a reading the caller has redefined, which does nothing. */
void rz_ChangeDefYomi(void *rz, void *row)
{
    (void)rz;
    (void)row;
}

/* Whether a row is the mark that says a reading is being redefined: the two
   kakari bytes exactly one and three, and no reading at all. */
int16_t rz_CheckDefYomiCMD(void *rz, void *row)
{
    (void)rz;
    if (((uint8_t *)row)[PT_LEFT] == 1
        && ((uint8_t *)row)[PT_RIGHT] == 3
        && ((uint8_t *)row)[PT_KANA] == 0xff)
        return 0;
    return -1;
}

/* The mark between two words of a phrase, written into the output where the
   walk has reached the place the phrase says the next one begins. The answer
   is the index to look at next, moved on only where a mark was written. */
int16_t rz_InsertWordSeparator(void *rz, char *out, void *ph, int16_t at,
                               int16_t i)
{
    (void)rz;
    if (at == *(int16_t *)((uint8_t *)ph + IH_E + i * 2)) {
        char buf[16];

        sprintf(buf, "/%02X", (unsigned)((uint8_t *)ph)[IH_F + i]);
        strcat(out, buf);
        i++;
    }
    return i;
}

/* How many words the whole chain of breath groups runs to, counted off each
   phrase's own first byte. */
int16_t rz_GetWordIndex(void *rz, void *bg)
{
    int16_t n = 0;
    void   *g;

    (void)rz;
    for (g = bg; g != NULL; g = IG_NEXT_OF(g)) {
        int32_t i;

        for (i = 0; i < (int32_t)((uint8_t *)g)[IG_PHRASES]; i++)
            n = (int16_t)(n + *((uint8_t *)g + IG_PHRASE
                                + (size_t)i * IG_PHRASE_SIZE + IH_FIRST));
    }
    return n;
}

/* How many index marks the caller put in a stretch of text: a backquote, then
   a `u', then an `i'. Anything else between them starts the look again. */
uint16_t rz_CountUserIndex(void *rz, char *s)
{
    uint16_t at = 0;
    uint16_t n  = 0;
    int32_t  sawBack = 0;
    int32_t  sawU    = 0;
    uint8_t  c = (uint8_t)s[0];

    (void)rz;
    while (c != 0) {
        if (sawBack != 0) {
            if (sawU != 0) {
                if (c == 'i')
                    n++;
                sawU    = 0;
                sawBack = 0;
            } else if (c == 'u') {
                sawU = 1;
            }
        } else if (c == '`') {
            sawBack = 1;
        }
        at++;
        c = (uint8_t)s[at];
    }
    return n;
}
