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
#include "delta.h"
#include "eci_synththread.h"

#define STDCALL __attribute__((stdcall))

/* A stdcall name carries the size of its arguments, so these need the alias
   that puts it back. */
#define ALIAS_N(mangled, ours, n) \
    __asm__(".globl \"" mangled "\"\n.set \"" mangled "\", _" ours "@" #n "\n")

/* What the block the machine keeps for ECI holds for this layer. */
#define ELOQ(d)          ((unsigned char *)(d)->eloqc)
#define ELOQ_STARTED(d)  (*(int32_t *)(ELOQ(d) + 0x04))
#define ELOQ_ENDED(d)    (*(int32_t *)(ELOQ(d) + 0x08))
#define ELOQ_FLUSHING(d) (*(int32_t *)(ELOQ(d) + 0x0c))
#define ELOQ_BUSY(d)     (*(int32_t *)(ELOQ(d) + 0x10))
#define ELOQ_MAINLINK(d) (*(void **)(ELOQ(d) + 0xa0))

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
extern int32_t DeltaProc_start(delta_state *d);
extern int32_t DeltaProc_end(delta_state *d);
extern int32_t DeltaProc_flush(delta_state *d);
extern void    vcmdend(delta_state *d, int32_t how);
extern void    setInterrupt(delta_state *d, int32_t on);
extern void    throwDeltaErrorNow(delta_state *d);
extern void    stopSynthesizing(delta_state *d);
extern void    eciLinkCleanup(delta_state *d);
extern void    deltaCleanup(delta_state *d);
extern int32_t eciLinkDataFromECI(void *link, const char *text);
extern int32_t insertDelayedSynthIndex(delta_state *d, int32_t index,
                                       int32_t delay);

extern void *cpp_new(uint32_t n) MANGLED("??2@YAPAXI@Z");
extern void  cpp_delete(void *p) MANGLED("??3@YAXPAX@Z");

/* The user dictionary, which this layer only ever hands on to. */
extern THIS void *dictset_ctor(void *s, delta_state *d)
    MANGLED("??0DictionarySet@@QAE@PAUDelta_This_Struct@@@Z");
extern THIS void dictset_dtor(void *s) MANGLED("??1DictionarySet@@QAE@XZ");
extern THIS int32_t dictset_load(void *s, int32_t volume, const char *name)
    MANGLED("?load@DictionarySet@@QAEHW4DictVolume@@PBD@Z");
extern THIS int32_t dictset_findFirst(void *s, int32_t volume,
                                      const char **a, const char **b)
    MANGLED("?findFirst@DictionarySet@@QAEHW4DictVolume@@AAPBD1@Z");
extern THIS int32_t dictset_findNext(void *s, int32_t volume,
                                     const char **a, const char **b)
    MANGLED("?findNext@DictionarySet@@QAEHW4DictVolume@@AAPBD1@Z");
extern THIS const char *dictset_lookup(void *s, int32_t volume,
                                       const char *word)
    MANGLED("?lookup@DictionarySet@@QAEPBDW4DictVolume@@PBD@Z");
extern int32_t setCurrentUserDict(delta_state *d, void *s)
    MANGLED("?setCurrentUserDict@@YAHPAUDelta_This_Struct@@PAVDictionarySet@@@Z");
extern void *getCurrentUserDict(delta_state *d)
    MANGLED("?getCurrentUserDict@@YAPAVDictionarySet@@PAUDelta_This_Struct@@@Z");

/* A dictionary set records what went wrong in one field of its own; nought
   there means it came up cleanly. */
#define DICTSET_FAILED(s)  (*(int32_t *)((char *)(s) + 0x14))

/* Still the original's; it comes later in this batch. */
extern STDCALL int32_t engsynRestart(delta_state *d);

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
        throwDeltaErrorNow(d);
        stopSynthesizing(d);
    } else {
        engsynRestart(d);
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
    void *room = cpp_new(0x18);
    void *set  = room ? dictset_ctor(room, d) : 0;

    if (set && DICTSET_FAILED(set) != 0) {
        dictset_dtor(set);
        cpp_delete(set);
        set = 0;
    }
    return set;
}

STDCALL int32_t es_engsynDeleteDict(void *set)
{
    if (set) {
        dictset_dtor(set);
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
    return dictset_load(set, volume, name);
}

STDCALL int32_t es_engsynDictFindFirst(void *set, int32_t volume,
                                       const char **a, const char **b)
{
    if (set == 0)
        return ERR_ARGUMENT;
    return dictset_findFirst(set, volume, a, b);
}

STDCALL int32_t es_engsynDictFindNext(void *set, int32_t volume,
                                      const char **a, const char **b)
{
    if (set == 0)
        return ERR_ARGUMENT;
    return dictset_findNext(set, volume, a, b);
}

STDCALL const char *es_engsynDictLookup(void *set, int32_t volume,
                                        const char *word)
{
    if (set == 0)
        return 0;
    return dictset_lookup(set, volume, word);
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
