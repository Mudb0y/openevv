/* Changing which language the thread speaks.

   This is one function. It is long because a language change is really an
   engine change: a different engine object, a different set of callbacks, a
   different sample format, a different romanizer and a different answer to
   whether the voice is concatenative. Every one of those has to be taken
   down and put back up in order.

   The short road out is when only the dialect has moved. The engine id
   carries the dialect in its second byte, so a change that leaves the rest
   of the word alone keeps the same engine and only has to tell the romanizer
   and the concatenative side which dialect to use.

   A language the engine array does not have falls back to the one already in
   play rather than failing, so a caller asking for something impossible
   keeps a voice. */

#include <stdint.h>
#include "eci_synththread.h"
#include "evv_abi.h"

#define APP_CONCATENATIVE  0xc
#define ECI_PARAM_LANGUAGE 2
#define ROM_LANGUAGE       2
#define CAT_LANGUAGE       2

#define CAT_CB_WORD_START  1
#define CAT_CB_WORD_MARK   2
#define CAT_CB_PHONEME     3
#define CAT_CB_USER_INDEX  4
#define CAT_CB_BREAK       5

#define ENG_COMMAND        0x18
#define ENG_WORD_START     0x4c
#define ENG_USER_INDEX     0x98

/* Which of the two errors the romanizer is complaining about. Everything
   else in the object reports nought here; a language change reports one. */
#define ROM_ERR_LANGUAGE   1

typedef void (*IndexCallback)(int32_t, void *);
typedef void (*UserCallback)(void *);
typedef void (*SynthCallback)(int32_t, int32_t *, void *);

#define ENGCALL __attribute__((stdcall))
typedef ENGCALL int32_t (*EngCommand)(void *engine, const char *line);
typedef ENGCALL void (*EngSetUserIndex)(void *engine, UserCallback cb,
                                        void *param);
typedef ENGCALL void (*EngSetWordStart)(void *engine, IndexCallback cb,
                                        void *param);

#define ENG_CALL(t, off) \
    (((void **)*(void ***)ST_ENGINE(t))[(off) / 4])

/* The three words of sample format the outside world hands in sit a little
   way into whatever it handed in. */
#define OUTFMT_AT 0x0c

static const char CMD_CONCATENATIVE[] = "`esp2";

extern THIS void stw_processRemaining(SynthThread *t)
    MANGLED("?processRemaining@SynthThread@@QAEXXZ");
extern THIS void stb_sendRemainingPhonemesToUser(SynthThread *t)
    MANGLED("?sendRemainingPhonemesToUser@SynthThread@@AAEXXZ");
extern THIS void stb_postEngineError(SynthThread *t)
    MANGLED("?postEngineError@SynthThread@@AAEXXZ");
extern THIS void stb_postRomanizerError(SynthThread *t, int32_t which)
    MANGLED("?postRomanizerError@SynthThread@@AAEXH@Z");
extern THIS int32_t stw_checkLanguage(SynthThread *t, LangIdentifier *l)
    MANGLED("?checkLanguage@SynthThread@@AAEHPAVLangIdentifier@@@Z");
extern THIS int32_t stm_deactivateAllFilters(SynthThread *t)
    MANGLED("?deactivateAllFilters@SynthThread@@QAEJXZ");
extern THIS int32_t stw_isOldEngine(SynthThread *t)
    MANGLED("?isOldEngine@SynthThread@@QAEHXZ");
extern THIS int32_t stw_engineInitialize(SynthThread *t, void *engine)
    MANGLED("?engineInitialize@SynthThread@@AAEHPAVEngineWrapper@@@Z");
extern THIS int32_t stw_createAudioConverter(SynthThread *t, void *format)
    MANGLED("?createAudioConverter@SynthThread@@AAEJAAUECIsampleFormat@@@Z");

extern void stb_staticSynthCallback(int32_t a, int32_t *b, void *param)
    MANGLED("?staticSynthCallback@SynthThread@@CAXHPAJPAX@Z");
extern void stb_staticTorrentPhonemeCallback(int32_t index, void *param)
    MANGLED("?staticTorrentPhonemeCallback@SynthThread@@CAXHPAX@Z");
extern void stb_staticUserIndexCallback(void *param)
    MANGLED("?staticUserIndexCallback@SynthThread@@CAXPAX@Z");
extern void stb_staticWordCallback(int32_t index, void *param)
    MANGLED("?staticWordCallback@SynthThread@@CAXHPAX@Z");
extern void stb_staticSynthesisBreakCallback(int32_t index, void *param)
    MANGLED("?staticSynthesisBreakCallback@SynthThread@@CAXHPAX@Z");

extern THIS void ea_removeEngine(void *a, const LangIdentifier *l)
    MANGLED("?removeEngine@EngineArray@@QAEXQBVLangIdentifier@@@Z");
extern THIS void *ea_getEngine(void *a, const LangIdentifier *l)
    MANGLED("?getEngine@EngineArray@@QAEPAVEngineWrapper@@QBVLangIdentifier@@@Z");
extern THIS uint32_t ea_getCallbackFnFlag(void *a, const LangIdentifier *l)
    MANGLED("?getCallbackFnFlag@EngineArray@@QAEKQBVLangIdentifier@@@Z");
extern THIS uint32_t ea_getCorporaVersion(void *a, const LangIdentifier *l)
    MANGLED("?getCorporaVersion@EngineArray@@QAEKQBVLangIdentifier@@@Z");

extern THIS void lang_setString(LangIdentifier *l)
    MANGLED("?setString@LangIdentifier@@AAEXXZ");

extern THIS void rz_removeUnused(void *r, LangIdentifier *l)
    MANGLED("?removeUnusedRomanizer@RomanizerManager@@QAEXPAVLangIdentifier@@@Z");
extern THIS int32_t rz_setParam(void *r, int32_t which, int32_t value)
    MANGLED("?setParam@RomanizerManager@@QAEHJH@Z");

extern THIS int32_t cm_usingConcatenativeEngine(void *c)
    MANGLED("?usingConcatenativeEngine@ConcatenationManager@@QAEHXZ");
extern THIS int32_t cm_setParam(void *c, int32_t which, int32_t value,
                                 int32_t extra)
    MANGLED("?setParam@ConcatenationManager@@QAEHJHH@Z");
extern THIS void cm_registerCallbackA(void *c, uint32_t which,
                                           IndexCallback cb, void *param)
    MANGLED("?registerCallback@ConcatenationManager@@QAEXKP6AXHPAX@Z0@Z");
extern THIS void cm_registerCallbackB(void *c, uint32_t which,
                                          UserCallback cb, void *param)
    MANGLED("?registerCallback@ConcatenationManager@@QAEXKP6AXPAX@Z0@Z");
extern THIS void cm_registerCallbackC(void *c, SynthCallback cb,
                                           void *param)
    MANGLED("?registerCallback@ConcatenationManager@@QAEXP6AXHPAJPAX@Z1@Z");

extern THIS void fm_autoLoadFilter(void *m, LangIdentifier *l)
    MANGLED("?autoLoadFilter@FilterManager@@QAEXPAVLangIdentifier@@@Z");

extern THIS void es_paramFromEngine(void *s, int32_t which, int32_t value)
    MANGLED("?paramFromEngine@ECIstate@@QAEXJJ@Z");
extern THIS int32_t es_setCurrentState(void *s, void *engine, int32_t concat)
    MANGLED("?setCurrentState@ECIstate@@QAEHPAVEngineWrapper@@H@Z");

extern THIS int32_t aq_postUser(void *a, int32_t what, int32_t value)
    MANGLED("?postUser@ETIappMessageQueue@@QAEHJJ@Z");

/* Say whether the voice is concatenative, but only if that has changed. */
static void tellCaller(SynthThread *t, int32_t now)
{
    if (now) {
        if (!ST_TOLD_CAT(t)) {
            if (APP_LISTENING(ST_APP(t)))
                aq_postUser(ST_APP(t), APP_CONCATENATIVE, 1);
            ST_TOLD_CAT(t) = 1;
        }
    } else if (ST_TOLD_CAT(t) == 1) {
        if (APP_LISTENING(ST_APP(t)))
            aq_postUser(ST_APP(t), APP_CONCATENATIVE, 0);
        ST_TOLD_CAT(t) = 0;
    }
}

/* Both the romanizer and the concatenative side keep their own idea of which
   language is in play, and both are told the whole id, dialect and all. */
static void tellTheOthers(SynthThread *t, int32_t id)
{
    if (rz_setParam(ST_ROMAN(t), ROM_LANGUAGE, id) == -1)
        stb_postRomanizerError(t, ROM_ERR_LANGUAGE);
    cm_setParam(ST_CONCAT(t), CAT_LANGUAGE, id, 0);
}

/* Whichever of the two ways of reporting positions this engine wants, wired
   either into the engine or into the concatenative side. Called once with
   nothing, to take the old engine's callbacks down, and again with the real
   ones once the new engine is up. */
static void wireReporting(SynthThread *t, UserCallback user,
                          IndexCallback word)
{
    if (ST_FLAGS(t) & STF_ROMANIZING) {
        EngSetUserIndex set = (EngSetUserIndex)ENG_CALL(t, ENG_USER_INDEX);

        set(ST_ENGINE(t), user, t);
    } else if (ST_FLAGS(t) & STF_WORD_STARTS) {
        EngSetWordStart set = (EngSetWordStart)ENG_CALL(t, ENG_WORD_START);

        set(ST_ENGINE(t), word, t);
    }
}

/* The whole engine changes under us. */
static void switchEngine(SynthThread *t, LangIdentifier *lang)
{
    LangIdentifier *current = &t->lang;

    ea_removeEngine(ST_ENGINES(t), current);
    if (ST_ROMAN(t))
        rz_removeUnused(ST_ROMAN(t), lang);

    /* Asked for something the array has not got, stay where we are. */
    if (!stw_checkLanguage(t, lang))
        lang = current;

    stm_deactivateAllFilters(t);
    ST_FILTER(t) = 0;

    ST_ENGINE(t) = ea_getEngine(ST_ENGINES(t), lang);
    ST_FLAGS(t) = ea_getCallbackFnFlag(ST_ENGINES(t), lang);
    ST_CORPORA(t) = ea_getCorporaVersion(ST_ENGINES(t), lang);
    /* Asked and the answer thrown away. It is called for whatever it does on
       the way rather than for what it says. */
    stw_isOldEngine(t);

    current->packed = lang->packed;
    lang_setString(current);

    if (!stw_engineInitialize(t, ST_ENGINE(t)))
        stb_postEngineError(t);
    fm_autoLoadFilter(ST_FILTERS(t), lang);
    es_paramFromEngine(ST_STATE(t), ECI_PARAM_LANGUAGE, (int32_t)lang->packed);

    /* Take the old reporting down before the format moves under it. */
    wireReporting(t, 0, 0);
    if (ST_FLAGS(t) & STF_ROMANIZING)
        cm_registerCallbackB(ST_CONCAT(t), CAT_CB_USER_INDEX, 0, t);
    else if (ST_FLAGS(t) & STF_WORD_STARTS)
        cm_registerCallbackA(ST_CONCAT(t), CAT_CB_WORD_MARK, 0, t);

    /* The sample format is ours to work out when the caller registered a
       buffer of its own; otherwise a format handed in from outside is used,
       its three words copied out first. */
    if (ST_SAMPBUF(t)) {
        if (stw_createAudioConverter(t, ST_FORMAT(t)))
            stb_postEngineError(t);
    } else if (ST_OUTFMT(t)) {
        const int32_t *given =
            (const int32_t *)((char *)ST_OUTFMT(t) + OUTFMT_AT);
        SampleFormat fmt;

        fmt.layout = given[0];
        fmt.width = given[2];
        fmt.rate = given[1];
        if (stw_createAudioConverter(t, &fmt))
            stb_postEngineError(t);
    }

    /* And now put the reporting back, on the new engine. */
    wireReporting(t, stb_staticUserIndexCallback, stb_staticWordCallback);

    tellTheOthers(t, (int32_t)lang->packed);

    if (cm_usingConcatenativeEngine(ST_CONCAT(t))) {
        tellCaller(t, 1);

        /* The flag that says we are talking to the engine ourselves goes up
           three times over and comes down once, at the very end. That is the
           original's; it reads as though two of the three were meant to be
           its opposite. */
        ST_DIRECT(t) = 1;
        if (!es_setCurrentState(ST_STATE(t), ST_ENGINE(t), 1))
            stb_postEngineError(t);
        ST_DIRECT(t) = 1;

        cm_registerCallbackC(ST_CONCAT(t), stb_staticSynthCallback, t);
        cm_registerCallbackA(ST_CONCAT(t), CAT_CB_PHONEME,
                                  stb_staticTorrentPhonemeCallback, t);
        if (ST_FLAGS(t) & STF_ROMANIZING)
            cm_registerCallbackB(ST_CONCAT(t), CAT_CB_USER_INDEX,
                                     stb_staticUserIndexCallback, t);
        else if (ST_FLAGS(t) & STF_WORD_STARTS)
            cm_registerCallbackA(ST_CONCAT(t), CAT_CB_WORD_START,
                                      stb_staticWordCallback, t);
        cm_registerCallbackA(ST_CONCAT(t), CAT_CB_BREAK,
                                  stb_staticSynthesisBreakCallback, t);
        if (ST_FLAGS(t) & STF_WORD_MARKS)
            cm_registerCallbackA(ST_CONCAT(t), CAT_CB_WORD_MARK, 0, t);
        /* The phoneme callback again, exactly as it went in the first time. */
        cm_registerCallbackA(ST_CONCAT(t), CAT_CB_PHONEME,
                                  stb_staticTorrentPhonemeCallback, t);

        ST_DIRECT(t) = 1;
        {
            EngCommand command = (EngCommand)ENG_CALL(t, ENG_COMMAND);

            if (command(ST_ENGINE(t), CMD_CONCATENATIVE))
                stb_postEngineError(t);
        }
        ST_DIRECT(t) = 0;
    } else {
        if (!es_setCurrentState(ST_STATE(t), ST_ENGINE(t), 0))
            stb_postEngineError(t);
        tellCaller(t, 0);
    }
}

THIS void changeLanguageRun(SynthThread *t, LangIdentifier *lang, int32_t seq)
{
    void *lock;
    int same_engine = 0;

    (void)seq;

    stw_processRemaining(t);
    if (ST_PHONBUF(t))
        stb_sendRemainingPhonemesToUser(t);

    lock = ST_LOCK(t);
    sy_mutexWait(lock, -1);

    if (lang)
        same_engine = (lang->packed & LANG_ENGINE_MASK)
                      == (ST_ENGINE_ID(t) & LANG_ENGINE_MASK);

    if (!same_engine) {
        switchEngine(t, lang);
    } else {
        /* Only the dialect moved, and possibly not even that. */
        int same_dialect = 0;

        if (lang)
            same_dialect = lang->packed == ST_ENGINE_ID(t);
        if (!same_dialect) {
            LangIdentifier *current = &t->lang;

            current->packed = lang->packed;
            lang_setString(current);
            tellTheOthers(t, (int32_t)lang->packed);
        }
    }

    ST_PENDING(t) -= 1;
    sy_mutexRelease(lock);
}

ALIAS("?changeLanguageRun@SynthThread@@AAEXPAVLangIdentifier@@J@Z",
      "changeLanguageRun");
