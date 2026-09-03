/* What the SSML reader remembers while it reads one document.
 *
 * src/eci_ssmlstate.c is the whole of it, with the five say-as counters in
 * src/eci_ssmlsayas.c beside it because that is how IBM's objects divide.
 * The tag handlers in src/eci_ssmlprocessor.c write through nothing else.
 */

#ifndef ECI_SSMLSTATE_H
#define ECI_SSMLSTATE_H

#include <stdint.h>
#include "eci_ssml.h"
#include "eci_vcinfo.h"

typedef struct {
    char          *filteredText;   /* the answer, as it is built up      */
    char          *tmpBuffer;      /* scratch the handlers borrow        */
    int32_t        blockAddText;   /* how deep in something silent       */
    int32_t        spellOut;       /* how deep in a spelt-out run        */
    int32_t        sayAsDate;
    int32_t        sayAsNumber;
    int32_t        sayAsBoolean;
    int32_t        sayAsVXMLdate;
    int32_t        sayAsVXMLcurrency;
    int8_t         errorSyntax;
    int8_t         errorPhoneme;
    int8_t         errorMalloc;
    int8_t         endStruct;
    int8_t         spellAddSpace;  /* a space wanted before the next     */
    int8_t         envSet;         /* the three below were put in by hand */
    int32_t        filteredLength;
    int32_t        filteredRoom;
    int32_t        tmpRoom;
    SSMLIntStack  *voiceNumber;
    SSMLIntStack  *voiceVolume;
    SSMLStrStack  *voicePitch;
    SSMLStrStack  *voiceRange;
    SSMLStrStack  *voiceSpeed;
    SSMLLangStack *lang;
    SSMLStrStack  *audio;
    SSMLStrStack  *emphasis;
    void          *env;            /* what the engine is set to          */
    int32_t        envVoice;
    int32_t        envVolume;
    int32_t        envLang;
    CVoicesInfo   *voices;
} SSMLState;

THIS void    ss_ctor(SSMLState *s);
THIS void    ss_dtor(SSMLState *s);
THIS void    ss_delete(SSMLState *s);

THIS void    ss_setErrorSyntax(SSMLState *s);
THIS int8_t  ss_getErrorSyntax(SSMLState *s);
THIS void    ss_setErrorPhoneme(SSMLState *s);
THIS int8_t  ss_getErrorPhoneme(SSMLState *s);
THIS void    ss_setErrorMalloc(SSMLState *s);
THIS int8_t  ss_getErrorMalloc(SSMLState *s);
THIS int8_t  ss_getErrorSet(SSMLState *s);

THIS void    ss_setBlockAddText(SSMLState *s);
THIS void    ss_relBlockAddText(SSMLState *s);
THIS int8_t  ss_canAddTextBlock(SSMLState *s);
THIS int8_t  ss_canAddText(SSMLState *s);
THIS void    ss_setSpellOut(SSMLState *s);
THIS void    ss_relSpellOut(SSMLState *s);
THIS int8_t  ss_isSpellOut(SSMLState *s);
THIS void    ss_setEndStruct(SSMLState *s);
THIS void    ss_relEndStruct(SSMLState *s);
THIS int8_t  ss_getEndStruct(SSMLState *s);
THIS void    ss_setSpellAddSpace(SSMLState *s, int8_t yes);
THIS int8_t  ss_isSpellAddSpace(SSMLState *s);

THIS int32_t ss_addToFilteredText(SSMLState *s, const char *text, int32_t length);
THIS int32_t ss_getFilteredTextLength(SSMLState *s);
THIS char   *ss_getFilteredText(SSMLState *s);
THIS void    ss_resetFilterText(SSMLState *s);
THIS int8_t  ss_resetFilterState(SSMLState *s);

THIS char   *ss_getTmpBuffer(SSMLState *s);
THIS int32_t ss_getTmpBufferSize(SSMLState *s);
THIS int32_t ss_reallocTmpBuffer(SSMLState *s, int32_t want);

THIS int8_t  ss_setFilterEnv(SSMLState *s);
THIS int8_t  ss_setFilterEnvValues(SSMLState *s, int32_t voice, int32_t lang,
                              int32_t volume);
THIS void    ss_setEnvironment(SSMLState *s, void *env);
THIS void    ss_setEnvironmentVoice(SSMLState *s, int32_t voice);
THIS void    ss_setEnvironmentVolume(SSMLState *s, int32_t volume);
THIS void    ss_setEnvironmentLang(SSMLState *s, int32_t packed);

THIS int32_t ss_getVoiceInfo(SSMLState *s, VOICE_INFO *out, int32_t which);

/* The eight stacks. Each is push, pop, peek, valid, empty and size, and
   the string ones also answer by position from the bottom. */
#define SS_INT_STACK(name)                                           \
    THIS void    ss_push##name(SSMLState *s, int32_t v);             \
    THIS int32_t ss_pop##name(SSMLState *s);                         \
    THIS int32_t ss_peek##name(SSMLState *s);                        \
    THIS int8_t  ss_valid##name(SSMLState *s);                       \
    THIS int8_t  ss_isEmpty##name(SSMLState *s);                     \
    THIS int32_t ss_size##name(SSMLState *s);

#define SS_STR_STACK(name)                                           \
    THIS void    ss_push##name(SSMLState *s, char *v);               \
    THIS char   *ss_pop##name(SSMLState *s);                         \
    THIS char   *ss_peek##name(SSMLState *s);                        \
    THIS char   *ss_peek##name##At(SSMLState *s, int32_t which);     \
    THIS int8_t  ss_valid##name(SSMLState *s);                       \
    THIS int8_t  ss_isEmpty##name(SSMLState *s);                     \
    THIS int32_t ss_size##name(SSMLState *s);

SS_INT_STACK(VoiceNumber)
SS_INT_STACK(VoiceVolume)
SS_STR_STACK(VoicePitch)
SS_STR_STACK(VoiceRange)
SS_STR_STACK(VoiceSpeed)
SS_STR_STACK(Emphasis)
SS_STR_STACK(Audio)

THIS void       ss_pushLang(SSMLState *s, LanguageId v);
THIS LanguageId ss_popLang(SSMLState *s);
THIS LanguageId ss_peekLang(SSMLState *s);
THIS int8_t     ss_validLang(SSMLState *s);
THIS int8_t     ss_isEmptyLang(SSMLState *s);

/* The five say-as counters, in src/eci_ssmlsayas.c. */
#define SS_SAYAS(name)                                               \
    THIS void   ss_set##name(SSMLState *s);                          \
    THIS void   ss_rel##name(SSMLState *s);                          \
    THIS int8_t ss_is##name(SSMLState *s);

SS_SAYAS(SayAsDate)
SS_SAYAS(SayAsNumber)
SS_SAYAS(SayAsBoolean)
SS_SAYAS(SayAsVXMLdate)
SS_SAYAS(SayAsVXMLcurrency)

#endif
