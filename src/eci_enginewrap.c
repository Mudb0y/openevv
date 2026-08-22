/* The engine behind an interface.
 *
 * Forty-three methods, of which most are four lines: hand the call on to
 * the entry point of the same name, and keep two pieces of state while
 * doing it.
 *
 * The first is whether the engine has stopped. Once any call answers that
 * it has, every later call is refused without asking again, because there
 * is nothing left to ask. The second is whether the caller has asked to be
 * interrupted; while it has, every answer is turned into nought, so a
 * caller unwinding after an abort is not told about failures it caused.
 *
 * The dictionary half is different in a way worth noticing: most of it
 * does not pass the machine at all. A dictionary knows which engine it
 * belongs to, so the entry points take the dictionary and nothing else.
 */

#include <stdint.h>
#include <stdlib.h>
#include "delta.h"
#include "evv_abi.h"

typedef struct EngineWrapper {
    const void *vt;       /* +0x00 */
    int32_t     refs;     /* +0x04 */
    delta_state *machine; /* +0x08, what delta_new made */
    int32_t     stopped;  /* +0x0c */
    int32_t     aborting; /* +0x10 */
} EngineWrapper;

/* How much room one takes here, for whoever makes one. IBM's is 0x14 bytes
   because the vtable pointer and the machine pointer were four apiece. */
const uint32_t ew_bytes = sizeof(EngineWrapper);

/* The answer that means the engine has stopped and will not start again. */
#define ENGINE_STOPPED 1

/* Which interfaces this will answer to. */
#define IID_UNKNOWN 1
#define IID_ENGINE  2

extern const void *vtbl_unknown[3];
extern void  cpp_delete(void *p) MANGLED("??3@YAXPAX@Z");
extern void *delta_new(void);
extern void  delta_delete(delta_state *d);

typedef STDCALL uint32_t (*AddRefFn)(void *self);

extern STDCALL int32_t es_engsynStart(delta_state *d);
extern STDCALL int32_t es_engsynEnd(delta_state *d);
extern STDCALL int32_t es_engsynClose(delta_state *d);
extern STDCALL int32_t es_engsynRestart(delta_state *d);
extern STDCALL int32_t es_engsynClearInput(delta_state *d);
extern STDCALL int32_t es_engsynSetAbort(delta_state *d);
extern STDCALL int32_t es_engsynOutputPlaying(delta_state *d);
extern STDCALL int32_t es_engsynFlush(delta_state *d, int32_t stop);
extern STDCALL int32_t es_engsynPause(delta_state *d, int32_t on);
extern STDCALL int32_t es_engsynProcessSentences(delta_state *d,
                                                 const char *text);
extern STDCALL int32_t es_engsynProcessRemaining(delta_state *d,
                                                 const char *text);
extern STDCALL int32_t es_engsynGetLastError(delta_state *d, int32_t *from,
                                             int32_t *code);
extern STDCALL int32_t es_engsynReadPhonemes(delta_state *d, char *buf,
                                             int32_t room, int32_t *got);
extern STDCALL int32_t es_engsynReadConSprs(delta_state *d, char *buf,
                                            int32_t room, int32_t *got);
extern STDCALL int32_t es_engsynReadErrorMessage(delta_state *d, char *buf,
                                                 int32_t room, int32_t *got);
extern STDCALL int32_t es_engsynSetSynthToNamedFile(delta_state *d,
                                                    const char *name);
extern STDCALL int32_t es_engsynSetSynthToCallback(delta_state *d, void *fn,
                                                   void *param);
extern STDCALL int32_t es_engsynInsertSynthesisIndex(delta_state *d,
                                                     int32_t index);
extern STDCALL int32_t es_engsynInsertDelayedSynthesisIndex(delta_state *d,
                                                            int32_t index,
                                                            uint32_t after);
extern STDCALL void es_engsynWantPhonemeIndices(delta_state *d, int32_t on);
extern STDCALL void es_engsynSetDurationCallback(delta_state *d, void *fn,
                                                 void *param);
extern STDCALL void es_engsynRegisterWordCallback(delta_state *d, void *fn,
                                                  void *param);
extern STDCALL void es_engsynRegisterWordIndexCallback(delta_state *d,
                                                       void *fn, void *param);
extern STDCALL void es_engsynRegisterUserIndexCallback(delta_state *d,
                                                       void *fn, void *param);
extern STDCALL void es_engsynRegisterIndexCallback(delta_state *d, void *fn,
                                                   void *param);
extern STDCALL void es_engsynRegisterPhonemeCallback(delta_state *d, void *fn,
                                                     void *param);
extern STDCALL void es_engsynRegisterAnnoCallback(delta_state *d, void *fn,
                                                  void *param);
extern STDCALL void es_engsynRegisterVoiceCallback(delta_state *d, void *fn,
                                                   void *param);
extern STDCALL void es_engsynRegisterEnhancedSPRCallback(delta_state *d,
                                                         void *fn,
                                                         void *param);
extern STDCALL void   *es_engsynNewDict(delta_state *d);
extern STDCALL void   *es_engsynGetDict(delta_state *d);
extern STDCALL int32_t es_engsynSetDict(delta_state *d, void *set);
extern STDCALL int32_t es_engsynDeleteDict(void *set);
extern STDCALL int32_t es_engsynLoadDict(delta_state *d, void *set,
                                         int32_t volume, const char *name);
extern STDCALL int32_t es_engsynSaveDict(void *set, int32_t volume,
                                         const char *name);
extern STDCALL int32_t es_engsynUpdateDict(void *set, int32_t volume,
                                           const char *word,
                                           const char *xlat);
extern STDCALL int32_t es_engsynDictFindFirst(void *set, int32_t volume,
                                              const char **word,
                                              const char **xlat);
extern STDCALL int32_t es_engsynDictFindNext(void *set, int32_t volume,
                                             const char **word,
                                             const char **xlat);
extern STDCALL const char *es_engsynDictLookup(void *set, int32_t volume,
                                               const char *word);

extern const void *vtbl_enginewrapper[];

/* Whether to say anything at all. An abort turns every answer into
   nought, which is how a caller unwinding is kept from seeing the
   failures its own abort caused. */
#define ANSWER(e, rc) ((e)->aborting ? 0 : (rc))

/* The guard every one of the middle group opens with, and the note it
   makes on the way out. */
#define GUARDED(e, call)                        \
    do {                                        \
        rc = ENGINE_STOPPED;                    \
        if (!(e)->stopped) {                    \
            rc = (call);                        \
            if (rc == ENGINE_STOPPED)           \
                (e)->stopped = 1;               \
        }                                       \
    } while (0)


/* Which language the wrapper is speaking, for as long as a method runs.
 *
 * The machine knows: it was made for one language and remembers which. The
 * primitives underneath do not all get told -- several are handed nothing
 * but the value they are working on -- so the language is set here, at the
 * boundary between the ECI layer and the machine, and every table read
 * below finds the right one.
 *
 * It goes back on the way out however the method leaves, which is what the
 * cleanup is for: some of these return from the middle. A method that never
 * touches the machine does not have one. */
static void lang_put_back(const delta_language *const *was)
{
    delta_lang_set(*was);
}

#define SPEAKING(e) \
    const delta_language *was_lang_                                     \
        __attribute__((cleanup(lang_put_back)))                         \
        = delta_lang_set(delta_lang_of((e)->machine))

/* ---- the object itself ---------------------------------------------- */

THIS EngineWrapper *ew_ctor(EngineWrapper *e)
{
    e->vt       = &vtbl_unknown;
    e->vt       = &vtbl_enginewrapper;
    e->refs     = 0;
    e->stopped  = 0;
    e->aborting = 0;

    e->machine = delta_new();
    if (e->machine == 0)
        e->stopped = 1;

    return e;
}

THIS void ew_dtor(EngineWrapper *e)
{
    e->vt = &vtbl_enginewrapper;

    if (e->machine != 0) {
        delta_delete(e->machine);
        e->machine = 0;
    }
}

STDCALL int32_t ew_queryInterface(EngineWrapper *e, uint32_t iid, void **out)
{
    *out = 0;
    if (iid == IID_UNKNOWN || iid == IID_ENGINE) {
        *out = e;
        ((AddRefFn *)(*(void ***)e))[1](e);
    }
    return *out != 0;
}

STDCALL uint32_t ew_addRef(EngineWrapper *e)
{
    e->refs++;
    return (uint32_t)e->refs;
}

/* The last reference taken away takes the engine with it. */
STDCALL uint32_t ew_release(EngineWrapper *e)
{
    e->refs--;
    if (e->refs != 0)
        return (uint32_t)e->refs;

    ew_dtor(e);
    cpp_delete(e);
    return 0;
}


/* ---- speaking -------------------------------------------------------- */

STDCALL int32_t ew_start(EngineWrapper *e)
{
    SPEAKING(e);

    int32_t rc;

    GUARDED(e, es_engsynStart(e->machine));
    return ANSWER(e, rc);
}

STDCALL int32_t ew_end(EngineWrapper *e)
{
    SPEAKING(e);

    int32_t rc;

    GUARDED(e, es_engsynEnd(e->machine));
    return ANSWER(e, rc);
}

STDCALL int32_t ew_restart(EngineWrapper *e)
{
    SPEAKING(e);

    int32_t rc;

    GUARDED(e, es_engsynRestart(e->machine));
    return ANSWER(e, rc);
}

STDCALL int32_t ew_processSentences(EngineWrapper *e, const char *text)
{
    SPEAKING(e);

    int32_t rc;

    GUARDED(e, es_engsynProcessSentences(e->machine, text));
    return ANSWER(e, rc);
}

STDCALL int32_t ew_processRemaining(EngineWrapper *e, const char *text)
{
    SPEAKING(e);

    int32_t rc;

    GUARDED(e, es_engsynProcessRemaining(e->machine, text));
    return ANSWER(e, rc);
}

STDCALL int32_t ew_getLastError(EngineWrapper *e, int32_t *from, int32_t *code)
{
    SPEAKING(e);

    int32_t rc;

    GUARDED(e, es_engsynGetLastError(e->machine, from, code));
    return ANSWER(e, rc);
}

STDCALL int32_t ew_readPhonemes(EngineWrapper *e, char *buf, int32_t room,
                                int32_t *got)
{
    SPEAKING(e);

    int32_t rc;

    GUARDED(e, es_engsynReadPhonemes(e->machine, buf, room, got));
    return ANSWER(e, rc);
}

STDCALL int32_t ew_readConSprs(EngineWrapper *e, char *buf, int32_t room,
                               int32_t *got)
{
    SPEAKING(e);

    int32_t rc;

    GUARDED(e, es_engsynReadConSprs(e->machine, buf, room, got));
    return ANSWER(e, rc);
}

STDCALL int32_t ew_readErrorMessage(EngineWrapper *e, char *buf, int32_t room,
                                    int32_t *got)
{
    SPEAKING(e);

    int32_t rc;

    GUARDED(e, es_engsynReadErrorMessage(e->machine, buf, room, got));
    return ANSWER(e, rc);
}

STDCALL int32_t ew_clearInput(EngineWrapper *e)
{
    SPEAKING(e);

    int32_t rc;

    GUARDED(e, es_engsynClearInput(e->machine));
    return ANSWER(e, rc);
}

STDCALL int32_t ew_setAbort(EngineWrapper *e)
{
    SPEAKING(e);

    int32_t rc;

    GUARDED(e, es_engsynSetAbort(e->machine));
    return ANSWER(e, rc);
}

STDCALL int32_t ew_outputPlaying(EngineWrapper *e)
{
    SPEAKING(e);

    int32_t rc;

    GUARDED(e, es_engsynOutputPlaying(e->machine));
    return ANSWER(e, rc);
}

STDCALL int32_t ew_pause(EngineWrapper *e, int32_t on)
{
    SPEAKING(e);

    int32_t rc;

    GUARDED(e, es_engsynPause(e->machine, on));
    return ANSWER(e, rc);
}

/* Throwing the input away is the one thing a stopped engine will still
   do, and it is what un-stops it: whatever went wrong is being discarded
   along with everything else. */
STDCALL int32_t ew_flush(EngineWrapper *e, int32_t stop)
{
    SPEAKING(e);

    e->stopped  = 0;
    e->aborting = stop != 0;
    return es_engsynFlush(e->machine, stop);
}

/* Closing does not ask whether the engine has stopped, because closing a
   stopped engine is exactly what a caller wants to do. It stops it either
   way. */
STDCALL int32_t ew_close(EngineWrapper *e)
{
    SPEAKING(e);

    int32_t rc = 0;

    rc = es_engsynClose(e->machine);
    if (rc != 0) {
        e->stopped = 1;
        rc = ENGINE_STOPPED;
    }
    return ANSWER(e, rc);
}


/* ---- where the sound and the reports go ------------------------------ */

STDCALL int32_t ew_setSynthToNamedFile(EngineWrapper *e, const char *name)
{
    SPEAKING(e);

    int32_t rc;

    GUARDED(e, es_engsynSetSynthToNamedFile(e->machine, name));
    return ANSWER(e, rc);
}

STDCALL int32_t ew_setSynthToCallback(EngineWrapper *e, void *fn, void *param)
{
    SPEAKING(e);

    int32_t rc;

    GUARDED(e, es_engsynSetSynthToCallback(e->machine, fn, param));
    return ANSWER(e, rc);
}

STDCALL int32_t ew_insertSynthesisIndex(EngineWrapper *e, int32_t index)
{
    SPEAKING(e);

    int32_t rc;

    GUARDED(e, es_engsynInsertSynthesisIndex(e->machine, index));
    return ANSWER(e, rc);
}

STDCALL int32_t ew_insertDelayedSynthesisIndex(EngineWrapper *e,
                                               int32_t index, uint32_t after)
{
    SPEAKING(e);

    int32_t rc;

    GUARDED(e, es_engsynInsertDelayedSynthesisIndex(e->machine, index, after));
    return ANSWER(e, rc);
}

/* The ones that register something answer nothing, so they only carry the
   guard: a stopped engine is not told about a new callback. */
STDCALL void ew_wantPhonemeIndices(EngineWrapper *e, int32_t on)
{
    SPEAKING(e);

    if (!e->stopped)
        es_engsynWantPhonemeIndices(e->machine, on);
}

STDCALL void ew_setDurationCallback(EngineWrapper *e, void *fn, void *param)
{
    SPEAKING(e);

    if (!e->stopped)
        es_engsynSetDurationCallback(e->machine, fn, param);
}

STDCALL void ew_registerWordCallback(EngineWrapper *e, void *fn, void *param)
{
    SPEAKING(e);

    if (!e->stopped)
        es_engsynRegisterWordCallback(e->machine, fn, param);
}

STDCALL void ew_registerWordIndexCallback(EngineWrapper *e, void *fn,
                                          void *param)
{
    SPEAKING(e);

    if (!e->stopped)
        es_engsynRegisterWordIndexCallback(e->machine, fn, param);
}

STDCALL void ew_registerUserIndexCallback(EngineWrapper *e, void *fn,
                                          void *param)
{
    SPEAKING(e);

    if (!e->stopped)
        es_engsynRegisterUserIndexCallback(e->machine, fn, param);
}

STDCALL void ew_registerIndexCallback(EngineWrapper *e, void *fn, void *param)
{
    SPEAKING(e);

    if (!e->stopped)
        es_engsynRegisterIndexCallback(e->machine, fn, param);
}

STDCALL void ew_registerPhonemeCallback(EngineWrapper *e, void *fn,
                                        void *param)
{
    SPEAKING(e);

    if (!e->stopped)
        es_engsynRegisterPhonemeCallback(e->machine, fn, param);
}

STDCALL void ew_registerAnnoCallback(EngineWrapper *e, void *fn, void *param)
{
    SPEAKING(e);

    if (!e->stopped)
        es_engsynRegisterAnnoCallback(e->machine, fn, param);
}

STDCALL void ew_registerVoiceCallback(EngineWrapper *e, void *fn, void *param)
{
    SPEAKING(e);

    if (!e->stopped)
        es_engsynRegisterVoiceCallback(e->machine, fn, param);
}

STDCALL void ew_registerEnhancedSPRCallback(EngineWrapper *e, void *fn,
                                            void *param)
{
    SPEAKING(e);

    if (!e->stopped)
        es_engsynRegisterEnhancedSPRCallback(e->machine, fn, param);
}


/* ---- dictionaries ---------------------------------------------------- */

/* Only the two that make or choose one need the machine. A dictionary
   already knows which engine it belongs to, so the rest are handed the
   dictionary alone, and none of them is guarded. */
STDCALL void *ew_newDict(EngineWrapper *e)
{
    SPEAKING(e);

    return es_engsynNewDict(e->machine);
}

STDCALL void *ew_getDict(EngineWrapper *e)
{
    SPEAKING(e);

    return es_engsynGetDict(e->machine);
}

STDCALL int32_t ew_setDict(EngineWrapper *e, void *set)
{
    SPEAKING(e);

    return es_engsynSetDict(e->machine, set);
}

STDCALL int32_t ew_deleteDict(EngineWrapper *e, void *set)
{
    (void)e;
    return es_engsynDeleteDict(set);
}

STDCALL int32_t ew_loadDict(EngineWrapper *e, void *set, int32_t volume,
                            const char *name)
{
    SPEAKING(e);

    return es_engsynLoadDict(e->machine, set, volume, name);
}

STDCALL int32_t ew_saveDict(EngineWrapper *e, void *set, int32_t volume,
                            const char *name)
{
    (void)e;
    return es_engsynSaveDict(set, volume, name);
}

STDCALL int32_t ew_updateDict(EngineWrapper *e, void *set, int32_t volume,
                              const char *word, const char *xlat)
{
    (void)e;
    return es_engsynUpdateDict(set, volume, word, xlat);
}

STDCALL int32_t ew_dictFindFirst(EngineWrapper *e, void *set, int32_t volume,
                                 const char **word, const char **xlat)
{
    (void)e;
    return es_engsynDictFindFirst(set, volume, word, xlat);
}

STDCALL int32_t ew_dictFindNext(EngineWrapper *e, void *set, int32_t volume,
                                const char **word, const char **xlat)
{
    (void)e;
    return es_engsynDictFindNext(set, volume, word, xlat);
}

STDCALL const char *ew_dictLookup(EngineWrapper *e, void *set, int32_t volume,
                                  const char *word)
{
    (void)e;
    return es_engsynDictLookup(set, volume, word);
}


/* The table, in the order the original laid it out. The three at the end
   have no body: the class declares them and never wrote them. */
extern void purecall(void) MANGLED("__purecall");

const void *vtbl_enginewrapper[] = {
    (void *)ew_queryInterface,
    (void *)ew_addRef,
    (void *)ew_release,
    (void *)ew_start,
    (void *)ew_end,
    (void *)ew_processSentences,
    (void *)ew_processRemaining,
    (void *)ew_getLastError,
    (void *)ew_restart,
    (void *)ew_readPhonemes,
    (void *)ew_readErrorMessage,
    (void *)ew_flush,
    (void *)ew_clearInput,
    (void *)ew_setAbort,
    (void *)ew_outputPlaying,
    (void *)ew_pause,
    (void *)ew_setSynthToNamedFile,
    (void *)ew_setSynthToCallback,
    (void *)ew_setDurationCallback,
    (void *)ew_registerWordCallback,
    (void *)ew_registerIndexCallback,
    (void *)ew_registerPhonemeCallback,
    (void *)ew_registerAnnoCallback,
    (void *)ew_insertSynthesisIndex,
    (void *)ew_insertDelayedSynthesisIndex,
    (void *)ew_wantPhonemeIndices,
    (void *)ew_close,
    (void *)ew_newDict,
    (void *)ew_getDict,
    (void *)ew_setDict,
    (void *)ew_deleteDict,
    (void *)ew_loadDict,
    (void *)ew_saveDict,
    (void *)ew_updateDict,
    (void *)ew_dictFindFirst,
    (void *)ew_dictFindNext,
    (void *)ew_dictLookup,
    (void *)ew_registerWordIndexCallback,
    (void *)ew_registerUserIndexCallback,
    (void *)ew_registerEnhancedSPRCallback,
    (void *)ew_registerVoiceCallback,
    (void *)ew_readConSprs,
    (void *)purecall,
    (void *)purecall,
    (void *)purecall
};

ALIAS("??_7EngineWrapper@@6B@", "vtbl_enginewrapper");
ALIAS("??0EngineWrapper@@QAE@XZ", "ew_ctor");
ALIAS("??1EngineWrapper@@QAE@XZ", "ew_dtor");
ALIAS_N("?queryInterface@EngineWrapper@@UAGHKPAPAX@Z", "ew_queryInterface", 12);
ALIAS_N("?addRef@EngineWrapper@@UAGKXZ", "ew_addRef", 4);
ALIAS_N("?release@EngineWrapper@@UAGKXZ", "ew_release", 4);
ALIAS_N("?start@EngineWrapper@@UAGHXZ", "ew_start", 4);
ALIAS_N("?end@EngineWrapper@@UAGHXZ", "ew_end", 4);
ALIAS_N("?processSentences@EngineWrapper@@UAGHPBD@Z", "ew_processSentences", 8);
ALIAS_N("?processRemaining@EngineWrapper@@UAGHPBD@Z", "ew_processRemaining", 8);
ALIAS_N("?getLastError@EngineWrapper@@UAGHPAH0@Z", "ew_getLastError", 12);
ALIAS_N("?restart@EngineWrapper@@UAGHXZ", "ew_restart", 4);
ALIAS_N("?readPhonemes@EngineWrapper@@UAGHPADHPAH@Z", "ew_readPhonemes", 16);
ALIAS_N("?readErrorMessage@EngineWrapper@@UAGHPADHPAH@Z",
        "ew_readErrorMessage", 16);
ALIAS_N("?flush@EngineWrapper@@UAGHH@Z", "ew_flush", 8);
ALIAS_N("?clearInput@EngineWrapper@@UAGHXZ", "ew_clearInput", 4);
ALIAS_N("?setAbort@EngineWrapper@@UAGHXZ", "ew_setAbort", 4);
ALIAS_N("?outputPlaying@EngineWrapper@@UAGHXZ", "ew_outputPlaying", 4);
ALIAS_N("?pause@EngineWrapper@@UAGHH@Z", "ew_pause", 8);
ALIAS_N("?setSynthToNamedFile@EngineWrapper@@UAGHPBD@Z",
        "ew_setSynthToNamedFile", 8);
ALIAS_N("?setSynthToCallback@EngineWrapper@@UAGHP6AXHPAJPAX@Z1@Z",
        "ew_setSynthToCallback", 12);
ALIAS_N("?setDurationCallback@EngineWrapper@@UAGXP6AXJJPAX@Z0@Z",
        "ew_setDurationCallback", 12);
ALIAS_N("?registerWordCallback@EngineWrapper@@UAGXP6AXHPAX@Z0@Z",
        "ew_registerWordCallback", 12);
ALIAS_N("?registerIndexCallback@EngineWrapper@@UAGXP6AXHPAX@Z0@Z",
        "ew_registerIndexCallback", 12);
ALIAS_N("?registerPhonemeCallback@EngineWrapper@@UAGXP6AXHKPAX@Z0@Z",
        "ew_registerPhonemeCallback", 12);
ALIAS_N("?registerAnnoCallback@EngineWrapper@@UAGXP6AXJJPAX@Z0@Z",
        "ew_registerAnnoCallback", 12);
ALIAS_N("?insertSynthesisIndex@EngineWrapper@@UAGHH@Z",
        "ew_insertSynthesisIndex", 8);
ALIAS_N("?insertDelayedSynthesisIndex@EngineWrapper@@UAGHHK@Z",
        "ew_insertDelayedSynthesisIndex", 12);
ALIAS_N("?wantPhonemeIndices@EngineWrapper@@UAGXH@Z",
        "ew_wantPhonemeIndices", 8);
ALIAS_N("?close@EngineWrapper@@UAGHXZ", "ew_close", 4);
ALIAS_N("?newDict@EngineWrapper@@UAGPAXXZ", "ew_newDict", 4);
ALIAS_N("?getDict@EngineWrapper@@UAGPAXXZ", "ew_getDict", 4);
ALIAS_N("?setDict@EngineWrapper@@UAGHPAX@Z", "ew_setDict", 8);
ALIAS_N("?deleteDict@EngineWrapper@@UAGPAXPAX@Z", "ew_deleteDict", 8);
ALIAS_N("?loadDict@EngineWrapper@@UAGHPAXHPBD@Z", "ew_loadDict", 16);
ALIAS_N("?saveDict@EngineWrapper@@UAGHPAXHPBD@Z", "ew_saveDict", 16);
ALIAS_N("?updateDict@EngineWrapper@@UAGHPAXHPBD1@Z", "ew_updateDict", 20);
ALIAS_N("?dictFindFirst@EngineWrapper@@UAGHPAXHPAPBD1@Z",
        "ew_dictFindFirst", 20);
ALIAS_N("?dictFindNext@EngineWrapper@@UAGHPAXHPAPBD1@Z",
        "ew_dictFindNext", 20);
ALIAS_N("?dictLookup@EngineWrapper@@UAGPBDPAXHPBD@Z", "ew_dictLookup", 16);
ALIAS_N("?registerWordIndexCallback@EngineWrapper@@UAGXP6AXHPAX@Z0@Z",
        "ew_registerWordIndexCallback", 12);
ALIAS_N("?registerUserIndexCallback@EngineWrapper@@UAGXP6AXPAX@Z0@Z",
        "ew_registerUserIndexCallback", 12);
ALIAS_N("?registerEnhancedSPRCallback@EngineWrapper@@UAGXP6AXPAX@Z0@Z",
        "ew_registerEnhancedSPRCallback", 12);
ALIAS_N("?registerVoiceCallback@EngineWrapper@@UAGXP6AXJPAF0000PAX@Z1@Z",
        "ew_registerVoiceCallback", 12);
ALIAS_N("?readConSprs@EngineWrapper@@UAGHPADHPAH@Z", "ew_readConSprs", 16);
