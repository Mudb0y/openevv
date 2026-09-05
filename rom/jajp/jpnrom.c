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

/* ---- the dictionary manager, which is one for the whole library -------- */

/* IBM keeps a single `DictMan' in a static of the class, so a second instance
   overwrites the first's and neither ever gives it back. Ours does the same,
   because what the object holds is a path string and nothing else reads it
   after `Init' has looked at it. */
static void *rz_dictman;

/* Eight bytes: a vtable slot and the directory with `dic' on the end of it. */
static void *dm_ctor(void *dm, const char *path)
{
    size_t n = strlen(path) + strlen("dic") + 1;

    *(char **)((uint8_t *)dm + DM_PATH) = (char *)cpp_new((uint32_t)n);
    if (*(char **)((uint8_t *)dm + DM_PATH) != NULL) {
        strcpy(*(char **)((uint8_t *)dm + DM_PATH), path);
        strcat(*(char **)((uint8_t *)dm + DM_PATH), "dic");
    }
    return dm;
}

/* The dictionaries opened. `StaticDict::Initialize' does not appear here: what
   it does is fill in a table of pointers to the static dictionary's own pages,
   and those are lifted as initialised tables, so there is nothing left for it
   to do at run time.
 *
 * Minus one is either the manager or its path not being made, and both set the
 * memory error on the parameter block. */
int8_t rz_Init(void *rz)
{
    RomInstParam *param = (RomInstParam *)RZ_AT(rz, RZ_PARAM_AT);
    void         *dm    = cpp_new(DM_BYTES);

    rz_dictman = dm != NULL ? dm_ctor(dm, param->path) : NULL;
    if (rz_dictman == NULL) {
        rp_setError(param, ROM_ERR_MEMORY);
        return -1;
    }
    if (*(char **)((uint8_t *)rz_dictman + DM_PATH) == NULL) {
        rp_setError(param, ROM_ERR_MEMORY);
        return -1;
    }
    dm_EngRulesInit();
    dm_InitSupplementDictionary();
    RZ_W(rz, RZ_UNREAD_MID + 0x10) = 0;
    RZ_W(rz, RZ_NUMBER_MODE)       = 0;
    return 0;
}

/* ---- the answer handed back ------------------------------------------- */

/* What was built is given to the caller and everything that made it is reset.
 *
 * An empty answer at the end of the text is given a single space rather than
 * nothing, so that the caller sees an utterance. The reader is then wound
 * either back to the start of the text -- where the text ended -- or on to
 * where it had reached, and the phrase table, the breath groups and the two
 * marks are cleared for the next sentence.
 *
 * Nought is the buffer refusing to grow.
 */
int32_t rz_SendResult(void *rz, char **out)
{
    void *ta = RZ_AT(rz, RZ_TXTANAL_AT);
    void *ic = *(void **)((uint8_t *)ta + TA_INPUTCHAR_AT);
    void *ip = RZ_AT(rz, RZ_INTON_AT);
    char *text;

    text = dynaBufContents((DynaBuf *)RZ_AT(rz, RZ_OUT_AT));
    if (text[0] == '\0' && ta_IsEndOfInput(ta)) {
        if (dynaBufAddString((DynaBuf *)RZ_AT(rz, RZ_OUT_AT), " ", 1) == 0)
            return 0;
    }
    *out = dynaBufContents((DynaBuf *)RZ_AT(rz, RZ_OUT_AT));

    if (*(void **)((uint8_t *)ic + IC_TEXTP_AT) != NULL) {
        if (ta_IsEndOfInput(ta)) {
            RZ_L(rz, RZ_FRESH) = 1;
            ic_SetTextAt(ic, NULL, 0);
        } else {
            ic_SetTextAt(ic,
                         (const char *)*(void **)((uint8_t *)ic + IC_TEXTP_AT)
                         + *(int32_t *)((uint8_t *)ic + IC_POS), 0);
        }
    }
    *(int32_t *)((uint8_t *)ta + TA_DONE) = 0;
    ta_ClearPhraseTable(ta);
    *(void **)((uint8_t *)ip + IP_CUR_AT)  = NULL;
    *(void **)((uint8_t *)ip + IP_HEAD_AT) = NULL;
    *(int16_t *)((uint8_t *)ic + IC_RAWPOS) = -1;
    *(int32_t *)((uint8_t *)ic + IC_MORE)   = 0;
    RZ_W(rz, RZ_MARK) = -1;
    return 1;
}

/* ---- a reading the caller redefined ----------------------------------- */

/* The reading of one row copied into another with its first code dropped: the
   mark that says a reading is being redefined is itself the first code. The
   mora runs are cleared rather than copied, and the first entry of the two
   that are read again is set from the new length. */
int16_t rz_ChangeYomi(void *rz, void *dst, void *src)
{
    uint8_t *d = (uint8_t *)dst;
    uint8_t *s = (uint8_t *)src;
    int16_t  i;

    (void)rz;
    for (i = 0; i < (int16_t)(s[PT_MORAS] - 1); i++)
        d[PT_KANA + i] = s[PT_KANA + 1 + i];
    d[PT_MORAS] = (uint8_t)(s[PT_MORAS] - 1);

    for (i = 0; i < PT_MORA_N; i++) {
        d[PT_MORA + i]        = 0;
        d[PT_MORA_HI + i]     = 0;
        d[PT_MORA_ACC + i]    = 0;
        d[PT_MORA_HI_ACC + i] = 0;
    }
    d[PT_MORA]     = d[PT_MORAS];
    d[PT_MORA_ACC] = 0;
    d[PT_HOLD]     = (uint8_t)(d[PT_HOLD] + s[PT_HOLD]);
    return 0;
}

/* ---- the parameters an annotation set --------------------------------- */

/* Every backquote in the text handed to the reader of one, which is what
   settles the voice and the four numbers that go with it. */
int32_t rz_GetParameters(void *rz, char *s)
{
    uint16_t at = 0;
    uint8_t  c  = (uint8_t)s[0];

    while (c != 0) {
        if (c == '`')
            rz_GetParameter(rz, s + at);
        at++;
        c = (uint8_t)s[at];
    }
    return 0;
}

/* ---- one parameter out of an annotation ------------------------------- */

/* IBM writes the value clamped at the top and then writes it again clamped at
   the bottom, and the second write overwrites the first -- so the top clamp
   has no effect at all and a percentage can carry the parameter past it. That
   is kept as it is rather than corrected: it is what the original does with
   the same annotation. */
static int32_t rz_clamp(int32_t got, int32_t cap)
{
    int32_t capped = got < cap ? got : cap;

    (void)capped;
    return got > 0 ? got : 0;
}

/* `v' and then one of six shapes: a letter and a number, which sets a
 * parameter outright; a letter, a per cent sign, a sign and a number, which
 * moves it by that much of itself; `swpm' or `bhz' and a signed number, which
 * moves the speed or the baseline by words a minute or by hertz; a letter and
 * `med', which puts it back to the middle for whichever of the two voices is
 * in force; and a number on its own, which picks a voice and puts all three of
 * the others back to that voice's own.
 *
 * `t' and then `s' and a number is the one that is not a voice parameter: it
 * says whether English is spelt out.
 *
 * Minus one is text that is not one of those, which is most text.
 */
int32_t rz_GetParameter(void *rz, char *s)
{
    RomInstParam *param = (RomInstParam *)RZ_AT(rz, RZ_PARAM_AT);
    char          letter = 0;
    char          sign   = 0;
    int32_t       value  = 0;
    int32_t       dir;
    int32_t       got;
    int32_t       voice;

    if ((int32_t)strlen(s) <= 3)
        return -1;

    if (s[1] == 'v') {
        if (sscanf(s + 2, "%c%d", &letter, &value) == 2) {
            switch (letter) {
            case 's': RZ_L(rz, RZ_SPEED)    = value; break;
            case 'b': RZ_L(rz, RZ_BASELINE) = value; break;
            case 'f': RZ_L(rz, RZ_FLUENCY)  = value; break;
            case 'v': RZ_L(rz, RZ_VOLUME)   = value; break;
            default: break;
            }
            return 0;
        }
        if (sscanf(s + 2, "%c%%%c%d", &letter, &sign, &value) == 3) {
            dir = sign == '+' ? 1 : -1;
            switch (letter) {
            case 's':
                got = RZ_L(rz, RZ_SPEED) * dir * value / 100
                      + RZ_L(rz, RZ_SPEED);
                RZ_L(rz, RZ_SPEED) = rz_clamp(got, 0xfa);
                break;
            case 'b':
                got = RZ_L(rz, RZ_BASELINE) * dir * value / 100
                      + RZ_L(rz, RZ_BASELINE);
                RZ_L(rz, RZ_BASELINE) = rz_clamp(got, 0x64);
                break;
            case 'f':
                got = RZ_L(rz, RZ_FLUENCY) * dir * value / 100
                      + RZ_L(rz, RZ_FLUENCY);
                RZ_L(rz, RZ_FLUENCY) = rz_clamp(got, 0x64);
                break;
            case 'v':
                got = RZ_L(rz, RZ_VOLUME) * dir * value / 100
                      + RZ_L(rz, RZ_VOLUME);
                RZ_L(rz, RZ_VOLUME) = rz_clamp(got, 0x64);
                break;
            default:
                break;
            }
            return 0;
        }
        if (sscanf(s + 2, "swpm%c%d", &sign, &value) == 2) {
            dir = sign == '+' ? 1 : -1;
            got = dir * value / 0xc8 + RZ_L(rz, RZ_SPEED);
            RZ_L(rz, RZ_SPEED) = rz_clamp(got, 0xfa);
            return 0;
        }
        if (sscanf(s + 2, "bhz%c%d", &sign, &value) == 2) {
            dir = sign == '+' ? 1 : -1;
            got = dir * value / 10 + RZ_L(rz, RZ_BASELINE);
            RZ_L(rz, RZ_BASELINE) = rz_clamp(got, 0x64);
            return 0;
        }
        if (sscanf(s + 2, "%cmed", &letter) == 1) {
            voice = rp_getParam(param, 0x3ea);
            switch (letter) {
            case 's':
                RZ_L(rz, RZ_SPEED) = 0x2e;
                break;
            case 'b':
                RZ_L(rz, RZ_BASELINE) = voice == 1 ? 0x41 : 0x59;
                break;
            case 'f':
                RZ_L(rz, RZ_FLUENCY) = voice == 1 ? 0x1e : 0x27;
                break;
            default:
                break;
            }
            return 0;
        }
        if (sscanf(s + 2, "%d", &value) == 1
            && (value == 1 || value == 2)) {
            RZ_L(rz, RZ_VOICE) = value;
            if (value == 1) {
                RZ_L(rz, RZ_BASELINE) = 0x41;
                RZ_L(rz, RZ_FLUENCY)  = 0x1e;
                RZ_L(rz, RZ_SPEED)    = 0x2e;
            } else {
                RZ_L(rz, RZ_BASELINE) = 0x59;
                RZ_L(rz, RZ_FLUENCY)  = 0x27;
                RZ_L(rz, RZ_SPEED)    = 0x2e;
            }
            return 0;
        }
        return -1;
    }

    if (s[1] == 't') {
        if (sscanf(s + 2, "%c%d", &letter, &value) == 2 && letter == 's') {
            RZ_L(rz, RZ_SPELL_ENGLISH) = value;
            return 0;
        }
    }
    return -1;
}
