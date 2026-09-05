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
#include "evv_port.h"

#define RZ_AT(rz, which) (*(void **)((uint8_t *)(rz) + (which)))
#define RZ_L(rz, off)    (*(int32_t *)((uint8_t *)(rz) + (off)))
#define RZ_W(rz, off)    (*(int16_t *)((uint8_t *)(rz) + (off)))

/* ---- the small ones --------------------------------------------------- */

/* How far into the text the caller's own offset has reached, which the reader
   keeps and this only passes on. */
int32_t jrz_getOffset(void *rz)
{
    void *ic = *(void **)((uint8_t *)RZ_AT(rz, RZ_TXTANAL_AT)
                          + TA_INPUTCHAR_AT);

    return (int32_t)*(int16_t *)((uint8_t *)ic + IC_RAWPOS);
}

/* Everything waiting thrown away: the text the input manager holds, every
   annotation, the reader's own buffer, the phrase table, the text the analysis
   was given, and the chain of breath groups. */
void jrz_ResetBuffer(void *rz)
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
void jrz_ChangeDefYomi(void *rz, void *row)
{
    (void)rz;
    (void)row;
}

/* Whether a row is the mark that says a reading is being redefined: the two
   kakari bytes exactly one and three, and no reading at all. */
int16_t jrz_CheckDefYomiCMD(void *rz, void *row)
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
int16_t jrz_InsertWordSeparator(void *rz, char *out, void *ph, int16_t at,
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
int16_t jrz_GetWordIndex(void *rz, void *bg)
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
uint16_t jrz_CountUserIndex(void *rz, char *s)
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
static void *jrz_dictman;

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
int8_t jrz_Init(void *rz)
{
    RomInstParam *param = (RomInstParam *)RZ_AT(rz, RZ_PARAM_AT);
    void         *dm    = cpp_new(DM_BYTES);

    jrz_dictman = dm != NULL ? dm_ctor(dm, param->path) : NULL;
    if (jrz_dictman == NULL) {
        rp_setError(param, ROM_ERR_MEMORY);
        return -1;
    }
    if (*(char **)((uint8_t *)jrz_dictman + DM_PATH) == NULL) {
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
int32_t jrz_SendResult(void *rz, char **out)
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
int16_t jrz_ChangeYomi(void *rz, void *dst, void *src)
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
int32_t jrz_GetParameters(void *rz, char *s)
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

/* One setting an annotation names, changed.
 *
 * `v' and then one of six shapes: a letter and a number, which sets a
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
 *
 * `DictSearch::Do' calls it as well as this class does, which is why it was
 * written before the rest of Romanizer was. One thing in it is IBM's and is
 * kept: every relative form writes the setting twice, once clamped at the top
 * and then again from the unclamped value, so the upper clamp has no effect
 * at all and only the clamp at nought does anything.
 *
 * Held to IBM's answer by `test/harness/romprims.sh', over every shape there
 * is and a few there are not.
 */
#define RZ_L(rz, off)   (*(int32_t *)((uint8_t *)(rz) + (off)))

/* Which voice the parameter block says is in force, which is what the middle
   settings and the voice reset are taken from. */
#define P_VOICE_ID      0x3ea

/* One setting stepped by a proportion of itself, and then by the two clamps
   IBM wrote -- the second of which undoes the first. */
static void stepBy(void *rz, int32_t off, int32_t by, int32_t cap)
{
    int32_t got = by + RZ_L(rz, off);

    RZ_L(rz, off) = got < cap ? got : cap;
    RZ_L(rz, off) = got > 0 ? got : 0;
}

int32_t rz_GetParameter(void *rz, char *p)
{
    RomInstParam *param = *(RomInstParam **)((uint8_t *)rz + RZ_PARAM_AT);
    int32_t       v;
    int32_t       dir;
    int32_t       was;
    char          which;
    char          sign;

    if (strlen(p) <= 3)
        return -1;

    if (p[1] == 'v') {
        if (sscanf(p + 2, "%c%d", &which, &v) == 2) {
            switch (which) {
            case 's': RZ_L(rz, RZ_SPEED) = v; break;
            case 'b': RZ_L(rz, RZ_BASELINE) = v; break;
            case 'f': RZ_L(rz, RZ_FLUENCY) = v; break;
            case 'v': RZ_L(rz, RZ_VOLUME) = v; break;
            default: break;
            }
            return 0;
        }
        if (sscanf(p + 2, "%c%%%c%d", &which, &sign, &v) == 3) {
            dir = sign == '+' ? 1 : -1;
            switch (which) {
            case 's':
                stepBy(rz, RZ_SPEED,
                       RZ_L(rz, RZ_SPEED) * dir * v / 100, 0xfa);
                break;
            case 'b':
                stepBy(rz, RZ_BASELINE,
                       RZ_L(rz, RZ_BASELINE) * dir * v / 100, 0x64);
                break;
            case 'f':
                stepBy(rz, RZ_FLUENCY,
                       RZ_L(rz, RZ_FLUENCY) * dir * v / 100, 0x64);
                break;
            case 'v':
                stepBy(rz, RZ_VOLUME,
                       RZ_L(rz, RZ_VOLUME) * dir * v / 100, 0x64);
                break;
            default: break;
            }
            return 0;
        }
        if (sscanf(p + 2, "swpm%c%d", &sign, &v) == 2) {
            dir = sign == '+' ? 1 : -1;
            stepBy(rz, RZ_SPEED, dir * v / 0xc8, 0xfa);
            return 0;
        }
        if (sscanf(p + 2, "bhz%c%d", &sign, &v) == 2) {
            dir = sign == '+' ? 1 : -1;
            stepBy(rz, RZ_BASELINE, dir * v / 0xa, 0x64);
            return 0;
        }
        if (sscanf(p + 2, "%cmed", &which) == 1) {
            was = rp_getParam(param, P_VOICE_ID);
            switch (which) {
            case 's': RZ_L(rz, RZ_SPEED) = 0x2e; break;
            case 'b': RZ_L(rz, RZ_BASELINE) = was == 1 ? 0x41 : 0x59; break;
            case 'f': RZ_L(rz, RZ_FLUENCY) = was == 1 ? 0x1e : 0x27; break;
            default: break;
            }
            return 0;
        }
        if (sscanf(p + 2, "%d", &v) == 1 && (v == 1 || v == 2)) {
            RZ_L(rz, RZ_VOICE) = v;
            if (v == 1) {
                RZ_L(rz, RZ_BASELINE) = 0x41;
                RZ_L(rz, RZ_FLUENCY) = 0x1e;
            } else {
                RZ_L(rz, RZ_BASELINE) = 0x59;
                RZ_L(rz, RZ_FLUENCY) = 0x27;
            }
            RZ_L(rz, RZ_SPEED) = 0x2e;
            return 0;
        }
        return -1;
    }

    if (p[1] == 't' && sscanf(p + 2, "%c%d", &which, &v) == 2) {
        if (which == 's')
            RZ_L(rz, RZ_SPELL_ENGLISH) = v;
        return 0;
    }
    return -1;
}

/* ---- the string the synthesiser reads --------------------------------- */

/* One breath group turned into ESPR, with the annotations that belonged in
 * front of it put out first.
 *
 * The annotations are taken off the front of the ring while the place each
 * belonged to is at or before where this group starts. Three of them are the
 * index marks -- `@g', `@i' and `@ui' -- and those get a comment opener in
 * front so that the synthesiser does not read them as text; one is `@p' and a
 * number, which is a pause and is added up rather than written out.
 *
 * Then every phrase of every group is asked what index mark it carries and how
 * many the caller put in it, and the sample rate is turned into the device
 * number the prosody writer wants. What comes back from that writer is the
 * answer; nothing coming back leaves what was collected here.
 */
int32_t jrz_GenerateESPR(void *rz, void *bg, char *out)
{
    RomInstParam *param = (RomInstParam *)RZ_AT(rz, RZ_PARAM_AT);
    Annotation   *anno;
    char         *buf;
    void         *g;
    int32_t       rc     = -1;
    int32_t       pauses = 0;
    int32_t       ms     = 0;
    int16_t       at     = 0;
    int16_t       marks;
    int32_t       voice;
    int32_t       rate;

    buf = (char *)cpp_new(0x4001);
    if (buf == NULL)
        return rc;
    buf[0] = '\0';

    anno = (Annotation *)*(void **)((uint8_t *)RZ_AT(rz, RZ_TXTANAL_AT)
                                    + TA_ANNOTATION_AT);
    at   = *(int16_t *)((uint8_t *)bg + IG_PHRASE + IH_VAL);
    strcat(out, " ");

    while (anno->count > 0) {
        int16_t     where = anno->count != 0 ? anno->at[anno->head]
                                             : (int16_t)-1;
        const char *text;

        if (where > at)
            break;
        text = anno->count != 0 ? anno->text[anno->head] : NULL;
        if (!rp_isAnnotationsInText(param)
            || strncmp(text, "`g", 2) == 0
            || strncmp(text, "`i", 2) == 0
            || strncmp(text, "`ui", 3) == 0)
            strcat(out, " \\\\");
        if (rp_isAnnotationsInText(param)
            && sscanf(text, "`p%d", &ms) == 1) {
            pauses += ms;
        } else {
            strcat(out, text);
            strcat(out, " ");
        }
        an_Remove(anno);
    }

    if (rp_isAnnotationsInText(param))
        jrz_GetParameters(rz, out);
    ci_outputIndexOrParam(rz, buf, at);
    if (strlen(buf) > 0) {
        jrz_GetParameters(rz, buf);
        strcat(out, " ");
        strcat(out, buf);
        buf[0] = '\0';
    }
    strcat(out, " ");

    for (g = bg; g != NULL; g = IG_NEXT_OF(g)) {
        uint8_t i;

        for (i = 0; i < ((uint8_t *)bg)[IG_PHRASES]; i++) {
            uint8_t *ph = (uint8_t *)g + IG_PHRASE
                          + (size_t)i * IG_PHRASE_SIZE;

            if (ph[IH_COUNT] > 0)
                at = *(int16_t *)(ph + IH_VAL + (size_t)ph[IH_COUNT] * 2);
            else
                at = 0;
            buf[0] = '\0';
            ci_outputIndexOrParam(rz, buf, at);
            marks = strlen(buf) > 0
                    ? (int16_t)jrz_CountUserIndex(rz, buf) : (int16_t)0;
            *(int16_t *)(ph + IH_E) = marks;
        }
    }
    buf[0] = '\0';

    voice = rp_getParam(param, 0x3ea);
    rate  = rp_getParam(param, 0x3eb);
    switch (rate) {
    case 0x1f40: RZ_L(rz, RZ_RATE) = 0; break;
    case 0x2b11: RZ_L(rz, RZ_RATE) = 1; break;
    case 0x5622: RZ_L(rz, RZ_RATE) = 2; break;
    default:     RZ_L(rz, RZ_RATE) = 1; break;
    }
    RZ_L(rz, RZ_VOICE) = voice;

    if (RZ_AT(rz, RZ_PROS_AT) != NULL)
        rc = pc_GenerateESPR(RZ_AT(rz, RZ_PROS_AT),
                             (const uint8_t *)rz + RZ_VOICE, pauses, NULL,
                             bg, buf, 0x3ff6);
    if (rc == 0)
        strcat(out, buf);
    if (buf != NULL)
        cpp_delete(buf);
    return rc;
}

/* ---- the reading written out as romaji -------------------------------- */

/* One phrase of a breath group turned into what the synthesiser reads, run by
 * run: the annotations that belonged in front of each run first, then the
 * index mark the run carries as a backtick, an `i' and a digit, then a
 * backtick, a `g' and how many characters of the caller's own text the run
 * covers, then the reading itself with an apostrophe where the accent falls.
 *
 * The answer is how many codes of the reading were written, which the caller
 * carries into the next phrase.
 */
int16_t jrz_GenerateRomajiOutput(void *rz, void *bg, void *ph, char *out,
                                void *next)
{
    RomInstParam *param = (RomInstParam *)RZ_AT(rz, RZ_PARAM_AT);
    void         *ta    = RZ_AT(rz, RZ_TXTANAL_AT);
    Annotation   *anno;
    uint8_t      *p     = (uint8_t *)ph;
    int16_t       written = 0;
    int16_t       from    = (int16_t)(RZ_W(rz, RZ_MARK) + 1);
    int16_t       to      = 0;
    int16_t       i;

    (void)bg;
    for (i = 0; i < (int16_t)p[IH_COUNT]; i++, from = to) {
        int16_t span;
        uint8_t len;
        uint8_t moras;
        uint8_t mark;
        int16_t j;
        char    buf[48];

        if (i + 1 < (int16_t)p[IH_COUNT])
            to = *(int16_t *)(p + IH_VAL + (size_t)(i + 1) * 2);
        else if (next != NULL)
            to = *(int16_t *)((uint8_t *)next + IH_VAL);
        else
            to = (int16_t)(*(int16_t *)
                           ((uint8_t *)*(void **)((uint8_t *)ta
                                                  + TA_INPUTCHAR_AT)
                            + IC_RAWPOS) + 1);
        RZ_W(rz, RZ_MARK) = (int16_t)(to - 1);
        span  = (int16_t)(to - from);
        len   = p[IH_LEN + i];
        moras = p[IH_MORAS + i];
        mark  = p[IH_PITCH + i];
        (void)p[IH_A + i];

        anno = (Annotation *)*(void **)((uint8_t *)ta + TA_ANNOTATION_AT);
        while (anno->count > 0) {
            int16_t     where = anno->count != 0 ? anno->at[anno->head]
                                                 : (int16_t)-1;
            const char *text;

            if (where > *(int16_t *)(p + IH_VAL + (size_t)i * 2))
                break;
            text = anno->count != 0 ? anno->text[anno->head] : NULL;
            if (!rp_isAnnotationsInText(param)
                || strncmp(text, "`g", 2) == 0
                || strncmp(text, "`i", 2) == 0
                || strncmp(text, "`ui", 3) == 0)
                strcat(out, " \\\\");
            strcat(out, text);
            strcat(out, " ");
            an_Remove(anno);
        }

        ci_outputIndexOrParam(rz, out,
                              *(int16_t *)(p + IH_VAL + (size_t)i * 2));
        switch (mark) {
        case 6: strcat(out, "`i0 "); break;
        case 1: strcat(out, "`i1 "); break;
        case 2: strcat(out, "`i2 "); break;
        case 3: strcat(out, "`i3 "); break;
        case 4: strcat(out, "`i4 "); break;
        case 5: strcat(out, "`i5 "); break;
        default: break;
        }
        sprintf(buf, "`g%d_", (int)span);
        strcat(out, buf);

        for (j = 0; j <= (int16_t)len; j++) {
            if (j == (int16_t)moras && moras != 0)
                strcat(out, "'");
            if (j == (int16_t)len)
                continue;
            if (!ju_WriteRomajiStrBuf(p[IH_KANA + written + j],
                                      (uint8_t *)out))
                break;
        }
        strcat(out, " ");
        written = (int16_t)(written + len);
    }

    anno = (Annotation *)*(void **)((uint8_t *)ta + TA_ANNOTATION_AT);
    while (anno->count > 0) {
        int16_t     where = anno->count != 0 ? anno->at[anno->head]
                                             : (int16_t)-1;
        const char *text;

        if (where > RZ_W(rz, RZ_MARK))
            break;
        text = anno->count != 0 ? anno->text[anno->head] : NULL;
        if (rp_isAnnotationsInText(param))
            rz_GetParameter(rz, (char *)text);
        else
            strcat(out, "\\\\");
        strcat(out, text);
        strcat(out, " ");
        an_Remove(anno);
    }

    if (out[strlen(out) - 1] == ' ')
        out[strlen(out) - 1] = '\0';
    {
        char tail[8];

        sprintf(tail, " ");
        strcat(out, tail);
    }
    return written;
}

/* ---- the whole answer for one sentence -------------------------------- */

/* Every breath group in the chain written out, one at a time, and each one
 * added to the output buffer as it is finished. A group whose two boundary
 * bytes are both one is passed over.
 *
 * After the phrases of a group comes its pause: a pause shorter than four
 * hundred takes the last character off and puts a full stop or a comma there
 * instead, depending on whether another group follows, and the kind of
 * boundary that closes the group can put a full stop, a question mark or an
 * exclamation mark there as well.
 *
 * With the flush asked for, whatever annotations are left go out after
 * everything else, and the index mark at the very end of the text with them.
 *
 * Nought is the buffer refusing to grow. Two things of IBM's are kept: the
 * pause is written into a buffer of its own that nothing reads, and the one
 * failure that gives up before the working buffer is freed leaks it.
 */
int32_t jrz_GenerateResult(void *rz, int32_t flush)
{
    RomInstParam *param = (RomInstParam *)RZ_AT(rz, RZ_PARAM_AT);
    void         *ta    = RZ_AT(rz, RZ_TXTANAL_AT);
    void         *ip    = RZ_AT(rz, RZ_INTON_AT);
    Annotation   *anno;
    char         *buf;
    void         *g;
    int16_t       phrase = 0;

    buf = (char *)cpp_new(0x4050);
    if (buf == NULL)
        return 0;
    buf[0] = '\0';

    ci_outputIndexOrParam(rz, buf, 0);
    if (strlen(buf) > 0) {
        strcat(buf, " ");
        if (dynaBufAddString((DynaBuf *)RZ_AT(rz, RZ_OUT_AT), buf, 1) == 0)
            return 0;
        buf[0] = '\0';
    }

    for (g = *(void **)((uint8_t *)ip + IP_HEAD_AT); g != NULL;
         g = IG_NEXT_OF(g)) {
        uint8_t *gp = (uint8_t *)g;
        int16_t  i;
        char     pause[32];

        if (gp[IG_LEFT] == 1 && gp[IG_RIGHT] == 1)
            continue;

        for (i = 0; i < (int16_t)gp[IG_PHRASES]; i++, phrase++) {
            uint8_t *ph = gp + IG_PHRASE + (size_t)i * IG_PHRASE_SIZE;

            if (i + 1 < (int16_t)gp[IG_PHRASES])
                jrz_GenerateRomajiOutput(rz, g, ph, buf,
                                        gp + IG_PHRASE
                                        + (size_t)(i + 1) * IG_PHRASE_SIZE);
            else if (IG_NEXT_OF(g) != NULL)
                jrz_GenerateRomajiOutput(rz, g, ph, buf,
                                        (uint8_t *)IG_NEXT_OF(g) + IG_PHRASE);
            else
                jrz_GenerateRomajiOutput(rz, g, ph, buf, NULL);
        }

        /* Written and never read, which is IBM's. */
        sprintf(pause, "P%d\\", (int)*(int16_t *)(gp + IG_PAUSE));

        if (strlen(buf) > 0) {
            if (*(int16_t *)(gp + IG_PAUSE) < 0x190) {
                buf[strlen(buf) - 1] = '\0';
                if (IG_NEXT_OF(g) == NULL)
                    strcat(buf, ". ");
                else
                    strcat(buf, ", ");
            }
            switch (gp[IG_KIND]) {
            case 4:
                if (buf[strlen(buf) - 1] == ' ')
                    buf[strlen(buf) - 1] = '\0';
                strcat(buf, ". ");
                break;
            case 5:
                if (buf[strlen(buf) - 1] == ' ')
                    buf[strlen(buf) - 1] = '\0';
                strcat(buf, "? ");
                break;
            case 6:
                if (buf[strlen(buf) - 1] == ' ')
                    buf[strlen(buf) - 1] = '\0';
                strcat(buf, "! ");
                break;
            default:
                break;
            }
        }
        if (dynaBufAddString((DynaBuf *)RZ_AT(rz, RZ_OUT_AT), buf, 1) == 0) {
            if (buf != NULL)
                cpp_delete(buf);
            return 0;
        }
        buf[0] = '\0';
    }

    if (flush != 0) {
        anno = (Annotation *)*(void **)((uint8_t *)ta + TA_ANNOTATION_AT);
        if (anno->count > 0) {
            if (an_Flush(anno, rp_isAnnotationsInText(param),
                         (DynaBuf *)RZ_AT(rz, RZ_OUT_AT), 0) == 0)
                return 0;   /* IBM's: the working buffer is not freed here */
        }
        ci_outputIndexOrParam(rz, buf, 0x7ffff);
        if (dynaBufAddString((DynaBuf *)RZ_AT(rz, RZ_OUT_AT), buf, 1) == 0) {
            if (buf != NULL)
                cpp_delete(buf);
            return 0;
        }
    }
    if (buf != NULL)
        cpp_delete(buf);
    return 1;
}

/* ---- the whole of one call from the engine ---------------------------- */

/* What the engine asks a Japanese instance for: the next utterance's worth of
 * ESPR, or a word about why there is not one.
 *
 * Six is not an answer but the state the walk starts in and goes round again
 * on. Two is an utterance to say. One is text taken but nothing to say yet.
 * Three is the instance having been told to stop. Nought is nothing left.
 * Minus one is a failure, and every one of them sets the memory error.
 *
 * The first half takes text: from the input manager if there is any waiting,
 * into the analysis fresh or appended to what is there. The second half is the
 * loop that reads it: while the analysis has not finished, one sentence at a
 * time; and once it has, the phrase table it left is handed to `IntonPhrase',
 * which groups it, and the groups are written out.
 *
 * Two things of IBM's to know. An answer of minus twenty from the analysis --
 * the dictionary search refusing -- sets the failure and then goes round the
 * loop again rather than leaving it, because the test that leaves is on the
 * answer being above nought. And the busy flag is cleared on every road out
 * but the failures.
 */
int32_t jrz_processSentence(void *rz, char **out, int32_t more)
{
    RomInstParam *param = (RomInstParam *)RZ_AT(rz, RZ_PARAM_AT);
    void         *ta    = RZ_AT(rz, RZ_TXTANAL_AT);
    void         *ip    = RZ_AT(rz, RZ_INTON_AT);
    void         *ic;
    Annotation   *anno;
    char          buf[0x4000];
    const char   *text  = NULL;
    const char   *taken = NULL;
    uint32_t      tlen  = 0;
    int32_t       result = 6;
    int16_t       len = 0;
    int16_t       rc;

    strcpy(buf, "");
    if (RZ_L(rz, RZ_STOPPED) != 0)
        return 3;
    RZ_L(rz, RZ_BUSY) = 1;
    dynaBufReset((DynaBuf *)RZ_AT(rz, RZ_OUT_AT));

    anno = (Annotation *)*(void **)((uint8_t *)ta + TA_ANNOTATION_AT);
    ic   = *(void **)((uint8_t *)ta + TA_INPUTCHAR_AT);

    if (RZ_L(rz, RZ_FRESH) == 0 && RZ_L(rz, RZ_MORE) == 0 && more == 0) {
        ta_ClearInputBuf(ta);
    } else {
        if (RZ_L(rz, RZ_FRESH) != 0 && anno->count > 0) {
            dynaBufReset((DynaBuf *)RZ_AT(rz, RZ_OUT_AT));
            ci_outputIndexOrParam(rz, buf, 0);
            if (strlen(buf) > 0) {
                strcat(buf, " ");
                if (dynaBufAddString((DynaBuf *)RZ_AT(rz, RZ_OUT_AT), buf, 1)
                    == 0) {
                    rp_setError(param, ROM_ERR_MEMORY);
                    return -1;
                }
                strcpy(buf, "");
            }
            if (an_Flush(anno, rp_isAnnotationsInText(param),
                         (DynaBuf *)RZ_AT(rz, RZ_OUT_AT), 0) != 0
                && jrz_SendResult(rz, out) != 0)
                return 2;
            rp_setError(param, ROM_ERR_MEMORY);
            return -1;
        }

        text = *(const char **)((uint8_t *)ic + IC_TEXTP_AT);
        if (text != NULL)
            len = (int16_t)strlen(text);
        switch (im_getText((InputManager *)RZ_AT(rz, RZ_INPUT_AT), &taken,
                           &tlen, text, (uint32_t)len)) {
        case -1:
            result = -1;
            break;
        case 1:
            result = 0;
            break;
        case 2:
            if (jrz_GenerateResult(rz, 1) != 0 && jrz_SendResult(rz, out) != 0) {
                result = 2;
            } else {
                rp_setError(param, ROM_ERR_MEMORY);
                return -1;
            }
            break;
        case 4:
            more = 1;
            break;
        default:
            break;
        }

        if (result != 6) {
            RZ_L(rz, RZ_BUSY) = 0;
            if (RZ_L(rz, RZ_STOPPED) != 0)
                result = 3;
            return result;
        }

        if (RZ_L(rz, RZ_FRESH) != 0) {
            RZ_L(rz, RZ_FRESH) = 0;
            if (ta_SetText(ta, taken, (int32_t)tlen) == 0) {
                rp_setError(param, ROM_ERR_MEMORY);
                return -1;
            }
            ta_ClearInputBuf(ta);
            *(int16_t *)((uint8_t *)ic + IC_RAWPOS) = -1;
            *(int32_t *)((uint8_t *)ic + IC_MORE)   = 0;
            RZ_W(rz, RZ_MARK) = -1;
        } else if (RZ_L(rz, RZ_MORE) != 0) {
            RZ_L(rz, RZ_MORE) = 0;
            if (more == 0
                && ta_AppendText(ta, taken, (int32_t)tlen) == 0) {
                rp_setError(param, ROM_ERR_MEMORY);
                return -1;
            }
        }
    }

    result = 6;
    for (;;) {
        if (*(int32_t *)((uint8_t *)ta + TA_DONE) == 0) {
            rc = more == 1 ? ta_ProcessRemaining(ta) : ta_ProcessSentence(ta);
            switch (rc) {
            case -20:
                rp_setError(param, ROM_ERR_MEMORY);
                result = -1;
                break;
            case 0:
                break;
            case 1:
                if (jrz_GenerateResult(rz, 1) != 0
                    && jrz_SendResult(rz, out) != 0) {
                    RZ_L(rz, RZ_FRESH) = 1;
                    result = 2;
                } else {
                    rp_setError(param, ROM_ERR_MEMORY);
                    return -1;
                }
                break;
            case 2:
                RZ_L(rz, RZ_MORE) = 1;
                result = 1;
                break;
            case 3:
                if (anno->count > 0
                    && an_Flush(anno, rp_isAnnotationsInText(param),
                                (DynaBuf *)RZ_AT(rz, RZ_OUT_AT), 0) == 0) {
                    rp_setError(param, ROM_ERR_MEMORY);
                    return -1;
                }
                break;
            case 5:
                result = 3;
                break;
            default:
                result = -1;
                break;
            }
            if (rc > 0)
                break;
            continue;
        }

        {
            void *root = ta_GetPhraseTableRoot(ta);

            ip_InitPhraseTable(ip, TA_LINK_N);
            *(int16_t *)((uint8_t *)ip + IP_MORE) =
                (int16_t)*(int32_t *)((uint8_t *)ic + IC_PAUSE);
            ip_ThreePhraseParsing(ip, root);
        }
        if (jrz_GenerateResult(rz, more) != 0 && jrz_SendResult(rz, out) != 0) {
            if ((*out)[0] == '\0') {
                result = 1;
                continue;
            }
            result = 2;
            break;
        }
        rp_setError(param, ROM_ERR_MEMORY);
        return -1;
    }

    RZ_L(rz, RZ_BUSY) = 0;
    if (RZ_L(rz, RZ_STOPPED) != 0)
        result = 3;
    return result;
}

/* ---- making and unmaking one ------------------------------------------ */

/* How many instances there are, and the mutex that guards it. IBM keeps both
   in statics of the class: the dictionaries are opened by the first instance
   and given back by the last, so the count has to be right even where two
   instances are made at once. */
static int16_t jrz_instances;

/* One instance: the base half first, then the analysis, the intonation pass,
 * the user dictionary and the output buffer. Every one of them that cannot be
 * made sets the memory error and is left null rather than stopping the rest,
 * which is IBM's shape -- the caller finds out from the error rather than from
 * the constructor.
 */
void *jrz_ctor(void *rz, RomInstParam *param)
{
    void *ta;
    void *ip;
    void *ud;

    ci_initBase(rz, param);
    /* The base half calls three of these on itself -- the input manager asks
       the converter where in the output a mark belongs -- so the table has to
       be in place before anything else runs. */
    CI_VT(rz) = &JRZ_VTBL;

    evv_low_lock();
    if (jrz_instances == 0) {
        if (jrz_Init(rz) == 0)
            jrz_instances++;
    } else {
        jrz_instances++;
    }

    RZ_L(rz, RZ_UNREAD_MID) = 0;

    ta = cpp_new(TA_ROOM);
    RZ_AT(rz, RZ_TXTANAL_AT) = ta != NULL ? ta_ctor(ta, rz) : NULL;
    if (RZ_AT(rz, RZ_TXTANAL_AT) != NULL
        && ta_initialize(RZ_AT(rz, RZ_TXTANAL_AT)) != 0) {
        ta_ClearInputBuf(RZ_AT(rz, RZ_TXTANAL_AT));
        ta_ClearPhraseTable(RZ_AT(rz, RZ_TXTANAL_AT));
    } else {
        rp_setError(param, ROM_ERR_MEMORY);
    }

    ip = cpp_new(IP_ROOM);
    if (ip != NULL)
        *(void **)((uint8_t *)ip + IP_OWNER_AT) = rz;
    RZ_AT(rz, RZ_INTON_AT) = ip;
    if (RZ_AT(rz, RZ_INTON_AT) != NULL) {
        *(void **)((uint8_t *)RZ_AT(rz, RZ_INTON_AT) + IP_CUR_AT)  = NULL;
        *(void **)((uint8_t *)RZ_AT(rz, RZ_INTON_AT) + IP_HEAD_AT) = NULL;
    } else {
        rp_setError(param, ROM_ERR_MEMORY);
    }

    RZ_L(rz, RZ_UNREAD_MID + 0x0c) = 0;
    RZ_L(rz, RZ_VOICE)             = 0;
    RZ_L(rz, RZ_BASELINE)          = 0;
    RZ_L(rz, RZ_FLUENCY)           = 0;
    RZ_L(rz, RZ_RATE)              = 0;
    RZ_L(rz, RZ_SPEED)             = 0;
    RZ_L(rz, RZ_SPELL_ENGLISH)     = 0;
    RZ_L(rz, RZ_VOLUME)            = 0;
    RZ_L(rz, RZ_UNREAD_TAIL)       = 0;

    if (RZ_AT(rz, RZ_TXTANAL_AT) != NULL) {
        ud = cpp_new(sizeof(RomUserDict));
        RZ_AT(rz, RZ_USERDICT_AT) =
            ud != NULL
            ? (void *)rud_ctor((RomUserDict *)ud, RZ_AT(rz, RZ_TXTANAL_AT))
            : NULL;
        if (RZ_AT(rz, RZ_USERDICT_AT) == NULL)
            rp_setError(param, ROM_ERR_MEMORY);
    }

    RZ_AT(rz, RZ_OUT_AT) = dynaBufNew(0x4000);
    if (RZ_AT(rz, RZ_OUT_AT) == NULL)
        rp_setError(param, ROM_ERR_MEMORY);

    RZ_W(rz, RZ_MARK)  = -1;
    RZ_L(rz, RZ_MORE)  = 0;
    RZ_L(rz, RZ_FRESH) = 1;
    evv_low_unlock();
    return rz;
}

/* And unmaking one, in IBM's order: the intonation pass, the user dictionary,
   the analysis, then the count -- and the dictionaries with it where this was
   the last instance -- and last the output buffer. */
void jrz_dtor(void *rz)
{
    if (RZ_AT(rz, RZ_INTON_AT) != NULL) {
        ip_destroy(RZ_AT(rz, RZ_INTON_AT), 1);
        RZ_AT(rz, RZ_INTON_AT) = NULL;
    }
    if (RZ_AT(rz, RZ_USERDICT_AT) != NULL) {
        cpp_delete(RZ_AT(rz, RZ_USERDICT_AT));
        RZ_AT(rz, RZ_USERDICT_AT) = NULL;
    }
    if (RZ_AT(rz, RZ_TXTANAL_AT) != NULL) {
        ta_destroy(RZ_AT(rz, RZ_TXTANAL_AT), 1);
        RZ_AT(rz, RZ_TXTANAL_AT) = NULL;
    }

    evv_low_lock();
    jrz_instances--;
    if (jrz_instances == 0 && jrz_dictman != NULL) {
        if (*(char **)((uint8_t *)jrz_dictman + DM_PATH) != NULL)
            cpp_delete(*(char **)((uint8_t *)jrz_dictman + DM_PATH));
        cpp_delete(jrz_dictman);
        jrz_dictman = NULL;
    }
    evv_low_unlock();

    if (RZ_AT(rz, RZ_OUT_AT) != NULL) {
        dynaBufDelete((DynaBuf *)RZ_AT(rz, RZ_OUT_AT));
        RZ_AT(rz, RZ_OUT_AT) = NULL;
    }
}

void *jrz_destroy(void *rz, int32_t freeIt)
{
    jrz_dtor(rz);
    if (freeIt & 1)
        cpp_delete(rz);
    return rz;
}

/* ---- the three the base leaves to whoever derives from it -------------- */

/* Japanese takes any entry the caller offers. */
int32_t jrz_isValidUserDictEntry(void *rz, const char *s, int32_t a,
                                 int32_t b)
{
    (void)rz;
    (void)s;
    (void)a;
    (void)b;
    return 1;
}

/* And has no romaji form of a word to give back either way: both answer yes
   with nothing in the caller's pointer. */
int32_t jrz_mbcs2Rom(void *rz, const char *s, char **out)
{
    (void)rz;
    (void)s;
    *out = NULL;
    return 1;
}

int32_t jrz_rom2Mbcs(void *rz, const char *s, char **out)
{
    (void)rz;
    (void)s;
    *out = NULL;
    return 1;
}

/* The seven slots, at IBM's own numbers. */
const ConverterVtbl JRZ_VTBL = {
    (void *(*)(Converter *, int32_t))jrz_destroy,
    (int32_t (*)(Converter *, char **, int32_t))jrz_processSentence,
    (int32_t (*)(Converter *))jrz_getOffset,
    (void (*)(Converter *))jrz_ResetBuffer,
    (int32_t (*)(Converter *, const char *, int32_t, int32_t))
        jrz_isValidUserDictEntry,
    (int32_t (*)(Converter *, const char *, char **))jrz_mbcs2Rom,
    (int32_t (*)(Converter *, const char *, char **))jrz_rom2Mbcs
};
