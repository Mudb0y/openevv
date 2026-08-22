/* Building a synthesis thread, settling it on a language, stopping it and
   taking it apart again.

   The two constructors differ only at the ends. Both build the same object
   and both share the two things every synthesis thread in the process holds
   between them -- the sound manager and the table of phoneme names -- which
   are made by whichever thread is built first and given back by whichever is
   destroyed last, counted by a number held beside them.

   The one that is given a language also starts the thread running and copies
   the language in; the one that is not leaves both for later.

   Nothing in here reports a failure by returning: a constructor cannot. What
   it does instead is leave a complaint in a field the layer above reads back
   once it has the object. */

#include <stdint.h>
#include <string.h>
#include "eci_synththread.h"
#include "evv_abi.h"

#define OK              0
#define ERR_FAILED     (-2)
#define ERR_NO_LANG    (-14)
#define ERR_NO_SOUND   (-16)
#define ERR_ENGINE     (-15)
#define ERR_ROM_LANG   (-21)
#define ERR_CAT_LANG   (-19)

#define ECI_PARAM_LANGUAGE 2
#define ECI_PARAM_PHONEMES 4
#define ROM_LANGUAGE       2
#define ROM_CONCATENATIVE  0x3e8
#define CAT_LANGUAGE       2

#define CAT_CB_WORD_START 1
#define CAT_CB_WORD_MARK  2
#define CAT_CB_PHONEME    3
#define CAT_CB_USER_INDEX 4
#define CAT_CB_BREAK      5

#define ENG_COMMAND        0x18
#define ENG_RESET          0x2c
#define ENG_WORD_START_CB  0x4c
#define ENG_ANNO_CB        0x58
#define ENG_SET_PHONEMES   0x64
#define ENG_USER_INDEX_CB  0x98
#define ENG_SPR_CB         0x9c
#define ENG_VOICE_CB       0xa0

#define ENGCALL __attribute__((stdcall))
/* The engine's table of virtual functions, by the byte a slot sat at in
   the original. A slot is a pointer, not four bytes, so the number is
   divided rather than added. */
#define ENG_CALL_ON(e, off) (((void **)*(void ***)(e))[(off) / 4])

typedef void (*IndexCallback)(int32_t, void *);
typedef void (*UserCallback)(void *);
typedef void (*SynthCallback)(int32_t, int32_t *, void *);
typedef void (*AnnoCallback)(int32_t, int32_t, void *);
typedef void (*VoiceCallback)(int32_t, int16_t *, int16_t *, int16_t *,
                              int16_t *, int16_t *, void *);

typedef ENGCALL int32_t (*EngCommand)(void *engine, const char *line);
typedef ENGCALL int32_t (*EngReset)(void *engine, int32_t how);
typedef ENGCALL void (*EngSetPhonemes)(void *engine, int32_t on);
typedef ENGCALL void (*EngSetIndex)(void *engine, IndexCallback cb, void *p);
typedef ENGCALL void (*EngSetUser)(void *engine, UserCallback cb, void *p);
typedef ENGCALL void (*EngSetAnno)(void *engine, AnnoCallback cb, void *p);
typedef ENGCALL void (*EngSetVoice)(void *engine, VoiceCallback cb, void *p);

/* The line every engine is put through when it is first settled on a
   language, and the one an engine with newer corpora gets as well. */
static const char CMD_DEFAULTS[] = "`v1 `ts0 `da1 `ty1 `pp1";
static const char CMD_NORMALISE[] = "`nor";
static const char CMD_CONCATENATIVE[] = "`esp2";

/* Sub-objects of SynthThread that are built and taken apart here. */
#define ST_ENGINELIST(t) (&(t)->engines.base)
#define ST_INIFILE(t)    (&(t)->engines.ini)
#define ST_LANG(t)       (&(t)->lang)
#define ST_IDXMEM(t)     (&(t)->indexmgr)
#define ST_IDXLOCK(t)    ((void *)(t)->indexmgr.lock)
/* Three blocks the thread hands out and takes back, and the complaint the
   layer above reads once it has the object. */

/* How big each of the things the constructor makes is. */
extern const uint32_t rm_bytes;
#define SIZE_CONCAT      0x2c0
extern const uint32_t eq_bytes;
#define MARKQUEUE_ROOM   0x200
#define SIZE_FILTERS     0x144
extern const uint32_t sm_bytes;
extern const uint32_t ph_bytes;
#define INDEX_BLOCK      8

extern int32_t RAL_THREAD_PRIORITY_NORMAL MANGLED("_RAL_THREAD_PRIORITY_NORMAL");

/* What every synthesis thread in the process shares, and how many of them
   are still holding it. */
extern void *st_soundManager
    MANGLED("?m_soundManager@SynthThread@@0PAVSoundManager@@A");
extern void *st_phonemes MANGLED("?m_phonemes@SynthThread@@0PAVPhonemes@@A");
extern int32_t st_nRefPointers MANGLED("?nRefPointers@SynthThread@@0HA");
extern uint8_t st_protectInitialization[]
    MANGLED("?m_protectInitialization@SynthThread@@0VMutex@@A");

extern THIS void *qt_ctor(void *t) MANGLED("??0ETImessageQueueThread@@QAE@XZ");
extern THIS void qt_dtor(void *t) MANGLED("??1ETImessageQueueThread@@UAE@XZ");
extern THIS int16_t stm_qtSuspend(void *t)
    MANGLED("?suspend@ETImessageQueueThread@@QAEFXZ");
extern THIS int16_t stm_qtResume(void *t)
    MANGLED("?resume@ETImessageQueueThread@@QAEFXZ");
extern THIS int32_t th_start(void *t, int32_t priority)
    MANGLED("?start@ETIThread@@QAEHH@Z");
extern THIS int32_t th_terminateAndWait(void *t)
    MANGLED("?terminateAndWait@ETIThread@@QAEHXZ");

extern THIS void *sy_mutexCtor(void *m, int32_t kind)
    MANGLED("??0Mutex@@QAE@H@Z");
extern THIS void *sy_eventCtor(void *e, int32_t kind)
    MANGLED("??0ETIEvent@@QAE@H@Z");
extern THIS void sy_eventDtor(void *e) MANGLED("??1ETIEvent@@QAE@XZ");
extern THIS int32_t sy_eventSignal(void *e) MANGLED("?signal@ETIEvent@@QAEHXZ");
extern THIS int32_t sy_eventUnsignal(void *e)
    MANGLED("?unsignal@ETIEvent@@QAEHXZ");

extern THIS void *eng_ctor(void *a) MANGLED("??0EngineList@@QAE@XZ");
extern THIS void eng_dtor(void *a) MANGLED("??1EngineList@@QAE@XZ");
extern THIS void *ini_ctor(void *r) MANGLED("??0IniFileReader@@QAE@XZ");
extern THIS void ini_dtor(void *r) MANGLED("??1IniFileReader@@QAE@XZ");
extern THIS void *sti_indexQueueCtor(void *q) MANGLED("??0IndexQueue@@QAE@XZ");
extern THIS void *mm_ctor(void *m, uint32_t block)
    MANGLED("??0MemoryManager@@QAE@K@Z");
extern THIS void mm_dtor(void *m) MANGLED("??1MemoryManager@@QAE@XZ");
extern THIS void *sti_langCtor(LangIdentifier *l)
    MANGLED("??0LangIdentifier@@QAE@XZ");
extern THIS void lang_setString(LangIdentifier *l)
    MANGLED("?setString@LangIdentifier@@AAEXXZ");

extern THIS void *rz_ctor(void *r, SynthThread *t)
    MANGLED("??0RomanizerManager@@QAE@PAVSynthThread@@@Z");
extern THIS void rz_dtor(void *r) MANGLED("??1RomanizerManager@@QAE@XZ");
extern THIS void *cm_ctor(void *c, SynthThread *t)
    MANGLED("??0ConcatenationManager@@QAE@PAVSynthThread@@@Z");
extern THIS void cm_dtor(void *c) MANGLED("??1ConcatenationManager@@QAE@XZ");
extern THIS void *fm_ctor(void *m, SynthThread *t)
    MANGLED("??0FilterManager@@QAE@PAVSynthThread@@@Z");
extern THIS void fm_dtor(void *m) MANGLED("??1FilterManager@@QAE@XZ");
extern THIS void *eq_ctor(void *q, uint32_t room)
    MANGLED("??0ETIqueue@@QAE@K@Z");
extern THIS void *sm_ctor(void *m) MANGLED("??0SoundManager@@QAE@XZ");
extern THIS void sm_dtor(void *m) MANGLED("??1SoundManager@@QAE@XZ");
extern THIS void *ph_ctor(void *p) MANGLED("??0Phonemes@@QAE@XZ");
extern THIS void pcm_cvt_dtor(void *c) MANGLED("??1AudioConverter@@QAE@XZ");
extern THIS void sy_semDtor(void *s) MANGLED("??1Semaphore@@QAE@XZ");

extern THIS void sm_removeAudioFormat(void *m, void *f)
    MANGLED("?removeAudioFormat@SoundManager@@QAEXPAVAudioFormat@@@Z");
extern THIS void sti_deleteAll(void *m)
    MANGLED("?deleteAll@IndexManager@@QAEXXZ");
extern THIS void el_listReset(void *l) MANGLED("?reset@EList@@QAEXXZ");
extern THIS void eq_reset(void *q) MANGLED("?reset@ETIqueue@@QAEXXZ");

extern THIS void ea_removeEngine(void *a, const LangIdentifier *l)
    MANGLED("?removeEngine@EngineArray@@QAEXQBVLangIdentifier@@@Z");
extern THIS void *ea_getEngine(void *a, const LangIdentifier *l)
    MANGLED("?getEngine@EngineArray@@QAEPAVEngineWrapper@@QBVLangIdentifier@@@Z");
extern THIS uint32_t ea_getCallbackFnFlag(void *a, const LangIdentifier *l)
    MANGLED("?getCallbackFnFlag@EngineArray@@QAEKQBVLangIdentifier@@@Z");
extern THIS uint32_t ea_getCorporaVersion(void *a, const LangIdentifier *l)
    MANGLED("?getCorporaVersion@EngineArray@@QAEKQBVLangIdentifier@@@Z");

extern THIS void rz_removeUnused(void *r, LangIdentifier *l)
    MANGLED("?removeUnusedRomanizer@RomanizerManager@@QAEXPAVLangIdentifier@@@Z");
extern THIS int32_t rz_setParam(void *r, int32_t which, int32_t value)
    MANGLED("?setParam@RomanizerManager@@QAEHJH@Z");
extern THIS int32_t rz_stop(void *r) MANGLED("?stop@RomanizerManager@@QAEHXZ");
extern THIS void rz_clear(void *r) MANGLED("?clear@RomanizerManager@@QAEXXZ");
extern THIS int32_t rz_resume(void *r)
    MANGLED("?resume@RomanizerManager@@QAEHXZ");

extern THIS int32_t cm_usingConcatenativeEngine(void *c)
    MANGLED("?usingConcatenativeEngine@ConcatenationManager@@QAEHXZ");
extern THIS int32_t cm_setParam(void *c, int32_t which, int32_t value,
                                 int32_t extra)
    MANGLED("?setParam@ConcatenationManager@@QAEHJHH@Z");
extern THIS int32_t cm_engineSupports(void *c, uint32_t a, uint32_t b)
    MANGLED("?engineSupportsConcatenative@ConcatenationManager@@QAEHKK@Z");
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
extern THIS void stm_pauseMessageQueue(void *a, int32_t how)
    MANGLED("?pauseMessageQueue@ETIappMessageQueue@@QAEXH@Z");

extern THIS int16_t snd_getStatusDirect(void *s)
    MANGLED("?getStatusDirect@SoundThread@@QAEFXZ");
extern THIS int32_t snd_resetDirect(void *s)
    MANGLED("?resetDirect@SoundThread@@QAEHXZ");
extern THIS int32_t snd_closeDirect(void *s)
    MANGLED("?closeDirect@SoundThread@@QAEHXZ");

extern THIS int32_t stw_isOldEngine(SynthThread *t)
    MANGLED("?isOldEngine@SynthThread@@QAEHXZ");
extern THIS int32_t stw_engineInitialize(SynthThread *t, void *engine)
    MANGLED("?engineInitialize@SynthThread@@AAEHPAVEngineWrapper@@@Z");
extern THIS int32_t stl_stop(SynthThread *t);

extern void stb_staticSynthCallback(int32_t, int32_t *, void *)
    MANGLED("?staticSynthCallback@SynthThread@@CAXHPAJPAX@Z");
extern void stb_staticTorrentPhonemeCallback(int32_t, void *)
    MANGLED("?staticTorrentPhonemeCallback@SynthThread@@CAXHPAX@Z");
extern void stb_staticWordCallback(int32_t, void *)
    MANGLED("?staticWordCallback@SynthThread@@CAXHPAX@Z");
extern void stb_staticWordIndexCallback(int32_t, void *)
    MANGLED("?staticWordIndexCallback@SynthThread@@CAXHPAX@Z");
extern void stb_staticSynthesisBreakCallback(int32_t, void *)
    MANGLED("?staticSynthesisBreakCallback@SynthThread@@CAXHPAX@Z");
extern void stb_staticAnnoCallback(int32_t, int32_t, void *)
    MANGLED("?staticAnnoCallback@SynthThread@@CAXJJPAX@Z");
extern void stb_staticEnhancedSPRCallback(void *)
    MANGLED("?staticEnhancedSPRCallback@SynthThread@@CAXPAX@Z");
extern void stb_staticVoiceChangeCallback(int32_t, int16_t *, int16_t *,
                                          int16_t *, int16_t *, int16_t *,
                                          void *)
    MANGLED("?staticVoiceChangeCallback@SynthThread@@CAXJPAF0000PAX@Z");

/* The table. Only the destructor is this class's own; the rest of it is the
   message-queue thread's, slot for slot as its own .rdata has them. */
typedef struct { void *slot[6]; } ThreadVtbl;
extern const ThreadVtbl vt_synthThread;
extern THIS void qt_terminate(void *t)
    MANGLED("?terminate@ETImessageQueueThread@@MAEXXZ");
extern THIS int32_t qt_waitForExit(void *t)
    MANGLED("?waitForExit@ETImessageQueueThread@@MAEHXZ");
extern THIS uint32_t qt_run(void *t)
    MANGLED("?run@ETImessageQueueThread@@MAEKXZ");
extern THIS void qt_setToTerminate(void *t)
    MANGLED("?setToTerminate@ETImessageQueueThread@@MAEXXZ");
extern THIS void qt_translateMessage(void *t, void **m)
    MANGLED("?translateMessage@ETImessageQueueThread@@MAEXPAPAVETImessage@@@Z");

/* The two tables the index queue wears in turn while it is emptied. */
extern const void *vtbl_elistqueue MANGLED("??_7EListQueue@@6B@");
extern const void *vtbl_eslist MANGLED("??_7ESList@@6B@");

/* The application queue's own table, of which only two slots are reached
   from here. */
typedef struct AppQueue AppQueue;
typedef struct {
    THIS int16_t (*sendMessage)(AppQueue *, void *, int32_t, void *, void *);
    THIS int16_t (*postMessage)(AppQueue *, void *, int32_t, void *, void *);
    THIS int16_t (*popMessage)(AppQueue *, void **, int32_t, void *);
    THIS void    (*suspend)(AppQueue *);
    THIS void    (*resume)(AppQueue *);
} AppQueueVtbl;
struct AppQueue { const AppQueueVtbl *vt; };

/* And the mark queue, whose deleting destructor is slot nought. */
typedef struct { void *(*destroy)(void *self, int32_t free_it); } QueueVtbl;

/* ---- building ---- */

/* The body both constructors share: every field of the object put into a
   known state, and the five things it owns made. A block that could not be
   made leaves a complaint behind rather than failing outright, because a
   constructor has nowhere to fail to. */
static void stl_build(SynthThread *t, void *app, void *state)
{
    void *p;

    *(const ThreadVtbl **)t = &vt_synthThread;
    ST_CONVERTER(t) = 0;
    eng_ctor(ST_ENGINELIST(t));
    ini_ctor(ST_INIFILE(t));
    ST_ENGINE(t) = 0;

    sy_mutexCtor(ST_LOCK(t), 0);
    ST_POSTED(t) = 0;
    ST_SAMPLES(t) = 0;
    ST_PENDING(t) = 0;
    ST_LASTMARK(t) = 0;
    sti_indexQueueCtor(ST_INDEXQ(t));
    ST_OUTFMT(t) = 0;
    ST_SOUND(t) = 0;
    mm_ctor(ST_IDXMEM(t), INDEX_BLOCK);
    sy_mutexCtor(ST_IDXLOCK(t), 0);
    sy_eventCtor(ST_SYNCED(t), 0);

    ST_APP(t) = app;
    ST_STATE(t) = state;
    ST_SAMPBUF(t) = 0;
    ST_SAMPROOM(t) = 0;
    ST_SAMPHELD(t) = 0;
    ST_PHONBUF(t) = 0;
    ST_ENGPHON(t) = 0;
    ST_ENGPHONROOM(t) = 0;
    ST_PHONHELD(t) = 0;
    ST_ENGERR(t) = 0;
    ST_ROMERR(t) = 0;
    ST_SILENT(t) = 0;
    ST_LASTLOOKUP(t) = 0;
    ST_LASTKEY(t) = 0;
    ST_LASTVALUE(t) = 0;
    ST_BLOCKER(t) = 0;
    ST_FLAGS(t) = 0;
    ST_STATUS(t) = 0;
    ST_CORPORA(t) = 0;
    ST_STOPPED(t) = 0;
    ST_DIRECT(t) = 0;
    /* Nobody has asked the engine how old it is yet. */
    ST_OLD_ENGINE(t) = -1;
    ST_FILTER(t) = 0;
    ST_FRESH(t) = 0;
    ST_TOLD_CAT(t) = 0;

    p = cpp_new(rm_bytes);
    ST_ROMAN(t) = p ? rz_ctor(p, t) : 0;
    if (!ST_ROMAN(t))
        ST_STATUS(t) = ERR_FAILED;

    p = cpp_new(SIZE_CONCAT);
    ST_CONCAT(t) = p ? cm_ctor(p, t) : 0;

    p = cpp_new(eq_bytes);
    ST_MARKS(t) = p ? eq_ctor(p, MARKQUEUE_ROOM) : 0;
    if (!ST_MARKS(t))
        ST_STATUS(t) = ERR_FAILED;

    p = cpp_new(SIZE_FILTERS);
    ST_FILTERS(t) = p ? fm_ctor(p, t) : 0;
    if (!ST_FILTERS(t))
        ST_STATUS(t) = ERR_FAILED;
}

/* The two things every thread in the process shares. Whoever gets here first
   makes them; everyone after finds them already there. */
static void stl_takeShared(SynthThread *t)
{
    void *p;

    sy_mutexWait(st_protectInitialization, -1);

    if (!st_soundManager) {
        p = cpp_new(sm_bytes);
        st_soundManager = p ? sm_ctor(p) : 0;
        if (!st_soundManager)
            ST_STATUS(t) = ERR_FAILED;
    }
    if (!st_phonemes) {
        p = cpp_new(ph_bytes);
        st_phonemes = p ? ph_ctor(p) : 0;
        if (!st_phonemes)
            ST_STATUS(t) = ERR_FAILED;
    }
    st_nRefPointers += 1;

    sy_mutexRelease(st_protectInitialization);
}

/* Built without a language: the thread is not started and no language is
   settled on. Whoever wants either has to ask for it afterwards. */
THIS SynthThread *stl_ctor(SynthThread *t, void *app, void *state)
{
    qt_ctor(t);
    stl_build(t, app, state);
    /* The language record is zeroed and named by hand here rather than
       constructed, which comes to the same thing. */
    ST_LANG(t)->packed = 0;
    ST_LANG(t)->packed = 0;
    lang_setString(ST_LANG(t));
    stl_takeShared(t);
    return t;
}

/* And built with one: the same, then started, then settled. */
THIS SynthThread *stl_ctorWithLanguage(SynthThread *t, void *app, void *state,
                                       int32_t language)
{
    LangIdentifier want;

    qt_ctor(t);
    sti_langCtor(ST_LANG(t));
    stl_build(t, app, state);

    th_start(t, RAL_THREAD_PRIORITY_NORMAL);

    want.packed = language;
    want.packed = language;
    lang_setString(&want);
    memcpy(ST_LANG(t), &want, sizeof want);

    stl_takeShared(t);
    return t;
}

/* ---- settling on a language ---- */

/* Put the thread on a language and get the engine behind it ready to speak.

   The old engine is given back first, because the array only keeps one per
   language and holding two would leak. What follows is the whole setup in
   order: ask the array for the engine, take its answers about what it can
   report, hand it the callbacks, put it through its own defaults, and tell
   the romanizer and the concatenative side which language they are now on.

   A language the array has not got is the one road out, and it gives the
   engine straight back rather than leaving a half-built thread behind. */
THIS int32_t stl_initialize(SynthThread *t, LangIdentifier *want)
{
    int32_t rc = ERR_NO_LANG;
    void *engine;
    EngCommand command;

    ea_removeEngine(ST_ENGINES(t), ST_LANG(t));
    ST_ENGINE(t) = 0;
    ST_LANG(t)->packed = 0;
    lang_setString(ST_LANG(t));

    if (ST_ROMAN(t))
        rz_removeUnused(ST_ROMAN(t), want);

    engine = ea_getEngine(ST_ENGINES(t), want);
    if (!engine)
        return rc;

    ST_FLAGS(t) = ea_getCallbackFnFlag(ST_ENGINES(t), want);
    ST_CORPORA(t) = ea_getCorporaVersion(ST_ENGINES(t), want);
    rc = OK;

    {
        EngSetPhonemes setPhonemes =
            (EngSetPhonemes)ENG_CALL_ON(engine, ENG_SET_PHONEMES);
        EngSetAnno setAnno = (EngSetAnno)ENG_CALL_ON(engine, ENG_ANNO_CB);

        setPhonemes(engine, 0);
        es_paramFromEngine(ST_STATE(t), ECI_PARAM_PHONEMES, 0);
        setAnno(engine, stb_staticAnnoCallback, t);
    }

    if (ST_CONCAT(t)
        && cm_engineSupports(ST_CONCAT(t), (want->packed & 0xff0000) >> 16,
                              want->packed & 0xff)) {
        EngSetVoice setVoice = (EngSetVoice)ENG_CALL_ON(engine, ENG_VOICE_CB);
        EngSetUser setSPR = (EngSetUser)ENG_CALL_ON(engine, ENG_SPR_CB);

        setVoice(engine, stb_staticVoiceChangeCallback, t);
        setSPR(engine, stb_staticEnhancedSPRCallback, t);
    }

    /* The reporting goes in as nothing to begin with; it is put in properly
       once the engine has been started. */
    if (ST_FLAGS(t) & STF_ROMANIZING) {
        EngSetUser set = (EngSetUser)ENG_CALL_ON(engine, ENG_USER_INDEX_CB);

        set(engine, 0, t);
        cm_registerCallbackB(ST_CONCAT(t), CAT_CB_USER_INDEX, 0, t);
    } else if (ST_FLAGS(t) & STF_WORD_STARTS) {
        EngSetIndex set = (EngSetIndex)ENG_CALL_ON(engine, ENG_WORD_START_CB);

        set(engine, 0, t);
        cm_registerCallbackA(ST_CONCAT(t), CAT_CB_WORD_MARK, 0, t);
    }
    cm_registerCallbackA(ST_CONCAT(t), CAT_CB_BREAK, 0, t);

    command = (EngCommand)ENG_CALL_ON(engine, ENG_COMMAND);
    ST_DIRECT(t) = 1;
    if (command(engine, CMD_DEFAULTS))
        rc = ERR_ENGINE;
    /* Newer corpora want one more line than the older ones do. */
    if (ST_CORPORA(t) > 0 && command(engine, CMD_NORMALISE))
        rc = ERR_ENGINE;
    ST_DIRECT(t) = 0;

    if (rc != OK || !stw_engineInitialize(t, engine)) {
        ea_removeEngine(ST_ENGINES(t), want);
        return rc;
    }

    ST_ENGINE(t) = engine;
    stw_isOldEngine(t);

    ST_LANG(t)->packed = want->packed;
    lang_setString(ST_LANG(t));
    es_paramFromEngine(ST_STATE(t), ECI_PARAM_LANGUAGE, (int32_t)want->packed);

    if (rz_setParam(ST_ROMAN(t), ROM_LANGUAGE, (int32_t)want->packed) == -1)
        rc = ERR_ROM_LANG;
    if (cm_setParam(ST_CONCAT(t), CAT_LANGUAGE, (int32_t)want->packed, 1) == -1) {
        rc = ERR_CAT_LANG;
        return rc;
    }

    if (cm_usingConcatenativeEngine(ST_CONCAT(t))) {
        cm_registerCallbackC(ST_CONCAT(t), stb_staticSynthCallback, t);
        cm_registerCallbackA(ST_CONCAT(t), CAT_CB_PHONEME,
                                  stb_staticTorrentPhonemeCallback, t);
        if (ST_FLAGS(t) & STF_WORD_STARTS)
            cm_registerCallbackA(ST_CONCAT(t), CAT_CB_WORD_START,
                                      stb_staticWordCallback, t);
        if (ST_FLAGS(t) & STF_WORD_MARKS)
            cm_registerCallbackA(ST_CONCAT(t), CAT_CB_WORD_MARK,
                                      stb_staticWordIndexCallback, t);
        cm_registerCallbackA(ST_CONCAT(t), CAT_CB_BREAK,
                                  stb_staticSynthesisBreakCallback, t);
        ST_DIRECT(t) = 1;
        if (command(engine, CMD_CONCATENATIVE))
            rc = ERR_ENGINE;
        ST_DIRECT(t) = 0;
        rz_setParam(ST_ROMAN(t), ROM_CONCATENATIVE, 1);
    }

    fm_autoLoadFilter(ST_FILTERS(t), want);
    return rc;
}

/* ---- stopping ---- */

/* Throw away everything in flight and put the thread back where it started.

   The device is stopped by its own direct calls rather than by messages,
   because the messages are exactly what is being thrown away. Everything is
   suspended first and resumed at the end, so nothing arrives half way
   through. */
THIS int32_t stl_stop(SynthThread *t)
{
    int32_t rc = OK;
    void *lock;
    AppQueue *app;

    if (ST_SOUND(t)) {
        int16_t status;

        stm_qtSuspend(ST_SOUND(t));
        status = snd_getStatusDirect(ST_SOUND(t));
        if (status == 3 || status == 1) {
            if (!snd_resetDirect(ST_SOUND(t))
                || !snd_closeDirect(ST_SOUND(t)))
                rc = ERR_NO_SOUND;
        } else if (status == 5) {
            rc = ERR_NO_SOUND;
        }
        /* Wake anything waiting on the mark the device would have reached. */
        sy_eventSignal(ST_SYNCED(t));
    }

    lock = ST_LOCK(t);
    sy_mutexWait(lock, -1);
    if (ST_ENGINE(t)) {
        EngReset reset = (EngReset)ENG_CALL_ON(ST_ENGINE(t), ENG_RESET);

        if (reset(ST_ENGINE(t), 1))
            rc = ERR_ENGINE;
    }
    sy_mutexRelease(lock);

    app = (AppQueue *)ST_APP(t);
    app->vt->suspend(app);
    rz_stop(ST_ROMAN(t));
    stm_qtSuspend(t);
    rz_clear(ST_ROMAN(t));

    if (ST_ENGINE(t)) {
        EngReset reset = (EngReset)ENG_CALL_ON(ST_ENGINE(t), ENG_RESET);

        if (reset(ST_ENGINE(t), 0))
            rc = ERR_ENGINE;
    }

    ST_INDEXQ(t)->total = 0;
    el_listReset(ST_INDEXQ(t));
    eq_reset(ST_MARKS(t));

    ST_SAMPHELD(t) = 0;
    ST_PHONHELD(t) = 0;
    ST_SAMPLES(t) = 0;
    ST_LASTMARK(t) = 0;
    ST_PENDING(t) = 0;
    ST_STOPPED(t) = 0;
    ST_POSTED(t) = 0;
    sy_eventUnsignal(ST_SYNCED(t));

    if (ST_SOUND(t))
        stm_qtResume(ST_SOUND(t));
    stm_qtResume(t);
    rz_resume(ST_ROMAN(t));

    /* The application queue forgets what it was told about too, and what it
       has collected with it, or the next run would be numbered from where the
       last one stopped. Both, or the two drift: a stale count of what was
       collected that happens to equal the fresh count of what was posted
       reads as nothing outstanding, and the queue delivers none of what is
       sitting in it. */
    APP_POSTED(ST_APP(t)) = 0;
    APP_SEEN(ST_APP(t)) = 0;
    stm_pauseMessageQueue(ST_APP(t), 0);
    app->vt->resume(app);
    return rc;
}

/* ---- taking apart ---- */

/* Stop, wait for the thread to actually be gone, then give back everything
   in the order it was taken. Whoever is last out gives back the two shared
   things as well. */
THIS void stl_dtor(SynthThread *t)
{
    *(const ThreadVtbl **)t = &vt_synthThread;
    stl_stop(t);
    th_terminateAndWait(t);
    ST_POSTED(t) = 0;

    if (ST_OUTFMT(t)) {
        sm_removeAudioFormat(st_soundManager, ST_OUTFMT(t));
        ST_SOUND(t) = 0;
        ST_OUTFMT(t) = 0;
    }
    if (ST_CONVERTER(t)) {
        pcm_cvt_dtor(ST_CONVERTER(t));
        cpp_delete(ST_CONVERTER(t));
        ST_CONVERTER(t) = 0;
    }

    sti_deleteAll(ST_INDEXMGR(t));
    ST_SAMPBUF(t) = 0;
    ST_SAMPROOM(t) = 0;
    ST_SAMPHELD(t) = 0;
    if (ST_ENGPHON(t)) {
        cpp_delete(ST_ENGPHON(t));
        ST_ENGPHON(t) = 0;
    }
    ST_PHONBUF(t) = 0;
    ST_ENGPHONROOM(t) = 0;
    ST_PHONHELD(t) = 0;

    if (ST_BLOCKER(t)) {
        sy_semDtor(ST_BLOCKER(t));
        cpp_delete(ST_BLOCKER(t));
        ST_BLOCKER(t) = 0;
    }
    if (ST_LASTLOOKUP(t)) { cpp_delete(ST_LASTLOOKUP(t)); ST_LASTLOOKUP(t) = 0; }
    if (ST_LASTKEY(t)) { cpp_delete(ST_LASTKEY(t)); ST_LASTKEY(t) = 0; }
    if (ST_LASTVALUE(t)) { cpp_delete(ST_LASTVALUE(t)); ST_LASTVALUE(t) = 0; }

    if (ST_ROMAN(t)) {
        rz_dtor(ST_ROMAN(t));
        cpp_delete(ST_ROMAN(t));
    }
    if (ST_CONCAT(t)) {
        cm_dtor(ST_CONCAT(t));
        cpp_delete(ST_CONCAT(t));
    }
    if (ST_FILTERS(t)) {
        fm_dtor(ST_FILTERS(t));
        cpp_delete(ST_FILTERS(t));
    }
    if (ST_MARKS(t)) {
        MarkQueue *q = ST_MARKS(t);

        q->vt->destroy(q, 1);
    }

    sy_mutexWait(st_protectInitialization, -1);
    st_nRefPointers -= 1;
    if (st_nRefPointers == 0) {
        if (st_soundManager) {
            sm_dtor(st_soundManager);
            cpp_delete(st_soundManager);
        }
        st_soundManager = 0;
        if (st_phonemes) {
            /* The phoneme table's destructor and the engine list's are the
               same code, so the linker kept one of the two names. */
            eng_dtor(st_phonemes);
            cpp_delete(st_phonemes);
        }
        st_phonemes = 0;
    }
    sy_mutexRelease(st_protectInitialization);

    sy_eventDtor(ST_SYNCED(t));
    sy_mutexDtor(ST_IDXLOCK(t));
    mm_dtor(ST_IDXMEM(t));
    /* The index queue is two lists over one another, and each has to be put
       back to its own table before being emptied. */
    *(const void **)ST_INDEXQ(t) = &vtbl_elistqueue;
    el_listReset(ST_INDEXQ(t));
    *(const void **)ST_INDEXQ(t) = &vtbl_eslist;
    el_listReset(ST_INDEXQ(t));
    sy_mutexDtor(ST_LOCK(t));
    ini_dtor(ST_INIFILE(t));
    eng_dtor(ST_ENGINELIST(t));
    qt_dtor(t);
}

/* And the one the table names, which frees as well when asked. */
THIS void *stl_destroy(SynthThread *t, int32_t free_it)
{
    stl_dtor(t);
    if (free_it & 1)
        cpp_delete(t);
    return t;
}

const ThreadVtbl vt_synthThread = {
    { (void *)stl_destroy, (void *)qt_terminate, (void *)qt_waitForExit,
      (void *)qt_run, (void *)qt_setToTerminate,
      (void *)qt_translateMessage }
};

ALIAS("??_7SynthThread@@6B@", "vt_synthThread");
ALIAS("??0SynthThread@@QAE@PAVETIappMessageQueue@@PAVECIstate@@@Z",
      "stl_ctor");
ALIAS("??0SynthThread@@QAE@PAVETIappMessageQueue@@PAVECIstate@@"
      "W4ECILanguageDialect@@@Z", "stl_ctorWithLanguage");
ALIAS("??1SynthThread@@UAE@XZ", "stl_dtor");
ALIAS("??_GSynthThread@@UAEPAXI@Z", "stl_destroy");
ALIAS("??_ESynthThread@@UAEPAXI@Z", "stl_destroy");
ALIAS("?initialize@SynthThread@@QAEJPAVLangIdentifier@@@Z", "stl_initialize");
ALIAS("?stop@SynthThread@@QAEJXZ", "stl_stop");
