/* The door between the published interface and the engine.
 *
 * Every one of these is a thin wrapper: it takes the machine, does one or
 * two things to it, and answers whether anything went wrong. They are
 * stdcall because this is where the engine stopped being a C program and
 * started being something a library exports, and the layer above still calls
 * them that way.
 *
 * Almost all of them end the same: whatever happened, ask the error layer
 * whether an error was set and return that. So the error is the return
 * value, set on the way through rather than carried back by hand.
 *
 * A handful of state lives in the block the machine keeps for ECI: whether
 * the engine has been started, whether it has been ended, and whether a
 * flush is in progress. Those are named here by offset because this is the
 * only file that reads them.
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "delta.h"
#include "eci_synththread.h"
#include "evv_abi.h"
#include "eci_eloqc.h"

/* What the block the machine keeps for ECI holds for this layer. */

/* What can go wrong, as this layer numbers it. */
#define ERR_LINK      (-2)
#define ERR_ENGINE    (-3)
#define ERR_START     (-4)
#define ERR_ALREADY   (-5)
#define ERR_BUSY      (-8)
#define ERR_ARGUMENT  3

extern void    resetEngsynError(delta_state *d);
extern void    setEngsynError(delta_state *d, int32_t err);
extern int32_t checkEngsynError(delta_state *d);
extern int32_t getEngsynError(delta_state *d);
extern void    getEngsynErrorRange(delta_state *d, int32_t *from, int32_t *to);
extern int32_t etiwinMainDLL(delta_state *d, int32_t argc, char **argv);

extern int32_t initializeIO(delta_state *d);

/* The five the engine drives a machine through are rules, and every
   language has its own, so they are reached through the machine's rather
   than linked to by name. The call sites read as they did. */
#define DeltaProc_start(d)              (delta_lang_of(d)->proc_start(d))
#define DeltaProc_end(d)                (delta_lang_of(d)->proc_end(d))
#define DeltaProc_flush(d)              (delta_lang_of(d)->proc_flush(d))
#define DeltaProc_process_sentences(d)  \
    (delta_lang_of(d)->proc_process_sentences(d))
#define DeltaProc_process_remaining(d)  \
    (delta_lang_of(d)->proc_process_remaining(d))
extern void    vcmdend(delta_state *d, int32_t how);
extern void    setInterrupt(delta_state *d, int32_t on);
extern void    throwDeltaErrorNow(delta_state *d);
extern void    stopSynthesizing(delta_state *d);
extern void    eciLinkCleanup(delta_state *d);
extern void    deltaCleanup(delta_state *d);
extern int32_t insertDelayedSynthIndex(delta_state *d, int32_t index,
                                       int32_t delay);

extern void *cpp_new(uint32_t n) MANGLED("??2@YAPAXI@Z");
extern void  cpp_delete(void *p) MANGLED("??3@YAXPAX@Z");

/* The user dictionary, which this layer only ever hands on to. */
extern const uint32_t ds_bytes;
extern int32_t ds_failed(const void *s);
extern THIS void *ds_ctor(void *s, delta_state *d)
    MANGLED("??0DictionarySet@@QAE@PAUDelta_This_Struct@@@Z");
extern THIS void ds_dtor(void *s) MANGLED("??1DictionarySet@@QAE@XZ");
extern THIS int32_t ds_load(void *s, int32_t volume, const char *name)
    MANGLED("?load@DictionarySet@@QAEHW4DictVolume@@PBD@Z");
extern THIS int32_t ds_findFirst(void *s, int32_t volume,
                                      const char **a, const char **b)
    MANGLED("?findFirst@DictionarySet@@QAEHW4DictVolume@@AAPBD1@Z");
extern THIS int32_t ds_findNext(void *s, int32_t volume,
                                     const char **a, const char **b)
    MANGLED("?findNext@DictionarySet@@QAEHW4DictVolume@@AAPBD1@Z");
extern THIS const char *ds_lookup(void *s, int32_t volume,
                                       const char *word)
    MANGLED("?lookup@DictionarySet@@QAEPBDW4DictVolume@@PBD@Z");
extern int32_t setCurrentUserDict(delta_state *d, void *s)
    MANGLED("?setCurrentUserDict@@YAHPAUDelta_This_Struct@@PAVDictionarySet@@@Z");
extern void *getCurrentUserDict(delta_state *d)
    MANGLED("?getCurrentUserDict@@YAPAVDictionarySet@@PAUDelta_This_Struct@@@Z");

extern void    setSynthDurationCallback(delta_state *d, void *fn, void *param);
extern void    registerSynthIndexCallback(delta_state *d, void *fn, void *param);
extern void    registerPhonemeCallback(delta_state *d, void *fn, void *param);
extern void    initGlobalVars(delta_state *d);
extern void    resetDelayedSynthQueue(delta_state *d);
extern void    flushDelayedSynthQueue(delta_state *d);
extern int32_t vdltinit(delta_state *d, int32_t how);
extern int32_t vinitrun(delta_state *d);
extern int8_t  vffind_lf(delta_state *d, const char *name);
extern void    vf_clrbuf(delta_state *d, int32_t lf);
extern int32_t synthDevicePlaying(delta_state *d);
extern int32_t holdSynthDevice(delta_state *d, int32_t on);
extern int32_t setSynthToNamedFile(delta_state *d, const char *name);
extern int32_t setSynthToCallback(delta_state *d, void *fn, void *param);
extern int32_t deltaErrorThrown(delta_state *d);
extern THIS int32_t ds_save(void *s, int32_t volume, const char *name)
    MANGLED("?save@DictionarySet@@QAEHW4DictVolume@@PBD@Z");
extern THIS int32_t ds_updateEntry(void *s, int32_t volume,
                                        const char *key, const char *value)
    MANGLED("?updateEntry@DictionarySet@@QAEHW4DictVolume@@PBD1@Z");

/* A dictionary set records what went wrong in one field of its own; nought
   there means it came up cleanly. */

STDCALL int32_t es_engsynRestart(delta_state *d);

/* ---- coming up and going down --------------------------------------- */

/* Starting twice is an error rather than a no-op, which is why the flag is
   set before anything else is tried: a second caller is refused even while
   the first is still working. */
STDCALL int32_t es_engsynStart(delta_state *d)
{
    resetEngsynError(d);

    if (ELOQ_STARTED(d) != 0) {
        setEngsynError(d, ERR_START);
    } else {
        ELOQ_STARTED(d) = 1;
        if (etiwinMainDLL(d, 0, 0) <= 0)
            setEngsynError(d, ERR_START);
        else if (initializeIO(d))
            setEngsynError(d, ERR_START);
        else if (DeltaProc_start(d))
            setEngsynError(d, ERR_ENGINE);
    }

    return checkEngsynError(d);
}

/* Ending runs the command layer down whether the engine ended cleanly or
   not, and only then reports what the engine said. */
STDCALL int32_t es_engsynEnd(delta_state *d)
{
    resetEngsynError(d);

    if (ELOQ_ENDED(d) != 0) {
        setEngsynError(d, ERR_ALREADY);
    } else {
        int32_t rc;

        ELOQ_ENDED(d) = 1;
        rc = DeltaProc_end(d);
        vcmdend(d, 0);
        if (rc != 0)
            setEngsynError(d, ERR_ENGINE);
    }

    return checkEngsynError(d);
}

/* Closing takes no view on errors at all. */
STDCALL int32_t es_engsynClose(delta_state *d)
{
    if (d) {
        stopSynthesizing(d);
        eciLinkCleanup(d);
        deltaCleanup(d);
    }
    return 0;
}

/* ---- what went wrong ------------------------------------------------ */

STDCALL int32_t es_engsynGetLastError(delta_state *d, int32_t *from,
                                      int32_t *to)
{
    getEngsynErrorRange(d, from, to);
    return getEngsynError(d);
}

/* ---- interrupting --------------------------------------------------- */

/* Stopping and resuming are the same door. Stopping throws whatever the
   machine was holding and shuts the synthesiser down; not stopping starts it
   again. */
STDCALL int32_t es_engsynFlush(delta_state *d, int32_t stop)
{
    ELOQ_FLUSHING(d) = stop;
    setInterrupt(d, stop);

    if (stop) {
        /* The error is thrown only where the machine is not in the middle of a
           walk. This is called from whichever thread asked to stop, and the
           machine runs on the synthesiser's; the flag is not a request but an
           answer, and `vback` reads it before doing anything else. Set from
           outside mid-walk, the next backtrack a rule makes answers -1 without
           having restored any of what it saved -- the argument area, the scan
           position, the two pointers -- and the rule then carries on over a
           machine that has been half put back. What comes of that is a call
           whose arguments are taken from below what was pushed, so a location
           arrives as the machine's own state pointer and the accessor at that
           index is nothing: a fault in vinitloc_new, on the synthesiser's
           thread, which is what answering eciDataAbort from the callback did.

           Nothing is lost by leaving it. The interrupt raised just above is the
           cooperative half and the machine answers it at its own checkpoints;
           whoever asked to stop then suspends the synthesiser's queue, which
           does not come back until the walk is out, and only then resets. */
        if (ELOQ_BUSY(d) == 0)
            throwDeltaErrorNow(d);
        stopSynthesizing(d);
    } else {
        es_engsynRestart(d);
    }

    return checkEngsynError(d);
}

/* Throw away what has not been read yet by handing the link an empty string
   and letting the machine flush behind it. */
STDCALL int32_t es_engsynClearInput(delta_state *d)
{
    if (ELOQ_BUSY(d) != 0)
        setEngsynError(d, ERR_BUSY);
    else if (!eciLinkDataFromECI(ELOQ_MAINLINK(d), ""))
        setEngsynError(d, ERR_LINK);
    else if (DeltaProc_flush(d))
        setEngsynError(d, ERR_ENGINE);

    return checkEngsynError(d);
}

/* ---- index marks ---------------------------------------------------- */

/* Both answer true for success, where the layer below answers nought. */
STDCALL int32_t es_engsynInsertSynthesisIndex(delta_state *d, int32_t index)
{
    return insertDelayedSynthIndex(d, index, 0) == 0;
}

STDCALL int32_t es_engsynInsertDelayedSynthesisIndex(delta_state *d,
                                                     int32_t index,
                                                     int32_t delay)
{
    return insertDelayedSynthIndex(d, index, delay) == 0;
}

/* ---- the user dictionary -------------------------------------------- */

/* Built and then checked: one that could not open its volumes is taken
   apart again and nothing comes back. */
STDCALL void *es_engsynNewDict(delta_state *d)
{
    void *room = cpp_new(ds_bytes);
    void *set  = room ? ds_ctor(room, d) : 0;

    if (set && ds_failed(set) != 0) {
        ds_dtor(set);
        cpp_delete(set);
        set = 0;
    }
    return set;
}

STDCALL int32_t es_engsynDeleteDict(void *set)
{
    if (set) {
        ds_dtor(set);
        cpp_delete(set);
    }
    return 0;
}

STDCALL void *es_engsynGetDict(delta_state *d)
{
    return getCurrentUserDict(d);
}

STDCALL int32_t es_engsynSetDict(delta_state *d, void *set)
{
    return setCurrentUserDict(d, set);
}

/* The machine is passed in and ignored: a dictionary set already knows
   which one it belongs to. */
STDCALL int32_t es_engsynLoadDict(delta_state *d, void *set, int32_t volume,
                                  const char *name)
{
    (void)d;

    if (set == 0 || name == 0)
        return ERR_ARGUMENT;
    return ds_load(set, volume, name);
}

STDCALL int32_t es_engsynDictFindFirst(void *set, int32_t volume,
                                       const char **a, const char **b)
{
    if (set == 0)
        return ERR_ARGUMENT;
    return ds_findFirst(set, volume, a, b);
}

STDCALL int32_t es_engsynDictFindNext(void *set, int32_t volume,
                                      const char **a, const char **b)
{
    if (set == 0)
        return ERR_ARGUMENT;
    return ds_findNext(set, volume, a, b);
}

STDCALL const char *es_engsynDictLookup(void *set, int32_t volume,
                                        const char *word)
{
    if (set == 0)
        return 0;
    return ds_lookup(set, volume, word);
}

/* ---- where the answers go ------------------------------------------- */

/* Each of these is one slot for a function and one for whatever the caller
   wants handed back with it, side by side in the block the machine keeps
   for ECI. Three of them are not kept here at all and go straight through
   to the layer that owns them. */

STDCALL void es_engsynWantPhonemeIndices(delta_state *d, int32_t on)
{
    ELOQ_WANT_PHONEMES(d) = on;
}

STDCALL void es_engsynRegisterWordCallback(delta_state *d, void *fn, void *param)
{
    ELOQ_CB(d, 0x14) = fn;
    ELOQ_CB(d, 0x18) = param;
}

STDCALL void es_engsynRegisterWordIndexCallback(delta_state *d, void *fn,
                                                void *param)
{
    ELOQ_CB(d, 0x1c) = fn;
    ELOQ_CB(d, 0x20) = param;
}

STDCALL void es_engsynRegisterUserIndexCallback(delta_state *d, void *fn,
                                                void *param)
{
    ELOQ_CB(d, 0x24) = fn;
    ELOQ_CB(d, 0x28) = param;
}

STDCALL void es_engsynRegisterAnnoCallback(delta_state *d, void *fn, void *param)
{
    ELOQ_CB(d, 0x2c) = fn;
    ELOQ_CB(d, 0x30) = param;
}

STDCALL void es_engsynRegisterEnhancedSPRCallback(delta_state *d, void *fn,
                                                  void *param)
{
    ELOQ_CB(d, 0x34) = fn;
    ELOQ_CB(d, 0x38) = param;
}

STDCALL void es_engsynRegisterVoiceCallback(delta_state *d, void *fn,
                                            void *param)
{
    ELOQ_CB(d, 0x3c) = fn;
    ELOQ_CB(d, 0x40) = param;
}

STDCALL void es_engsynRegisterIndexCallback(delta_state *d, void *fn,
                                            void *param)
{
    registerSynthIndexCallback(d, fn, param);
}

STDCALL void es_engsynRegisterPhonemeCallback(delta_state *d, void *fn,
                                              void *param)
{
    registerPhonemeCallback(d, fn, param);
}

STDCALL void es_engsynSetDurationCallback(delta_state *d, void *fn, void *param)
{
    setSynthDurationCallback(d, fn, param);
}

/* ---- where the sound goes ------------------------------------------- */

/* All three answer true for success where the layer below answers nought. */
STDCALL int32_t es_engsynSetSynthToNamedFile(delta_state *d, const char *name)
{
    return setSynthToNamedFile(d, name) == 0;
}

STDCALL int32_t es_engsynSetSynthToCallback(delta_state *d, void *fn,
                                            void *param)
{
    return setSynthToCallback(d, fn, param) == 0;
}

STDCALL int32_t es_engsynPause(delta_state *d, int32_t on)
{
    return holdSynthDevice(d, on) == 0;
}

/* Still playing if something is being processed, or if the device says so. */
STDCALL int32_t es_engsynOutputPlaying(delta_state *d)
{
    if (ELOQ_BUSY(d) != 0 || synthDevicePlaying(d))
        return 1;
    return 0;
}

/* ---- putting text in and taking answers out ------------------------- */

/* The two are the same but for which of the machine's doors they go
   through. Both refuse to run inside themselves, which is what the busy
   flag is for, and both clear it again on every way out. */
STDCALL int32_t es_engsynProcessSentences(delta_state *d, const char *text)
{
    if (ELOQ_BUSY(d) != 0) {
        setEngsynError(d, ERR_BUSY);
    } else {
        ELOQ_BUSY(d) = 1;
        if (ELOQ_MAINLINK(d) == 0) {
            setEngsynError(d, -1);
        } else {
            if (!eciLinkDataFromECI(ELOQ_MAINLINK(d), text))
                setEngsynError(d, ERR_LINK);
            if (DeltaProc_process_sentences(d) || deltaErrorThrown(d))
                setEngsynError(d, ERR_ENGINE);
        }
        ELOQ_BUSY(d) = 0;
    }
    return checkEngsynError(d);
}

/* The one difference besides the door: a link that would not take the text
   stops this one, and whatever was waiting is let go at the end. */
STDCALL int32_t es_engsynProcessRemaining(delta_state *d, const char *text)
{
    if (ELOQ_BUSY(d) != 0) {
        setEngsynError(d, ERR_BUSY);
    } else {
        ELOQ_BUSY(d) = 1;
        if (ELOQ_MAINLINK(d) == 0) {
            setEngsynError(d, -1);
        } else if (!eciLinkDataFromECI(ELOQ_MAINLINK(d), text)) {
            setEngsynError(d, ERR_LINK);
        } else if (DeltaProc_process_remaining(d) || deltaErrorThrown(d)) {
            setEngsynError(d, ERR_ENGINE);
        }
        ELOQ_BUSY(d) = 0;
    }
    flushDelayedSynthQueue(d);
    return checkEngsynError(d);
}

/* Reading back what the engine produced, each from its own link. */
STDCALL int32_t es_engsynReadPhonemes(delta_state *d, char *buf, int32_t room,
                                      void *n)
{
    if (ELOQ_MAINLINK(d) != 0
        && !eciLinkDataToECI(ELOQ_MAINLINK(d), buf, room, n))
        setEngsynError(d, ERR_LINK);

    return checkEngsynError(d);
}

STDCALL int32_t es_engsynReadConSprs(delta_state *d, char *buf, int32_t room,
                                     void *n)
{
    if (ELOQ_CONSLINK(d) != 0
        && !eciLinkDataToECI(ELOQ_CONSLINK(d), buf, room, n))
        setEngsynError(d, ERR_LINK);

    return checkEngsynError(d);
}

/* The odd one out. It looks at the main link to decide whether to try, but
   reads from the error link, and it answers true only when it could not get
   a message and had to put a fixed one there instead. */
STDCALL int32_t es_engsynReadErrorMessage(delta_state *d, char *buf,
                                          int32_t room, void *n)
{
    if (ELOQ_MAINLINK(d) == 0)
        return 0;

    if (eciLinkDataToECI(ELOQ_ERRLINK(d), buf, room, n))
        return 0;

    strncpy(buf, "Unable to get error message from engine.", (size_t)room);
    buf[room] = 0;
    return 1;
}

/* ---- stopping and starting again ------------------------------------ */

/* Abort is not a request: it sets the error and throws immediately, so
   whatever the machine was in the middle of unwinds. */
STDCALL int32_t es_engsynSetAbort(delta_state *d)
{
    setEngsynError(d, -7);
    throwDeltaErrorNow(d);
    return 0;
}

/* Put the machine back where it started: globals, the delayed queue, the
   variable stack, the run state, and the word buffer if there is one. Then
   stop whatever is playing and start the engine again. */
STDCALL int32_t es_engsynRestart(delta_state *d)
{
    int8_t lf;

    resetEngsynError(d);
    initGlobalVars(d);
    resetDelayedSynthQueue(d);
    flushDelayedSynthQueue(d);

    if (!vdltinit(d, 1) || !vinitrun(d))
        return 1;

    lf = vffind_lf(d, "wordsin");
    if (lf != -1)
        vf_clrbuf(d, lf);

    stopSynthesizing(d);
    if (DeltaProc_start(d))
        setEngsynError(d, ERR_ENGINE);

    ELOQ_BUSY(d) = 0;
    ELOQ_FLUSHING(d) = 0;
    return checkEngsynError(d);
}

/* ---- the rest of the user dictionary -------------------------------- */

STDCALL int32_t es_engsynSaveDict(void *set, int32_t volume, const char *name)
{
    if (set == 0 || name == 0)
        return ERR_ARGUMENT;
    return ds_save(set, volume, name);
}

STDCALL int32_t es_engsynUpdateDict(void *set, int32_t volume,
                                    const char *key, const char *value)
{
    if (set == 0 || key == 0)
        return ERR_ARGUMENT;
    return ds_updateEntry(set, volume, key, value);
}

ALIAS_N("_engsynStart@4", "es_engsynStart", 4);
ALIAS_N("_engsynEnd@4", "es_engsynEnd", 4);
ALIAS_N("_engsynClose@4", "es_engsynClose", 4);
ALIAS_N("_engsynGetLastError@12", "es_engsynGetLastError", 12);
ALIAS_N("_engsynFlush@8", "es_engsynFlush", 8);
ALIAS_N("_engsynClearInput@4", "es_engsynClearInput", 4);
ALIAS_N("_engsynInsertSynthesisIndex@8", "es_engsynInsertSynthesisIndex", 8);
ALIAS_N("_engsynInsertDelayedSynthesisIndex@12",
        "es_engsynInsertDelayedSynthesisIndex", 12);
ALIAS_N("_engsynNewDict@4", "es_engsynNewDict", 4);
ALIAS_N("_engsynDeleteDict@4", "es_engsynDeleteDict", 4);
ALIAS_N("_engsynGetDict@4", "es_engsynGetDict", 4);
ALIAS_N("_engsynSetDict@8", "es_engsynSetDict", 8);
ALIAS_N("_engsynLoadDict@16", "es_engsynLoadDict", 16);
ALIAS_N("_engsynDictFindFirst@16", "es_engsynDictFindFirst", 16);
ALIAS_N("_engsynDictFindNext@16", "es_engsynDictFindNext", 16);
ALIAS_N("_engsynDictLookup@12", "es_engsynDictLookup", 12);
ALIAS_N("_engsynWantPhonemeIndices@8", "es_engsynWantPhonemeIndices", 8);
ALIAS_N("_engsynRegisterWordCallback@12", "es_engsynRegisterWordCallback", 12);
ALIAS_N("_engsynRegisterWordIndexCallback@12", "es_engsynRegisterWordIndexCallback", 12);
ALIAS_N("_engsynRegisterUserIndexCallback@12", "es_engsynRegisterUserIndexCallback", 12);
ALIAS_N("_engsynRegisterAnnoCallback@12", "es_engsynRegisterAnnoCallback", 12);
ALIAS_N("_engsynRegisterEnhancedSPRCallback@12", "es_engsynRegisterEnhancedSPRCallback", 12);
ALIAS_N("_engsynRegisterVoiceCallback@12", "es_engsynRegisterVoiceCallback", 12);
ALIAS_N("_engsynRegisterIndexCallback@12", "es_engsynRegisterIndexCallback", 12);
ALIAS_N("_engsynRegisterPhonemeCallback@12", "es_engsynRegisterPhonemeCallback", 12);
ALIAS_N("_engsynSetDurationCallback@12", "es_engsynSetDurationCallback", 12);
ALIAS_N("_engsynSetSynthToNamedFile@8", "es_engsynSetSynthToNamedFile", 8);
ALIAS_N("_engsynSetSynthToCallback@12", "es_engsynSetSynthToCallback", 12);
ALIAS_N("_engsynPause@8", "es_engsynPause", 8);
ALIAS_N("_engsynOutputPlaying@4", "es_engsynOutputPlaying", 4);
ALIAS_N("_engsynProcessSentences@8", "es_engsynProcessSentences", 8);
ALIAS_N("_engsynProcessRemaining@8", "es_engsynProcessRemaining", 8);
ALIAS_N("_engsynReadPhonemes@16", "es_engsynReadPhonemes", 16);
ALIAS_N("_engsynReadConSprs@16", "es_engsynReadConSprs", 16);
ALIAS_N("_engsynReadErrorMessage@16", "es_engsynReadErrorMessage", 16);
ALIAS_N("_engsynSetAbort@4", "es_engsynSetAbort", 4);
ALIAS_N("_engsynRestart@4", "es_engsynRestart", 4);
ALIAS_N("_engsynSaveDict@12", "es_engsynSaveDict", 12);
ALIAS_N("_engsynUpdateDict@16", "es_engsynUpdateDict", 16);
