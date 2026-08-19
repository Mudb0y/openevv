/* What the synthesis thread does when it takes a message off the queue.

   Every one of these is the far half of a sender in eci_synthmsg.c. They all
   run on the thread rather than on the caller, which is why almost all of
   them end the same way: take the lock, take what the message stood for off
   the count of outstanding work, let go. Anything that gives up early leaves
   the count alone, so work that was never done is never counted as done.

   Nearly all of them begin the same way too, by flushing whatever text is
   half-processed. A parameter change has to land between words rather than
   inside one, and stw_processRemaining is what makes sure of that.

   The two long ones, addTextRun and changeLanguageRun, are not here yet;
   they are still the original's.

   None of these read the application-queue slot the sender claimed for them,
   with the single exception of synthesizeRun, which hands it on to whatever
   stops the synthesis. The rest take it and ignore it. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "eci_synththread.h"
#include "evv_abi.h"
#include "evv_arena.h"

/* Parameters the concatenative side understands. Numbered from its own list,
   which is not the one the published interface uses. */
#define CAT_VOLUME        0x0c
#define CAT_SPEED         0x0f
#define CAT_PITCH         0x10
#define CAT_EMPHASIS      0x13
#define CAT_FLUCTUATION   0x14
#define CAT_SPEED_NAMED   0x15
#define CAT_PITCH_NAMED   0x16
#define CAT_FLUCT_NAMED   0x17

/* And what the romanizer understands. */
#define ROM_CONCATENATIVE 0x3e8
#define ROM_VOICE         0x3ea
#define ROM_SAMPLE_RATE   0x3eb

/* Which parameter a change is being reported back under. */
#define ECI_PARAM_PHONEMES 4
#define ECI_PARAM_VOICE    0x10
#define ECI_PARAM_ROM      0x0e

/* What the application is told about. */
#define APP_INDEX_LOST     6
#define APP_CONCATENATIVE  0xc

/* Where the concatenative callback registers itself. */
#define CAT_CALLBACK_WORD  2

/* Slots of the engine's own table, by byte offset as its callers use them. */
#define ENG_INSERT_INDEX   0x5c
#define ENG_SET_PHONEMES   0x64
#define ENG_WORD_CALLBACK  0x94

typedef void (*IndexCallback)(int32_t, void *);

/* The engine's virtuals are stdcall with the object pushed like any other
   argument, not thiscall with it in a register. That is how the compiler
   marked them and it is not a detail one can guess wrong quietly: passing
   the object the other way leaves the stack one word out and the engine
   wanders off into whatever was under it. */
#define ENGCALL __attribute__((stdcall))

typedef ENGCALL int32_t (*EngInsertIndex)(void *engine, uint32_t id);
typedef ENGCALL void (*EngSetPhonemes)(void *engine, int32_t on);
typedef ENGCALL void (*EngWordCallback)(void *engine, IndexCallback cb,
                                        void *param);

extern THIS void stw_processRemaining(SynthThread *t)
    MANGLED("?processRemaining@SynthThread@@QAEXXZ");
extern THIS void stw_shutDown(SynthThread *t)
    MANGLED("?shutDown@SynthThread@@AAEXXZ");
extern THIS void stw_stopSynthesis(SynthThread *t, int32_t seq)
    MANGLED("?stopSynthesis@SynthThread@@AAEXJ@Z");
extern THIS void stb_postRomanizerError(SynthThread *t, int32_t which)
    MANGLED("?postRomanizerError@SynthThread@@AAEXH@Z");
extern THIS void stb_postEngineError(SynthThread *t)
    MANGLED("?postEngineError@SynthThread@@AAEXXZ");
extern THIS int32_t stm_newFilter(SynthThread *t, int32_t engine, int32_t which,
                              void **out)
    MANGLED("?newFilter@SynthThread@@QAEJJJPAPAX@Z");
extern THIS int32_t stm_activateFilter(SynthThread *t, void *f)
    MANGLED("?activateFilter@SynthThread@@QAEJPAX@Z");
extern THIS int32_t stm_deactivateFilter(SynthThread *t, void *f)
    MANGLED("?deactivateFilter@SynthThread@@QAEJPAX@Z");
extern THIS int32_t stm_deleteFilter(SynthThread *t, void *f)
    MANGLED("?deleteFilter@SynthThread@@QAEJPAX@Z");
extern THIS int32_t stw_createAudioConverter(SynthThread *t, void *format)
    MANGLED("?createAudioConverter@SynthThread@@AAEJAAUECIsampleFormat@@@Z");
extern void stb_staticWordIndexCallback(int32_t index, void *param)
    MANGLED("?staticWordIndexCallback@SynthThread@@CAXHPAX@Z");

extern THIS int32_t cm_setTorrentParam1(void *c, uint32_t which, int32_t value)
    MANGLED("?setTorrentParam1@ConcatenationManager@@QAEHKJ@Z");
extern THIS int32_t cm_voiceIsConcatenative(void *c, int32_t voice)
    MANGLED("?voiceIsConcatenative@ConcatenationManager@@QAEHH@Z");
extern THIS uint32_t cm_getActiveSampleRate(void *c)
    MANGLED("?getActiveSampleRate@ConcatenationManager@@QAEIXZ");
extern THIS int32_t cm_engineSupports(void *c, uint32_t a, uint32_t b)
    MANGLED("?engineSupportsConcatenative@ConcatenationManager@@QAEHKK@Z");
extern THIS int32_t cm_usingConcatenativeEngine(void *c)
    MANGLED("?usingConcatenativeEngine@ConcatenationManager@@QAEHXZ");
extern THIS void cm_registerCallbackA(void *c, uint32_t which,
                                      IndexCallback cb, void *param)
    MANGLED("?registerCallback@ConcatenationManager@@QAEXKP6AXHPAX@Z0@Z");

extern THIS int32_t rz_addParam(void *r, const char *text, int32_t len)
    MANGLED("?addParam@RomanizerManager@@QAEHPBDH@Z");
extern THIS int32_t rz_setParam(void *r, int32_t which, int32_t value)
    MANGLED("?setParam@RomanizerManager@@QAEHJH@Z");
extern THIS int32_t rz_insertIndex(void *r)
    MANGLED("?insertIndex@RomanizerManager@@QAEHXZ");

extern THIS void es_paramFromEngine(void *s, int32_t which, int32_t value)
    MANGLED("?paramFromEngine@ECIstate@@QAEXJJ@Z");
extern THIS int32_t aq_postUser(void *a, int32_t what, int32_t value)
    MANGLED("?postUser@ETIappMessageQueue@@QAEHJJ@Z");

extern THIS uint32_t sti_newIndex(void *m)
    MANGLED("?newIndex@IndexManager@@QAEKXZ");
extern THIS Index *sti_getIndex(void *m, uint32_t id)
    MANGLED("?getIndex@IndexManager@@QAEPAUIndex@@K@Z");
extern THIS int32_t iq_addOffsetFromLast(void *q, uint32_t id, int32_t off)
    MANGLED("?addOffsetFromLast@IndexQueue@@QAEHHK@Z");

extern THIS int32_t sy_semWait(void *s)
    MANGLED("?wait@Semaphore@@QAEHXZ");

/* The engine's own table, reached by byte offset because only a handful of
   its slots are named here and the rest are the original's. */
#define ENG_CALL(t, off) \
    (((void **)*(void ***)ST_ENGINE(t))[(off) / 4])

/* Done with a message: one fewer thing outstanding. Everything that finishes
   normally ends here, and everything that gives up early does not. */
static void finished(SynthThread *t)
{
    void *lock = ST_LOCK(t);

    sy_mutexWait(lock, -1);
    ST_PENDING(t) -= 1;
    sy_mutexRelease(lock);
}

/* ---- the parameter changes ---- */

/* All of these say the same thing to the concatenative side and nothing at
   all to anybody else, because the engine proper picks its parameters up
   from the state block rather than being told. */
static void toldConcat(SynthThread *t, uint32_t which, int32_t value)
{
    stw_processRemaining(t);
    if (ST_CONCAT(t))
        cm_setTorrentParam1(ST_CONCAT(t), which, value);
    finished(t);
}

/* And these three carry a string the sender copied, which is spent here.
   Note it is freed only if there was a concatenative side to give it to;
   with none, the copy is dropped on the floor. That is the original's, and
   it leaks a little on a voice that is not concatenative. */
static void toldConcatNamed(SynthThread *t, uint32_t which, int32_t name)
{
    stw_processRemaining(t);
    if (ST_CONCAT(t)) {
        cm_setTorrentParam1(ST_CONCAT(t), which, name);
        free((void *)name);
    }
    finished(t);
}

THIS void changeEmphasisRun(SynthThread *t, int32_t seq)
{
    (void)seq;
    /* Emphasis has no value of its own: asking for it is the whole message,
       so what goes across is a nought. */
    toldConcat(t, CAT_EMPHASIS, 0);
}

THIS void changeSpeedRun(SynthThread *t, int32_t v, int32_t seq)
{
    (void)seq;
    toldConcat(t, CAT_SPEED, v);
}

THIS void changePitchRun(SynthThread *t, int32_t v, int32_t seq)
{
    (void)seq;
    toldConcat(t, CAT_PITCH, v);
}

THIS void changeFluctuationRun(SynthThread *t, int32_t v, int32_t seq)
{
    (void)seq;
    toldConcat(t, CAT_FLUCTUATION, v);
}

THIS void changeSpeedStringRun(SynthThread *t, int32_t name, int32_t seq)
{
    (void)seq;
    toldConcatNamed(t, CAT_SPEED_NAMED, name);
}

THIS void changePitchStringRun(SynthThread *t, int32_t name, int32_t seq)
{
    (void)seq;
    toldConcatNamed(t, CAT_PITCH_NAMED, name);
}

THIS void changeFluctuationStringRun(SynthThread *t, int32_t name,
                                     int32_t seq)
{
    (void)seq;
    toldConcatNamed(t, CAT_FLUCT_NAMED, name);
}

/* Volume is the one that does not go across as it arrived. The published
   scale is nought to a hundred and the concatenative side wants tenths of a
   per cent in tenths of a bit, so it is scaled by a thousand and
   twenty-four. */
THIS void changeVolumeRun(SynthThread *t, int32_t v, int32_t seq)
{
    (void)seq;
    stw_processRemaining(t);
    if (ST_CONCAT(t))
        cm_setTorrentParam1(ST_CONCAT(t), CAT_VOLUME, (v << 10) / 100);
    finished(t);
}

/* ---- the ones that are their own shape ---- */

/* Turning phoneme marks on or off is one of the few things the engine itself
   has to be told, and the state block is told too so that a caller asking
   what the setting is gets the answer the engine acted on. */
THIS void setPhonemeIndiciesRun(SynthThread *t, int32_t on, int32_t seq)
{
    EngSetPhonemes set;

    (void)seq;
    stw_processRemaining(t);
    set = (EngSetPhonemes)ENG_CALL(t, ENG_SET_PHONEMES);
    set(ST_ENGINE(t), on);
    es_paramFromEngine(ST_STATE(t), ECI_PARAM_PHONEMES, on);
    finished(t);
}

/* A voice change is the widest of them: the sample format may have moved
   under it, the concatenative side may or may not have this voice, and if
   that answer has changed since last time the caller is told so. */
THIS void changeVoiceRun(SynthThread *t, int32_t voice, int32_t seq)
{
    (void)seq;
    stw_processRemaining(t);
    es_paramFromEngine(ST_STATE(t), ECI_PARAM_VOICE, voice);
    stw_createAudioConverter(t, ST_FORMAT(t));

    if (ST_CONCAT(t)) {
        if (cm_voiceIsConcatenative(ST_CONCAT(t), voice)) {
            if (!ST_TOLD_CAT(t)) {
                if (APP_LISTENING(ST_APP(t)))
                    aq_postUser(ST_APP(t), APP_CONCATENATIVE, 1);
                ST_TOLD_CAT(t) = 1;
            }
            rz_setParam(ST_ROMAN(t), ROM_CONCATENATIVE, 1);
        } else {
            rz_setParam(ST_ROMAN(t), ROM_CONCATENATIVE, 0);
            if (ST_TOLD_CAT(t) == 1) {
                if (APP_LISTENING(ST_APP(t)))
                    aq_postUser(ST_APP(t), APP_CONCATENATIVE, 0);
                ST_TOLD_CAT(t) = 0;
            }
        }
        rz_setParam(ST_ROMAN(t), ROM_VOICE, voice);
        rz_setParam(ST_ROMAN(t), ROM_SAMPLE_RATE,
                     (int32_t)cm_getActiveSampleRate(ST_CONCAT(t)));
    }
    finished(t);
}

/* Most romanizer parameters go straight through. The one that does not is
   word marks, which the engine and the concatenative side both have to be
   told about separately, and only when this build reports them at all. */
THIS void changeRomParamRun(SynthThread *t, int32_t param, int32_t value,
                            int32_t seq)
{
    (void)seq;

    if (param == ECI_PARAM_ROM && (ST_FLAGS(t) & STF_WORD_MARKS)) {
        EngWordCallback setCallback;
        IndexCallback cb = value == 1 ? stb_staticWordIndexCallback : 0;

        if (ST_CONCAT(t) &&
            cm_engineSupports(ST_CONCAT(t), (ST_ENGINE_ID(t) >> 16) & 0xff,
                               ST_ENGINE_ID(t) & 0xff))
            es_paramFromEngine(ST_STATE(t), ECI_PARAM_ROM, value);

        setCallback = (EngWordCallback)ENG_CALL(t, ENG_WORD_CALLBACK);
        setCallback(ST_ENGINE(t), cb, t);
        if (cm_usingConcatenativeEngine(ST_CONCAT(t)))
            cm_registerCallbackA(ST_CONCAT(t), CAT_CALLBACK_WORD, cb, t);
    }

    if (rz_setParam(ST_ROMAN(t), param, value) == -1) {
        stb_postRomanizerError(t, 0);
        return;
    }
    finished(t);
}

/* A filter is made and switched on when the value is one and switched off
   and thrown away otherwise. The engine the filter belongs to is this
   thread's own unless the flag says to take it from nowhere. */
THIS void changeFilterRun(SynthThread *t, uint32_t which, uint32_t value,
                          int32_t seq, uint8_t any_engine)
{
    (void)seq;
    stw_processRemaining(t);

    if (value == 1) {
        int32_t engine = any_engine ? 0 : (int32_t)ST_ENGINE_ID(t);

        stm_newFilter(t, engine, (int32_t)which, &ST_FILTER(t));
        if (ST_FILTER(t))
            stm_activateFilter(t, ST_FILTER(t));
    } else {
        stm_deactivateFilter(t, ST_FILTER(t));
        stm_deleteFilter(t, ST_FILTER(t));
    }
    finished(t);
}

/* ---- index marks ---- */

/* All three kinds of mark take the same two roads. When the romanizer is in
   the way the mark is queued behind the text it follows and the romanizer is
   asked to put it in; otherwise it is handed to the index manager, and from
   there either straight to the engine or, if sound has already gone out, on
   to the queue that keeps marks in step with it.

   A romanizer that will not take the mark is the one path that gives up
   without counting the message done. */
static void markRun(SynthThread *t, int32_t kind, int32_t payload)
{
    if (ST_FLAGS(t) & STF_ROMANIZING) {
        MarkQueue *q = ST_MARKS(t);
        IndexNote *n = (IndexNote *)cpp_new(sizeof(IndexNote));

        n->payload = payload;
        n->kind = kind;
        q->vt->push(q, n);
        if (!rz_insertIndex(ST_ROMAN(t))) {
            stb_postRomanizerError(t, 0);
            return;
        }
    } else {
        uint32_t id = sti_newIndex(ST_INDEXMGR(t));

        if (id) {
            Index *ix = sti_getIndex(ST_INDEXMGR(t), id);

            ix->kind = kind;
            ix->payload = payload;
            if (ST_SAMPLES(t) == 0) {
                EngInsertIndex insert;

                insert = (EngInsertIndex)ENG_CALL(t, ENG_INSERT_INDEX);
                if (insert(ST_ENGINE(t), id))
                    stb_postEngineError(t);
            } else if (!iq_addOffsetFromLast(ST_INDEXQ(t), id,
                                               ST_SAMPLES(t)
                                               - ST_LASTMARK(t))) {
                aq_postUser(ST_APP(t), APP_INDEX_LOST, 0);
            }
            /* Both roads move the watermark, including the one that just
               told the caller the mark was lost. */
            ST_LASTMARK(t) = ST_SAMPLES(t);
        } else if (APP_LISTENING(ST_APP(t))) {
            aq_postUser(ST_APP(t), APP_INDEX_LOST, 0);
        }
    }
    finished(t);
}

THIS void insertIndexRun(SynthThread *t, int32_t index, int32_t seq)
{
    (void)seq;
    markRun(t, 0, index);
}

/* The two named kinds copy the name a second time. The sender already took
   one copy so the caller's buffer could go; this one is the copy the mark
   itself keeps, because the message is freed as soon as it has run. */
THIS void insertStringIndexRun(SynthThread *t, char *name, int32_t seq)
{
    (void)seq;
    markRun(t, 4, EVV_REF(strdup(name)));
}

THIS void insertAudioIndexRun(SynthThread *t, char *name, int32_t seq)
{
    (void)seq;
    markRun(t, 5, EVV_REF(strdup(name)));
}

/* ---- the rest ---- */

/* Text handed straight to the romanizer without going near Delta. What it
   stands for is a length rather than a single message, so that is what comes
   off the count. */
THIS void addParamRun(SynthThread *t, char *text, uint32_t len, int32_t seq)
{
    void *lock;

    (void)seq;
    if (rz_addParam(ST_ROMAN(t), text, (int32_t)len) == -1) {
        stb_postRomanizerError(t, 0);
        return;
    }
    lock = ST_LOCK(t);
    sy_mutexWait(lock, -1);
    ST_PENDING(t) -= (int32_t)len;
    sy_mutexRelease(lock);
}

/* The end of a run of speech. Whatever is still half-processed is flushed
   only if this message was the last thing outstanding; either way the engine
   is wound down afterwards. */
THIS void synthesizeRun(SynthThread *t, int32_t seq)
{
    void *lock = ST_LOCK(t);
    int32_t was_last;

    sy_mutexWait(lock, -1);
    ST_PENDING(t) -= 1;
    was_last = ST_PENDING(t) == 0;
    sy_mutexRelease(lock);

    if (was_last)
        stw_processRemaining(t);
    stw_shutDown(t);
    stw_stopSynthesis(t, seq);
}

/* Someone asked the thread to stand still. It does so here, on the thread
   itself, which is what makes the queue behind it stop moving. */
THIS void blockRun(SynthThread *t)
{
    sy_semWait(ST_BLOCKER(t));
}

ALIAS("?changeEmphasisRun@SynthThread@@QAEXJ@Z", "changeEmphasisRun");
ALIAS("?changeSpeedRun@SynthThread@@QAEXJJ@Z", "changeSpeedRun");
ALIAS("?changePitchRun@SynthThread@@QAEXJJ@Z", "changePitchRun");
ALIAS("?changeFluctuationRun@SynthThread@@QAEXJJ@Z", "changeFluctuationRun");
ALIAS("?changeSpeedStringRun@SynthThread@@QAEXJJ@Z", "changeSpeedStringRun");
ALIAS("?changePitchStringRun@SynthThread@@QAEXJJ@Z", "changePitchStringRun");
ALIAS("?changeFluctuationStringRun@SynthThread@@QAEXJJ@Z",
      "changeFluctuationStringRun");
ALIAS("?changeVolumeRun@SynthThread@@QAEXJJ@Z", "changeVolumeRun");
ALIAS("?setPhonemeIndiciesRun@SynthThread@@AAEXJJ@Z",
      "setPhonemeIndiciesRun");
ALIAS("?changeVoiceRun@SynthThread@@QAEXJJ@Z", "changeVoiceRun");
ALIAS("?changeRomParamRun@SynthThread@@AAEXJJJ@Z", "changeRomParamRun");
ALIAS("?changeFilterRun@SynthThread@@QAEXKKJ_N@Z", "changeFilterRun");
ALIAS("?insertIndexRun@SynthThread@@AAEXJJ@Z", "insertIndexRun");
ALIAS("?insertStringIndexRun@SynthThread@@QAEXPADJ@Z",
      "insertStringIndexRun");
ALIAS("?insertAudioIndexRun@SynthThread@@QAEXPADJ@Z", "insertAudioIndexRun");
ALIAS("?addParamRun@SynthThread@@AAEXPADKJ@Z", "addParamRun");
ALIAS("?synthesizeRun@SynthThread@@AAEXJ@Z", "synthesizeRun");
ALIAS("?blockRun@SynthThread@@AAEXXZ", "blockRun");
