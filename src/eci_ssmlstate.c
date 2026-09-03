/* Everything the reader has to remember while it reads.
 *
 * SSML nests, and every element that changes something has to put back what
 * it changed when it closes. So there is a stack for each: the voice
 * number, the volume, the pitch, the range, the rate, the emphasis and the
 * audio source, and one for the language. What is on top is what is in
 * force; closing an element pops it and the reader writes an annotation to
 * put the engine back.
 *
 * Beside the stacks there are counters rather than flags. `<say-as
 * interpret-as="date">' inside another one of the same kind has to close
 * twice before dates stop being dates, so the reader counts rather than
 * sets, and every counter has a `set', a `rel' and an `is'. A `rel' with
 * nothing to release, or a `set' from below nought, is a document that
 * cannot be read and records a syntax error.
 *
 * The answer is built up here too: `addToFilteredText' is what every
 * handler in src/eci_ssmlprocessor.c writes through, and the buffer doubles
 * as it fills. Once any error is set nothing more is added and
 * `getFilteredText' answers nothing at all, which is why a document with
 * one bad attribute in it comes out empty rather than partly read.
 *
 * Three things are the original's and are kept. The three error flags are
 * never cleared except by a whole reset, so one bad element spoils the
 * document. Every stack answers through `isValid', which calls a stack
 * exactly full invalid -- see src/eci_ssmlintstack.c -- so the twentieth
 * element of a kind records a syntax error rather than being pushed. And
 * `resetFilterState' answers false when there is no environment and no
 * values were set by hand, which is what makes the whole reader refuse to
 * run before anything has told it what the engine's rate and voice are.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "evv_abi.h"
#include "eci_ssml.h"
#include "eci_ssmlstate.h"

extern void *cpp_new(uint32_t n) MANGLED("??2@YAPAXI@Z");
extern void  cpp_delete(void *p) MANGLED("??3@YAXPAX@Z");

/* What the answer's buffer starts at, and the scratch beside it. */
#define FILTERED_ROOM  0x400
#define TMP_ROOM       0x400

/* What the reader falls back on when nothing has told it otherwise: voice
   one, US English, and the volume voice one carries. */
#define DEFAULT_VOICE   1
#define DEFAULT_LANG    0x10000
#define DEFAULT_VOLUME  0x5c

/* Which parameters the environment is asked for. The first two are the
   published numbers; the third is one of IBM's own that its header does
   not carry, and the environment maps it onto the state's voice. */
#define ECI_VOLUME_VOICE_PARAM  7
#define ECI_LANGUAGE_PARAM      9
#define ECI_VOICE_PARAM         0x11

/* An environment is reached through two entries, and the order is the
   original's rather than the one its names suggest.

   Both are methods, so both are thiscall, and on thirty-two bits that is
   not decoration: the environment goes over in a register there and on the
   stack here. Declared without it this built and ran on sixty-four bits,
   where there is one convention, and smashed the stack the moment a
   thirty-two bit build read a document. */
typedef struct {
    THIS int32_t (*getVoiceParam)(void *env, int32_t which);
    THIS int32_t (*getParam)(void *env, int32_t which);
} ECIEnvironmentTable;

typedef struct {
    const ECIEnvironmentTable *table;
} ECIEnvironment;

/* ---- the three errors ------------------------------------------------- */

THIS void ss_setErrorSyntax(SSMLState *s)
{
    s->errorSyntax = 1;
}

THIS int8_t ss_getErrorSyntax(SSMLState *s)
{
    return s->errorSyntax;
}

THIS void ss_setErrorPhoneme(SSMLState *s)
{
    s->errorPhoneme = 1;
}

THIS int8_t ss_getErrorPhoneme(SSMLState *s)
{
    return s->errorPhoneme;
}

THIS void ss_setErrorMalloc(SSMLState *s)
{
    s->errorMalloc = 1;
}

THIS int8_t ss_getErrorMalloc(SSMLState *s)
{
    return s->errorMalloc;
}

THIS int8_t ss_getErrorSet(SSMLState *s)
{
    return s->errorSyntax || s->errorMalloc || s->errorPhoneme;
}

/* ---- the counters ---------------------------------------------------- */

/* Each of these is a nesting depth rather than a flag, and each refuses to
   go below nought or to be released when nothing is open. */
#define COUNTER(name, field)                                              \
    THIS void ss_set##name(SSMLState *s)                                  \
    {                                                                     \
        if (s->field < 0)                                                 \
            ss_setErrorSyntax(s);                                         \
        else                                                              \
            s->field++;                                                   \
    }                                                                     \
    THIS void ss_rel##name(SSMLState *s)                                  \
    {                                                                     \
        if (s->field <= 0)                                                \
            ss_setErrorSyntax(s);                                         \
        else                                                              \
            s->field--;                                                   \
    }

COUNTER(BlockAddText, blockAddText)
COUNTER(SpellOut, spellOut)

THIS int8_t ss_canAddTextBlock(SSMLState *s)
{
    return s->blockAddText == 0;
}

THIS int8_t ss_isSpellOut(SSMLState *s)
{
    return s->spellOut > 0;
}

/* Whether a word may be added at all: nothing may be blocking it and the
   language in force has to be one the engine has. */
THIS int8_t ss_canAddText(SSMLState *s)
{
    LanguageId top;

    if (s->blockAddText != 0)
        return 0;
    top = ss_peekLang(s);
    return li_getIsLanguageAvailable(&top) ? 1 : 0;
}

/* The end of a structural element, which the reader uses to decide whether
   a full stop is wanted. This one is a flag rather than a count. */
THIS void ss_setEndStruct(SSMLState *s)
{
    s->endStruct = 1;
}

THIS void ss_relEndStruct(SSMLState *s)
{
    s->endStruct = 0;
}

THIS int8_t ss_getEndStruct(SSMLState *s)
{
    return s->endStruct;
}

THIS void ss_setSpellAddSpace(SSMLState *s, int8_t yes)
{
    s->spellAddSpace = yes;
}

THIS int8_t ss_isSpellAddSpace(SSMLState *s)
{
    return s->spellAddSpace;
}

/* ---- the answer ------------------------------------------------------ */

/* Everything the handlers write goes through here. The buffer doubles when
   it will not fit; running out of memory is recorded and nothing more is
   added.
 *
 * The space before a digit is what spelling out needs: the engine reads
 * `1 2 3' as three digits and `123' as a number, so a run of characters
 * being spelt out asks for a space in front of each and this puts one there
 * when the next thing is a digit. */
THIS int32_t ss_addToFilteredText(SSMLState *s, const char *text,
                                  int32_t length)
{
    if (ss_getErrorSet(s) || !ss_canAddText(s) || text == 0)
        return 0;

    if (s->filteredRoom <= s->filteredLength + length + 1) {
        char *fresh;

        s->filteredRoom = (s->filteredRoom + length) * 2;
        fresh = malloc((size_t)s->filteredRoom);
        if (fresh == 0) {
            ss_setErrorMalloc(s);
            return 0;
        }
        strcpy(fresh, s->filteredText);
        free(s->filteredText);
        s->filteredText = fresh;
    }

    if (ss_isSpellAddSpace(s) && isdigit((unsigned char)text[0])) {
        strcat(s->filteredText, " ");
        s->filteredLength++;
        ss_setSpellAddSpace(s, 0);
    } else if (ss_isSpellAddSpace(s)) {
        ss_setSpellAddSpace(s, 0);
    }

    strcpy(s->filteredText + s->filteredLength, text);
    s->filteredLength += length;
    s->filteredText[s->filteredLength] = 0;
    return length;
}

THIS int32_t ss_getFilteredTextLength(SSMLState *s)
{
    return s->filteredLength;
}

/* Nothing at all once an error is set, which is what makes a document with
   one bad attribute in it come out empty rather than half read. */
THIS char *ss_getFilteredText(SSMLState *s)
{
    if (ss_getErrorSet(s))
        return 0;
    return s->filteredText;
}

THIS void ss_resetFilterText(SSMLState *s)
{
    s->filteredRoom = FILTERED_ROOM;
    s->filteredLength = 0;
    if (s->filteredText != 0)
        free(s->filteredText);
    s->filteredText = malloc((size_t)s->filteredRoom + 1);
    strcpy(s->filteredText, "");
}

/* ---- the scratch buffer ---------------------------------------------- */

THIS char *ss_getTmpBuffer(SSMLState *s)
{
    return s->tmpBuffer;
}

THIS int32_t ss_getTmpBufferSize(SSMLState *s)
{
    return s->tmpRoom;
}

THIS int32_t ss_reallocTmpBuffer(SSMLState *s, int32_t want)
{
    if (s->tmpBuffer != 0 && want > s->tmpRoom) {
        free(s->tmpBuffer);
        s->tmpBuffer = malloc((size_t)want);
        s->tmpRoom = want;
    }
    return s->tmpBuffer != 0;
}

/* ---- the eight stacks ------------------------------------------------ */

/* Every one of them is the same three questions and three answers, and a
   stack that says it is not valid records a syntax error rather than
   losing what it was given quietly. */
#define INT_STACK(name, field)                                            \
    THIS void ss_push##name(SSMLState *s, int32_t v)                      \
    {                                                                     \
        if (sis_isValid(s->field))                                        \
            sis_push(s->field, v);                                        \
        else                                                              \
            ss_setErrorSyntax(s);                                         \
    }                                                                     \
    THIS int32_t ss_pop##name(SSMLState *s)                               \
    {                                                                     \
        if (sis_isValid(s->field) && !sis_isEmpty(s->field))              \
            return sis_pop(s->field);                                     \
        ss_setErrorSyntax(s);                                             \
        return -1;                                                        \
    }                                                                     \
    THIS int32_t ss_peek##name(SSMLState *s)                              \
    {                                                                     \
        if (sis_isValid(s->field) && !sis_isEmpty(s->field))              \
            return sis_peek(s->field);                                    \
        ss_setErrorSyntax(s);                                             \
        return -1;                                                        \
    }                                                                     \
    THIS int8_t ss_valid##name(SSMLState *s)                              \
    {                                                                     \
        return sis_isValid(s->field);                                     \
    }                                                                     \
    THIS int8_t ss_isEmpty##name(SSMLState *s)                            \
    {                                                                     \
        return sis_isEmpty(s->field);                                     \
    }                                                                     \
    THIS int32_t ss_size##name(SSMLState *s)                              \
    {                                                                     \
        return sis_stackSize(s->field);                                   \
    }

#define STR_STACK(name, field)                                            \
    THIS void ss_push##name(SSMLState *s, char *v)                        \
    {                                                                     \
        if (sss_isValid(s->field))                                        \
            sss_push(s->field, v);                                        \
        else                                                              \
            ss_setErrorSyntax(s);                                         \
    }                                                                     \
    THIS char *ss_pop##name(SSMLState *s)                                 \
    {                                                                     \
        if (sss_isValid(s->field) && !sss_isEmpty(s->field))              \
            return sss_pop(s->field);                                     \
        ss_setErrorSyntax(s);                                             \
        return 0;                                                         \
    }                                                                     \
    THIS char *ss_peek##name(SSMLState *s)                                \
    {                                                                     \
        if (sss_isValid(s->field) && !sss_isEmpty(s->field))              \
            return sss_peek(s->field);                                    \
        ss_setErrorSyntax(s);                                             \
        return 0;                                                         \
    }                                                                     \
    THIS char *ss_peek##name##At(SSMLState *s, int32_t which)              \
    {                                                                     \
        return sss_peekAt(s->field, which);                               \
    }                                                                     \
    THIS int8_t ss_valid##name(SSMLState *s)                              \
    {                                                                     \
        return sss_isValid(s->field);                                     \
    }                                                                     \
    THIS int8_t ss_isEmpty##name(SSMLState *s)                            \
    {                                                                     \
        return sss_isEmpty(s->field);                                     \
    }                                                                     \
    THIS int32_t ss_size##name(SSMLState *s)                              \
    {                                                                     \
        return sss_stackSize(s->field);                                   \
    }

INT_STACK(VoiceNumber, voiceNumber)
INT_STACK(VoiceVolume, voiceVolume)
STR_STACK(VoicePitch, voicePitch)
STR_STACK(VoiceRange, voiceRange)
STR_STACK(VoiceSpeed, voiceSpeed)
STR_STACK(Emphasis, emphasis)
STR_STACK(Audio, audio)

THIS void ss_pushLang(SSMLState *s, LanguageId v)
{
    if (sls_isValid(s->lang))
        sls_push(s->lang, v);
    else
        ss_setErrorSyntax(s);
}

THIS LanguageId ss_popLang(SSMLState *s)
{
    LanguageId out;

    if (sls_isValid(s->lang) && !sls_isEmpty(s->lang))
        return sls_pop(s->lang);
    ss_setErrorSyntax(s);
    li_initPacked(&out, 0);
    return out;
}

THIS LanguageId ss_peekLang(SSMLState *s)
{
    LanguageId out;

    if (sls_isValid(s->lang) && !sls_isEmpty(s->lang))
        return sls_peek(s->lang);
    ss_setErrorSyntax(s);
    li_initPacked(&out, 0);
    return out;
}

THIS int8_t ss_validLang(SSMLState *s)
{
    return sls_isValid(s->lang);
}

THIS int8_t ss_isEmptyLang(SSMLState *s)
{
    return sls_isEmpty(s->lang);
}

/* ---- what the engine was doing before the document ------------------- */

/* Each of these three drains its stack and puts one value on it, so that
   what a closing element pops back to is the engine's own setting. */
THIS void ss_setEnvironmentVoice(SSMLState *s, int32_t voice)
{
    while (!sis_isEmpty(s->voiceNumber))
        ss_popVoiceNumber(s);
    ss_pushVoiceNumber(s, voice);
}

THIS void ss_setEnvironmentVolume(SSMLState *s, int32_t volume)
{
    while (!sis_isEmpty(s->voiceVolume))
        ss_popVoiceVolume(s);
    ss_pushVoiceVolume(s, volume);
}

/* The language the engine is in is always marked available, whether or not
   it is one the engine could name; a document that asks for another one is
   what `isLanguageAvailable' is for. */
THIS void ss_setEnvironmentLang(SSMLState *s, int32_t packed)
{
    LanguageId id;

    li_initPacked(&id, packed);
    li_setIsLanguageAvailable(&id, 1);

    while (!sls_isEmpty(s->lang))
        ss_popLang(s);
    ss_pushLang(s, id);
}

THIS void ss_setEnvironment(SSMLState *s, void *env)
{
    s->env = env;
}

/* Ask the environment what the engine is set to. Answers false only when
   there is neither an environment nor a set of values put in by hand. */
THIS int8_t ss_setFilterEnv(SSMLState *s)
{
    ECIEnvironment *env = s->env;

    if (env != 0) {
        ss_setEnvironmentLang(s,
            env->table->getParam(env, ECI_LANGUAGE_PARAM));
        ss_setEnvironmentVoice(s,
            env->table->getParam(env, ECI_VOICE_PARAM));
        ss_setEnvironmentVolume(s,
            env->table->getVoiceParam(env, ECI_VOLUME_VOICE_PARAM));
        return 1;
    }

    if (s->envSet) {
        ss_setEnvironmentLang(s, s->envLang);
        ss_setEnvironmentVoice(s, s->envVoice);
        ss_setEnvironmentVolume(s, s->envVolume);
        return 1;
    }

    return 0;
}

/* And the same three put in by hand, which is how a caller with no
   environment tells the reader what the engine is doing. */
THIS int8_t ss_setFilterEnvValues(SSMLState *s, int32_t voice, int32_t lang,
                                  int32_t volume)
{
    ss_setEnvironmentVoice(s, voice);
    ss_setEnvironmentLang(s, lang);
    ss_setEnvironmentVolume(s, volume);
    s->envVoice = voice;
    s->envVolume = volume;
    s->envLang = lang;
    s->envSet = 1;
    return 1;
}

/* ---- the voices the engine has --------------------------------------- */

THIS int32_t ss_getVoiceInfo(SSMLState *s, VOICE_INFO *out, int32_t which)
{
    if (s->voices == 0)
        return -1;
    return cvi_getVoiceInfo(s->voices, out, which);
}

/* ---- starting again -------------------------------------------------- */

/* Everything but the answer's buffer, which resetFilterText does. The
   stacks and the voice table go and are made again, so a document cannot
   inherit anything from the one before it. */
THIS int8_t ss_resetFilterState(SSMLState *s)
{
    int8_t ok = 1;

    s->sayAsVXMLcurrency = 0;
    s->sayAsVXMLdate = 0;
    s->sayAsBoolean = 0;
    s->sayAsNumber = 0;
    s->sayAsDate = 0;
    s->spellOut = 0;
    s->blockAddText = 0;
    s->errorSyntax = 0;
    s->errorPhoneme = 0;
    s->errorMalloc = 0;
    s->endStruct = 0;
    s->spellAddSpace = 0;

    if (s->voiceNumber != 0) {
        sis_delete(s->voiceNumber);
        s->voiceNumber = 0;
    }
    if (s->lang != 0) {
        sls_delete(s->lang);
        s->lang = 0;
    }
    if (s->voicePitch != 0) {
        sss_delete(s->voicePitch);
        s->voicePitch = 0;
    }
    if (s->voiceRange != 0) {
        sss_delete(s->voiceRange);
        s->voiceRange = 0;
    }
    if (s->voiceSpeed != 0) {
        sss_delete(s->voiceSpeed);
        s->voiceSpeed = 0;
    }
    if (s->voiceVolume != 0) {
        sis_delete(s->voiceVolume);
        s->voiceVolume = 0;
    }
    if (s->emphasis != 0) {
        sss_delete(s->emphasis);
        s->emphasis = 0;
    }
    if (s->audio != 0) {
        sss_delete(s->audio);
        s->audio = 0;
    }
    if (s->voices != 0) {
        cvi_delete(s->voices);
        s->voices = 0;
    }

    s->voiceNumber = cpp_new(sizeof *s->voiceNumber);
    if (s->voiceNumber != 0)
        sis_ctor(s->voiceNumber);
    s->lang = cpp_new(sizeof *s->lang);
    if (s->lang != 0)
        sls_ctor(s->lang);
    s->voicePitch = cpp_new(sizeof *s->voicePitch);
    if (s->voicePitch != 0)
        sss_ctor(s->voicePitch);
    s->voiceRange = cpp_new(sizeof *s->voiceRange);
    if (s->voiceRange != 0)
        sss_ctor(s->voiceRange);
    s->voiceSpeed = cpp_new(sizeof *s->voiceSpeed);
    if (s->voiceSpeed != 0)
        sss_ctor(s->voiceSpeed);
    s->voiceVolume = cpp_new(sizeof *s->voiceVolume);
    if (s->voiceVolume != 0)
        sis_ctor(s->voiceVolume);
    s->audio = cpp_new(sizeof *s->audio);
    if (s->audio != 0)
        sss_ctor(s->audio);
    s->emphasis = cpp_new(sizeof *s->emphasis);
    if (s->emphasis != 0)
        sss_ctor(s->emphasis);

    s->voices = cpp_new(sizeof *s->voices);
    if (s->voices != 0)
        cvi_ctor(s->voices);
    if (s->voices != 0)
        cvi_initVoicesInfo(s->voices);

    if (s->env != 0) {
        ss_setFilterEnv(s);
    } else if (s->envSet) {
        ss_pushVoiceNumber(s, s->envVoice);
        {
            LanguageId id;

            li_initPacked(&id, s->envLang);
            ss_pushLang(s, id);
        }
        ss_pushVoiceVolume(s, s->envVolume);
    } else {
        /* Nothing has said what the engine is doing, so the reader is
           given something to pop back to and answers false. */
        LanguageId id;

        ss_pushVoiceNumber(s, DEFAULT_VOICE);
        li_initPacked(&id, DEFAULT_LANG);
        li_setIsLanguageAvailable(&id, 1);
        ss_pushLang(s, id);
        ss_pushVoiceVolume(s, DEFAULT_VOLUME);
        ok = 0;
    }

    return ok;
}

/* ---- making one and dropping one ------------------------------------- */

THIS void ss_ctor(SSMLState *s)
{
    s->filteredText = 0;
    s->tmpBuffer = 0;
    s->spellAddSpace = 0;
    s->tmpRoom = TMP_ROOM;
    s->voiceNumber = 0;
    s->voiceVolume = 0;
    s->voicePitch = 0;
    s->voiceRange = 0;
    s->voiceSpeed = 0;
    s->lang = 0;
    s->audio = 0;
    s->emphasis = 0;
    s->env = 0;
    s->voices = 0;

    s->tmpBuffer = malloc((size_t)s->tmpRoom);
    ss_resetFilterText(s);
    ss_resetFilterState(s);
}

THIS void ss_dtor(SSMLState *s)
{
    if (s->filteredText != 0) {
        free(s->filteredText);
        s->filteredText = 0;
    }
    if (s->tmpBuffer != 0) {
        free(s->tmpBuffer);
        s->tmpBuffer = 0;
    }
    if (s->voiceNumber != 0) {
        sis_delete(s->voiceNumber);
        s->voiceNumber = 0;
    }
    if (s->lang != 0) {
        sls_delete(s->lang);
        s->lang = 0;
    }
    if (s->voicePitch != 0) {
        sss_delete(s->voicePitch);
        s->voicePitch = 0;
    }
    if (s->voiceRange != 0) {
        sss_delete(s->voiceRange);
        s->voiceRange = 0;
    }
    if (s->voiceSpeed != 0) {
        sss_delete(s->voiceSpeed);
        s->voiceSpeed = 0;
    }
    if (s->voiceVolume != 0) {
        sis_delete(s->voiceVolume);
        s->voiceVolume = 0;
    }
    if (s->audio != 0) {
        sss_delete(s->audio);
        s->audio = 0;
    }
    if (s->emphasis != 0) {
        sss_delete(s->emphasis);
        s->emphasis = 0;
    }
    if (s->voices != 0) {
        cvi_delete(s->voices);
        s->voices = 0;
    }
}

THIS void ss_delete(SSMLState *s)
{
    if (s == 0)
        return;
    ss_dtor(s);
    cpp_delete(s);
}

ALIAS("??0SSMLState@@QAE@XZ", "ss_ctor");
ALIAS("??1SSMLState@@QAE@XZ", "ss_dtor");
ALIAS("?deleteSSMLState@SSMLState@@QAEXXZ", "ss_delete");
ALIAS("?setBlockAddText@SSMLState@@QAEXXZ", "ss_setBlockAddText");
ALIAS("?relBlockAddText@SSMLState@@QAEXXZ", "ss_relBlockAddText");
ALIAS("?canAddTextBlock@SSMLState@@QAE_NXZ", "ss_canAddTextBlock");
ALIAS("?canAddText@SSMLState@@QAE_NXZ", "ss_canAddText");
ALIAS("?setSpellOut@SSMLState@@QAEXXZ", "ss_setSpellOut");
ALIAS("?relSpellOut@SSMLState@@QAEXXZ", "ss_relSpellOut");
ALIAS("?isSpellOut@SSMLState@@QAE_NXZ", "ss_isSpellOut");
ALIAS("?setErrorSyntax@SSMLState@@QAEXXZ", "ss_setErrorSyntax");
ALIAS("?getErrorSyntax@SSMLState@@QAE_NXZ", "ss_getErrorSyntax");
ALIAS("?setErrorPhoneme@SSMLState@@QAEXXZ", "ss_setErrorPhoneme");
ALIAS("?getErrorPhoneme@SSMLState@@QAE_NXZ", "ss_getErrorPhoneme");
ALIAS("?setErrorMalloc@SSMLState@@QAEXXZ", "ss_setErrorMalloc");
ALIAS("?getErrorMalloc@SSMLState@@QAE_NXZ", "ss_getErrorMalloc");
ALIAS("?getErrorSet@SSMLState@@QAE_NXZ", "ss_getErrorSet");
ALIAS("?setEndStruct@SSMLState@@QAEXXZ", "ss_setEndStruct");
ALIAS("?relEndStruct@SSMLState@@QAEXXZ", "ss_relEndStruct");
ALIAS("?getEndStruct@SSMLState@@QAE_NXZ", "ss_getEndStruct");
ALIAS("?addToFilteredText@SSMLState@@QAEHPBDH@Z", "ss_addToFilteredText");
ALIAS("?getFilteredTextLength@SSMLState@@QAEHXZ",
      "ss_getFilteredTextLength");
ALIAS("?getFilteredText@SSMLState@@QAEPADXZ", "ss_getFilteredText");
ALIAS("?resetFilterText@SSMLState@@QAEXXZ", "ss_resetFilterText");
ALIAS("?resetFilterState@SSMLState@@QAE_NXZ", "ss_resetFilterState");
ALIAS("?setFilterEnv@SSMLState@@QAE_NXZ", "ss_setFilterEnv");
ALIAS("?setFilterEnv@SSMLState@@QAE_NHW4ECILanguageDialect@@H@Z",
      "ss_setFilterEnvValues");
ALIAS("?setEnvironment@SSMLState@@QAEXPAVECIEnvironment@@@Z",
      "ss_setEnvironment");
ALIAS("?setEnvironmentVoice@SSMLState@@QAEXH@Z", "ss_setEnvironmentVoice");
ALIAS("?setEnvironmentLang@SSMLState@@QAEXW4ECILanguageDialect@@@Z",
      "ss_setEnvironmentLang");
ALIAS("?setEnvironmentVolume@SSMLState@@QAEXH@Z", "ss_setEnvironmentVolume");
ALIAS("?pushVoiceNumber@SSMLState@@QAEXH@Z", "ss_pushVoiceNumber");
ALIAS("?popVoiceNumber@SSMLState@@QAEHXZ", "ss_popVoiceNumber");
ALIAS("?peekVoiceNumber@SSMLState@@QAEHXZ", "ss_peekVoiceNumber");
ALIAS("?validVoiceNumber@SSMLState@@QAE_NXZ", "ss_validVoiceNumber");
ALIAS("?isEmptyVoiceNumber@SSMLState@@QAE_NXZ", "ss_isEmptyVoiceNumber");
ALIAS("?pushLang@SSMLState@@QAEXVLanguageId@@@Z", "ss_pushLang");
ALIAS("?popLang@SSMLState@@QAE?AVLanguageId@@XZ", "ss_popLang");
ALIAS("?peekLang@SSMLState@@QAE?AVLanguageId@@XZ", "ss_peekLang");
ALIAS("?validLang@SSMLState@@QAE_NXZ", "ss_validLang");
ALIAS("?isEmptyLang@SSMLState@@QAE_NXZ", "ss_isEmptyLang");
ALIAS("?pushVoicePitch@SSMLState@@QAEXPAD@Z", "ss_pushVoicePitch");
ALIAS("?popVoicePitch@SSMLState@@QAEPADXZ", "ss_popVoicePitch");
ALIAS("?peekVoicePitch@SSMLState@@QAEPADXZ", "ss_peekVoicePitch");
ALIAS("?peekVoicePitch@SSMLState@@QAEPADH@Z", "ss_peekVoicePitchAt");
ALIAS("?validVoicePitch@SSMLState@@QAE_NXZ", "ss_validVoicePitch");
ALIAS("?isEmptyVoicePitch@SSMLState@@QAE_NXZ", "ss_isEmptyVoicePitch");
ALIAS("?sizeVoicePitch@SSMLState@@QAEHXZ", "ss_sizeVoicePitch");
ALIAS("?pushVoiceRange@SSMLState@@QAEXPAD@Z", "ss_pushVoiceRange");
ALIAS("?popVoiceRange@SSMLState@@QAEPADXZ", "ss_popVoiceRange");
ALIAS("?peekVoiceRange@SSMLState@@QAEPADXZ", "ss_peekVoiceRange");
ALIAS("?peekVoiceRange@SSMLState@@QAEPADH@Z", "ss_peekVoiceRangeAt");
ALIAS("?validVoiceRange@SSMLState@@QAE_NXZ", "ss_validVoiceRange");
ALIAS("?isEmptyVoiceRange@SSMLState@@QAE_NXZ", "ss_isEmptyVoiceRange");
ALIAS("?sizeVoiceRange@SSMLState@@QAEHXZ", "ss_sizeVoiceRange");
ALIAS("?pushVoiceSpeed@SSMLState@@QAEXPAD@Z", "ss_pushVoiceSpeed");
ALIAS("?popVoiceSpeed@SSMLState@@QAEPADXZ", "ss_popVoiceSpeed");
ALIAS("?peekVoiceSpeed@SSMLState@@QAEPADXZ", "ss_peekVoiceSpeed");
ALIAS("?peekVoiceSpeed@SSMLState@@QAEPADH@Z", "ss_peekVoiceSpeedAt");
ALIAS("?validVoiceSpeed@SSMLState@@QAE_NXZ", "ss_validVoiceSpeed");
ALIAS("?isEmptyVoiceSpeed@SSMLState@@QAE_NXZ", "ss_isEmptyVoiceSpeed");
ALIAS("?sizeVoiceSpeed@SSMLState@@QAEHXZ", "ss_sizeVoiceSpeed");
ALIAS("?pushVoiceVolume@SSMLState@@QAEXH@Z", "ss_pushVoiceVolume");
ALIAS("?popVoiceVolume@SSMLState@@QAEHXZ", "ss_popVoiceVolume");
ALIAS("?peekVoiceVolume@SSMLState@@QAEHXZ", "ss_peekVoiceVolume");
ALIAS("?validVoiceVolume@SSMLState@@QAE_NXZ", "ss_validVoiceVolume");
ALIAS("?isEmptyVoiceVolume@SSMLState@@QAE_NXZ", "ss_isEmptyVoiceVolume");
ALIAS("?sizeVoiceVolume@SSMLState@@QAEHXZ", "ss_sizeVoiceVolume");
ALIAS("?pushEmphasis@SSMLState@@QAEXPAD@Z", "ss_pushEmphasis");
ALIAS("?popEmphasis@SSMLState@@QAEPADXZ", "ss_popEmphasis");
ALIAS("?peekEmphasis@SSMLState@@QAEPADH@Z", "ss_peekEmphasisAt");
ALIAS("?validEmphasis@SSMLState@@QAE_NXZ", "ss_validEmphasis");
ALIAS("?isEmptyEmphasis@SSMLState@@QAE_NXZ", "ss_isEmptyEmphasis");
ALIAS("?sizeEmphasis@SSMLState@@QAEHXZ", "ss_sizeEmphasis");
ALIAS("?pushAudio@SSMLState@@QAEXPAD@Z", "ss_pushAudio");
ALIAS("?popAudio@SSMLState@@QAEPADXZ", "ss_popAudio");
ALIAS("?peekAudio@SSMLState@@QAEPADXZ", "ss_peekAudio");
ALIAS("?validAudio@SSMLState@@QAE_NXZ", "ss_validAudio");
ALIAS("?isEmptyAudio@SSMLState@@QAE_NXZ", "ss_isEmptyAudio");
ALIAS("?sizeAudio@SSMLState@@QAEHXZ", "ss_sizeAudio");
ALIAS("?setSpellAddSpace@SSMLState@@QAEX_N@Z", "ss_setSpellAddSpace");
ALIAS("?isSpellAddSpace@SSMLState@@QAE_NXZ", "ss_isSpellAddSpace");
ALIAS("?GetVoiceInfo@SSMLState@@QAEHPAUVOICE_INFO@@H@Z", "ss_getVoiceInfo");
ALIAS("?getTmpBuffer@SSMLState@@QAEPADXZ", "ss_getTmpBuffer");
ALIAS("?reallocTmpBuffer@SSMLState@@QAEHH@Z", "ss_reallocTmpBuffer");
ALIAS("?getTmpBufferSize@SSMLState@@QAEHXZ", "ss_getTmpBufferSize");
