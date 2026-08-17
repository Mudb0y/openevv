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
#define ENG_CALL_ON(e, off) (*(void **)(*(char **)(e) + (off)))

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
#define ST_ENGINELIST(t) ST_AT(t, 0x08c)   /* the EngineArray's own base */
#define ST_INIFILE(t)    ST_AT(t, 0x12c)
#define ST_LANG(t)       ((LangIdentifier *)ST_AT(t, 0x2e0))
#define ST_IDXMEM(t)     ST_AT(t, 0x334)   /* IndexManager: memory then a lock */
#define ST_IDXLOCK(t)    ST_AT(t, 0x358)
/* Three blocks the thread hands out and takes back, and the complaint the
   layer above reads once it has the object. */
#define ST_SPARE0(t)     ST_PTR(t, 0x3a0)
#define ST_SPARE1(t)     ST_PTR(t, 0x3a4)
#define ST_SPARE2(t)     ST_PTR(t, 0x3a8)
#define ST_STATUS(t)     ST_I32(t, 0x3bc)
#define ST_STOPPED(t)    ST_I32(t, 0x3c8)

/* How big each of the things the constructor makes is. */
#define SIZE_ROMANIZER   0x278
#define SIZE_CONCAT      0x2c0
#define SIZE_MARKQUEUE   0x14
#define MARKQUEUE_ROOM   0x200
#define SIZE_FILTERS     0x144
#define SIZE_SOUNDMGR    0x6c
#define SIZE_PHONEMES    0x12c
#define INDEX_BLOCK      8

extern int32_t RAL_THREAD_PRIORITY_NORMAL MANGLED("_RAL_THREAD_PRIORITY_NORMAL");

/* What every synthesis thread in the process shares, and how many of them
   are still holding it. */
extern void *m_soundManager
    MANGLED("?m_soundManager@SynthThread@@0PAVSoundManager@@A");
extern void *m_phonemes MANGLED("?m_phonemes@SynthThread@@0PAVPhonemes@@A");
extern int32_t nRefPointers MANGLED("?nRefPointers@SynthThread@@0HA");
extern uint8_t m_protectInitialization[]
    MANGLED("?m_protectInitialization@SynthThread@@0VMutex@@A");

extern THIS void *qt_ctor(void *t) MANGLED("??0ETImessageQueueThread@@QAE@XZ");
extern THIS void qt_dtor(void *t) MANGLED("??1ETImessageQueueThread@@UAE@XZ");
extern THIS int16_t qt_suspend(void *t)
    MANGLED("?suspend@ETImessageQueueThread@@QAEFXZ");
extern THIS int16_t qt_resume(void *t)
    MANGLED("?resume@ETImessageQueueThread@@QAEFXZ");
extern THIS int32_t thread_start(void *t, int32_t priority)
    MANGLED("?start@ETIThread@@QAEHH@Z");
extern THIS int32_t thread_terminateAndWait(void *t)
    MANGLED("?terminateAndWait@ETIThread@@QAEHXZ");

extern THIS void *mutex_ctor(void *m, int32_t kind)
    MANGLED("??0Mutex@@QAE@H@Z");
extern THIS void *event_ctor(void *e, int32_t kind)
    MANGLED("??0ETIEvent@@QAE@H@Z");
extern THIS void event_dtor(void *e) MANGLED("??1ETIEvent@@QAE@XZ");
extern THIS int32_t event_signal(void *e) MANGLED("?signal@ETIEvent@@QAEHXZ");
extern THIS int32_t event_unsignal(void *e)
    MANGLED("?unsignal@ETIEvent@@QAEHXZ");

extern THIS void *enginelist_ctor(void *a) MANGLED("??0EngineList@@QAE@XZ");
extern THIS void enginelist_dtor(void *a) MANGLED("??1EngineList@@QAE@XZ");
extern THIS void *inifile_ctor(void *r) MANGLED("??0IniFileReader@@QAE@XZ");
extern THIS void inifile_dtor(void *r) MANGLED("??1IniFileReader@@QAE@XZ");
extern THIS void *indexq_ctor(void *q) MANGLED("??0IndexQueue@@QAE@XZ");
extern THIS void *memmgr_ctor(void *m, uint32_t block)
    MANGLED("??0MemoryManager@@QAE@K@Z");
extern THIS void memmgr_dtor(void *m) MANGLED("??1MemoryManager@@QAE@XZ");
extern THIS void *lang_ctor(LangIdentifier *l)
    MANGLED("??0LangIdentifier@@QAE@XZ");
extern THIS void lang_setString(LangIdentifier *l)
    MANGLED("?setString@LangIdentifier@@AAEXXZ");

extern THIS void *rom_ctor(void *r, SynthThread *t)
    MANGLED("??0RomanizerManager@@QAE@PAVSynthThread@@@Z");
extern THIS void rom_dtor(void *r) MANGLED("??1RomanizerManager@@QAE@XZ");
extern THIS void *cat_ctor(void *c, SynthThread *t)
    MANGLED("??0ConcatenationManager@@QAE@PAVSynthThread@@@Z");
extern THIS void cat_dtor(void *c) MANGLED("??1ConcatenationManager@@QAE@XZ");
extern THIS void *fm_ctor(void *m, SynthThread *t)
    MANGLED("??0FilterManager@@QAE@PAVSynthThread@@@Z");
extern THIS void fm_dtor(void *m) MANGLED("??1FilterManager@@QAE@XZ");
extern THIS void *queue_ctor(void *q, uint32_t room)
    MANGLED("??0ETIqueue@@QAE@K@Z");
extern THIS void *soundmgr_ctor(void *m) MANGLED("??0SoundManager@@QAE@XZ");
extern THIS void soundmgr_dtor(void *m) MANGLED("??1SoundManager@@QAE@XZ");
extern THIS void *phonemes_ctor(void *p) MANGLED("??0Phonemes@@QAE@XZ");
extern THIS void audio_dtor(void *c) MANGLED("??1AudioConverter@@QAE@XZ");
extern THIS void semaphore_dtor(void *s) MANGLED("??1Semaphore@@QAE@XZ");

extern THIS void sm_removeAudioFormat(void *m, void *f)
    MANGLED("?removeAudioFormat@SoundManager@@QAEXPAVAudioFormat@@@Z");
extern THIS void idx_deleteAll(void *m)
    MANGLED("?deleteAll@IndexManager@@QAEXXZ");
extern THIS void elist_reset(void *l) MANGLED("?reset@EList@@QAEXXZ");
extern THIS void etiqueue_reset(void *q) MANGLED("?reset@ETIqueue@@QAEXXZ");

extern THIS void ea_removeEngine(void *a, const LangIdentifier *l)
    MANGLED("?removeEngine@EngineArray@@QAEXQBVLangIdentifier@@@Z");
extern THIS void *ea_getEngine(void *a, const LangIdentifier *l)
    MANGLED("?getEngine@EngineArray@@QAEPAVEngineWrapper@@QBVLangIdentifier@@@Z");
extern THIS uint32_t ea_getCallbackFnFlag(void *a, const LangIdentifier *l)
    MANGLED("?getCallbackFnFlag@EngineArray@@QAEKQBVLangIdentifier@@@Z");
extern THIS uint32_t ea_getCorporaVersion(void *a, const LangIdentifier *l)
    MANGLED("?getCorporaVersion@EngineArray@@QAEKQBVLangIdentifier@@@Z");

extern THIS void rom_removeUnused(void *r, LangIdentifier *l)
    MANGLED("?removeUnusedRomanizer@RomanizerManager@@QAEXPAVLangIdentifier@@@Z");
extern THIS int32_t rom_setParam(void *r, int32_t which, int32_t value)
    MANGLED("?setParam@RomanizerManager@@QAEHJH@Z");
extern THIS int32_t rom_stop(void *r) MANGLED("?stop@RomanizerManager@@QAEHXZ");
extern THIS void rom_clear(void *r) MANGLED("?clear@RomanizerManager@@QAEXXZ");
extern THIS int32_t rom_resume(void *r)
    MANGLED("?resume@RomanizerManager@@QAEHXZ");

extern THIS int32_t cat_inUse(void *c)
    MANGLED("?usingConcatenativeEngine@ConcatenationManager@@QAEHXZ");
extern THIS int32_t cat_setParam(void *c, int32_t which, int32_t value,
                                 int32_t extra)
    MANGLED("?setParam@ConcatenationManager@@QAEHJHH@Z");
extern THIS int32_t cat_engineSupports(void *c, uint32_t a, uint32_t b)
    MANGLED("?engineSupportsConcatenative@ConcatenationManager@@QAEHKK@Z");
extern THIS void cat_registerIndexCallback(void *c, uint32_t which,
                                           IndexCallback cb, void *param)
    MANGLED("?registerCallback@ConcatenationManager@@QAEXKP6AXHPAX@Z0@Z");
extern THIS void cat_registerUserCallback(void *c, uint32_t which,
                                          UserCallback cb, void *param)
    MANGLED("?registerCallback@ConcatenationManager@@QAEXKP6AXPAX@Z0@Z");
extern THIS void cat_registerSynthCallback(void *c, SynthCallback cb,
                                           void *param)
    MANGLED("?registerCallback@ConcatenationManager@@QAEXP6AXHPAJPAX@Z1@Z");

extern THIS void fm_autoLoadFilter(void *m, LangIdentifier *l)
    MANGLED("?autoLoadFilter@FilterManager@@QAEXPAVLangIdentifier@@@Z");

extern THIS void st_paramFromEngine(void *s, int32_t which, int32_t value)
    MANGLED("?paramFromEngine@ECIstate@@QAEXJJ@Z");
extern THIS void app_pause(void *a, int32_t how)
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
extern THIS void qt_terminate_fn(void *t)
    MANGLED("?terminate@ETImessageQueueThread@@MAEXXZ");
extern THIS int32_t qt_waitForExit_fn(void *t)
    MANGLED("?waitForExit@ETImessageQueueThread@@MAEHXZ");
extern THIS uint32_t qt_run_fn(void *t)
    MANGLED("?run@ETImessageQueueThread@@MAEKXZ");
extern THIS void qt_setToTerminate_fn(void *t)
    MANGLED("?setToTerminate@ETImessageQueueThread@@MAEXXZ");
extern THIS void qt_translateMessage_fn(void *t, void **m)
    MANGLED("?translateMessage@ETImessageQueueThread@@MAEXPAPAVETImessage@@@Z");

/* The two tables the index queue wears in turn while it is emptied. */
extern const void *vtbl_eListQueue MANGLED("??_7EListQueue@@6B@");
extern const void *vtbl_eSList MANGLED("??_7ESList@@6B@");

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
    enginelist_ctor(ST_ENGINELIST(t));
    inifile_ctor(ST_INIFILE(t));
    ST_ENGINE(t) = 0;

    mutex_ctor(ST_LOCK(t), 0);
    ST_POSTED(t) = 0;
    ST_SAMPLES(t) = 0;
    ST_PENDING(t) = 0;
    ST_LASTMARK(t) = 0;
    indexq_ctor(ST_INDEXQ(t));
    ST_OUTFMT(t) = 0;
    ST_SOUND(t) = 0;
    memmgr_ctor(ST_IDXMEM(t), INDEX_BLOCK);
    mutex_ctor(ST_IDXLOCK(t), 0);
    event_ctor(ST_SYNCED(t), 0);

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
    ST_SPARE0(t) = 0;
    ST_SPARE1(t) = 0;
    ST_SPARE2(t) = 0;
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

    p = cpp_new(SIZE_ROMANIZER);
    ST_ROMAN(t) = p ? rom_ctor(p, t) : 0;
    if (!ST_ROMAN(t))
        ST_STATUS(t) = ERR_FAILED;

    p = cpp_new(SIZE_CONCAT);
    ST_CONCAT(t) = p ? cat_ctor(p, t) : 0;

    p = cpp_new(SIZE_MARKQUEUE);
    ST_PTR(t, 0x3b4) = p ? queue_ctor(p, MARKQUEUE_ROOM) : 0;
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

    mutex_wait(m_protectInitialization, -1);

    if (!m_soundManager) {
        p = cpp_new(SIZE_SOUNDMGR);
        m_soundManager = p ? soundmgr_ctor(p) : 0;
        if (!m_soundManager)
            ST_STATUS(t) = ERR_FAILED;
    }
    if (!m_phonemes) {
        p = cpp_new(SIZE_PHONEMES);
        m_phonemes = p ? phonemes_ctor(p) : 0;
        if (!m_phonemes)
            ST_STATUS(t) = ERR_FAILED;
    }
    nRefPointers += 1;

    mutex_release(m_protectInitialization);
}

/* Built without a language: the thread is not started and no language is
   settled on. Whoever wants either has to ask for it afterwards. */
THIS SynthThread *stl_ctor(SynthThread *t, void *app, void *state)
{
    qt_ctor(t);
    stl_build(t, app, state);
    /* The language record is zeroed and named by hand here rather than
       constructed, which comes to the same thing. */
    ST_LANG(t)->id = 0;
    ST_LANG(t)->id = 0;
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
    lang_ctor(ST_LANG(t));
    stl_build(t, app, state);

    thread_start(t, RAL_THREAD_PRIORITY_NORMAL);

    want.id = language;
    want.id = language;
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
    ST_LANG(t)->id = 0;
    lang_setString(ST_LANG(t));

    if (ST_ROMAN(t))
        rom_removeUnused(ST_ROMAN(t), want);

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
        st_paramFromEngine(ST_STATE(t), ECI_PARAM_PHONEMES, 0);
        setAnno(engine, stb_staticAnnoCallback, t);
    }

    if (ST_CONCAT(t)
        && cat_engineSupports(ST_CONCAT(t), (want->id & 0xff0000) >> 16,
                              want->id & 0xff)) {
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
        cat_registerUserCallback(ST_CONCAT(t), CAT_CB_USER_INDEX, 0, t);
    } else if (ST_FLAGS(t) & STF_WORD_STARTS) {
        EngSetIndex set = (EngSetIndex)ENG_CALL_ON(engine, ENG_WORD_START_CB);

        set(engine, 0, t);
        cat_registerIndexCallback(ST_CONCAT(t), CAT_CB_WORD_MARK, 0, t);
    }
    cat_registerIndexCallback(ST_CONCAT(t), CAT_CB_BREAK, 0, t);

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

    ST_LANG(t)->id = want->id;
    lang_setString(ST_LANG(t));
    st_paramFromEngine(ST_STATE(t), ECI_PARAM_LANGUAGE, (int32_t)want->id);

    if (rom_setParam(ST_ROMAN(t), ROM_LANGUAGE, (int32_t)want->id) == -1)
        rc = ERR_ROM_LANG;
    if (cat_setParam(ST_CONCAT(t), CAT_LANGUAGE, (int32_t)want->id, 1) == -1) {
        rc = ERR_CAT_LANG;
        return rc;
    }

    if (cat_inUse(ST_CONCAT(t))) {
        cat_registerSynthCallback(ST_CONCAT(t), stb_staticSynthCallback, t);
        cat_registerIndexCallback(ST_CONCAT(t), CAT_CB_PHONEME,
                                  stb_staticTorrentPhonemeCallback, t);
        if (ST_FLAGS(t) & STF_WORD_STARTS)
            cat_registerIndexCallback(ST_CONCAT(t), CAT_CB_WORD_START,
                                      stb_staticWordCallback, t);
        if (ST_FLAGS(t) & STF_WORD_MARKS)
            cat_registerIndexCallback(ST_CONCAT(t), CAT_CB_WORD_MARK,
                                      stb_staticWordIndexCallback, t);
        cat_registerIndexCallback(ST_CONCAT(t), CAT_CB_BREAK,
                                  stb_staticSynthesisBreakCallback, t);
        ST_DIRECT(t) = 1;
        if (command(engine, CMD_CONCATENATIVE))
            rc = ERR_ENGINE;
        ST_DIRECT(t) = 0;
        rom_setParam(ST_ROMAN(t), ROM_CONCATENATIVE, 1);
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

        qt_suspend(ST_SOUND(t));
        status = snd_getStatusDirect(ST_SOUND(t));
        if (status == 3 || status == 1) {
            if (!snd_resetDirect(ST_SOUND(t))
                || !snd_closeDirect(ST_SOUND(t)))
                rc = ERR_NO_SOUND;
        } else if (status == 5) {
            rc = ERR_NO_SOUND;
        }
        /* Wake anything waiting on the mark the device would have reached. */
        event_signal(ST_SYNCED(t));
    }

    lock = ST_LOCK(t);
    mutex_wait(lock, -1);
    if (ST_ENGINE(t)) {
        EngReset reset = (EngReset)ENG_CALL_ON(ST_ENGINE(t), ENG_RESET);

        if (reset(ST_ENGINE(t), 1))
            rc = ERR_ENGINE;
    }
    mutex_release(lock);

    app = (AppQueue *)ST_APP(t);
    app->vt->suspend(app);
    rom_stop(ST_ROMAN(t));
    qt_suspend(t);
    rom_clear(ST_ROMAN(t));

    if (ST_ENGINE(t)) {
        EngReset reset = (EngReset)ENG_CALL_ON(ST_ENGINE(t), ENG_RESET);

        if (reset(ST_ENGINE(t), 0))
            rc = ERR_ENGINE;
    }

    *(int32_t *)((char *)ST_INDEXQ(t) + 0x0c) = 0;
    elist_reset(ST_INDEXQ(t));
    etiqueue_reset(ST_MARKS(t));

    ST_SAMPHELD(t) = 0;
    ST_PHONHELD(t) = 0;
    ST_SAMPLES(t) = 0;
    ST_LASTMARK(t) = 0;
    ST_PENDING(t) = 0;
    ST_STOPPED(t) = 0;
    ST_POSTED(t) = 0;
    event_unsignal(ST_SYNCED(t));

    if (ST_SOUND(t))
        qt_resume(ST_SOUND(t));
    qt_resume(t);
    rom_resume(ST_ROMAN(t));

    /* The application queue forgets what it was told about too, or the next
       run would be numbered from where the last one stopped. */
    APP_POSTED(ST_APP(t)) = 0;
    *(int32_t *)((char *)ST_APP(t) + 0x5c) = 0;
    app_pause(ST_APP(t), 0);
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
    thread_terminateAndWait(t);
    ST_POSTED(t) = 0;

    if (ST_OUTFMT(t)) {
        sm_removeAudioFormat(m_soundManager, ST_OUTFMT(t));
        ST_SOUND(t) = 0;
        ST_OUTFMT(t) = 0;
    }
    if (ST_CONVERTER(t)) {
        audio_dtor(ST_CONVERTER(t));
        cpp_delete(ST_CONVERTER(t));
        ST_CONVERTER(t) = 0;
    }

    idx_deleteAll(ST_INDEXMGR(t));
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
        semaphore_dtor(ST_BLOCKER(t));
        cpp_delete(ST_BLOCKER(t));
        ST_BLOCKER(t) = 0;
    }
    if (ST_SPARE0(t)) { cpp_delete(ST_SPARE0(t)); ST_SPARE0(t) = 0; }
    if (ST_SPARE1(t)) { cpp_delete(ST_SPARE1(t)); ST_SPARE1(t) = 0; }
    if (ST_SPARE2(t)) { cpp_delete(ST_SPARE2(t)); ST_SPARE2(t) = 0; }

    if (ST_ROMAN(t)) {
        rom_dtor(ST_ROMAN(t));
        cpp_delete(ST_ROMAN(t));
    }
    if (ST_CONCAT(t)) {
        cat_dtor(ST_CONCAT(t));
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

    mutex_wait(m_protectInitialization, -1);
    nRefPointers -= 1;
    if (nRefPointers == 0) {
        if (m_soundManager) {
            soundmgr_dtor(m_soundManager);
            cpp_delete(m_soundManager);
        }
        m_soundManager = 0;
        if (m_phonemes) {
            /* The phoneme table's destructor and the engine list's are the
               same code, so the linker kept one of the two names. */
            enginelist_dtor(m_phonemes);
            cpp_delete(m_phonemes);
        }
        m_phonemes = 0;
    }
    mutex_release(m_protectInitialization);

    event_dtor(ST_SYNCED(t));
    mutex_dtor(ST_IDXLOCK(t));
    memmgr_dtor(ST_IDXMEM(t));
    /* The index queue is two lists over one another, and each has to be put
       back to its own table before being emptied. */
    *(const void **)ST_INDEXQ(t) = &vtbl_eListQueue;
    elist_reset(ST_INDEXQ(t));
    *(const void **)ST_INDEXQ(t) = &vtbl_eSList;
    elist_reset(ST_INDEXQ(t));
    mutex_dtor(ST_LOCK(t));
    inifile_dtor(ST_INIFILE(t));
    enginelist_dtor(ST_ENGINELIST(t));
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
    { (void *)stl_destroy, (void *)qt_terminate_fn, (void *)qt_waitForExit_fn,
      (void *)qt_run_fn, (void *)qt_setToTerminate_fn,
      (void *)qt_translateMessage_fn }
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
