/* What the synthesis thread hands back.

   Everything below runs on the thread, called from underneath rather than
   from above: the engine and the concatenative side both take callbacks, and
   these are what they call. From here results go three ways -- out to the
   sound device, into whichever buffers the caller registered, and onto the
   application queue as messages the caller will collect later.

   Each kind of callback is registered twice over, as a plain function the
   caller's side can hold and as a method. The plain ones are the trampolines
   at the top: they take the thread as their last argument and hand
   everything else straight on.

   Two of the original's own faults are transcribed rather than mended, and
   both are marked where they are. Neither can be reached by anything short
   of the engine failing outright, and mending either would change what the
   binary does under a condition there is no way to test. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "eci_synththread.h"
#include "evv_abi.h"

/* What the application queue is being told about. These are its own
   numbers, not the ones the published interface hands the caller. */
#define APP_INDEX          0x00
#define APP_SAMPLES        0x01
#define APP_PHONEMES       0x02
#define APP_PHONEME_INDEX  0x03
#define APP_ENGINE_ERROR   0x04
#define APP_SOUND_ERROR    0x05
#define APP_INDEX_LOST     0x06
#define APP_WORD_INDEX     0x08
#define APP_ROM_ERROR      0x09
#define APP_ROM_LANG_ERROR 0x0a
#define APP_BREAK          0x0b
#define APP_CONCATENATIVE  0x0c
#define APP_STRING_INDEX   0x0d
#define APP_AUDIO_INDEX    0x0e

/* What an index mark stands for. */
#define MARK_PLAIN       0
#define MARK_PHONEME     1
#define MARK_SYNCHRONISE 2
#define MARK_WORD        3
#define MARK_STRING      4
#define MARK_AUDIO       5

#define CAT_VOLUME       0x0c
#define CAT_SPEED        0x0f
#define CAT_PITCH        0x10
#define CAT_FLUCTUATION  0x14

#define CAT_CB_WORD_START 1
#define CAT_CB_WORD_MARK  2
#define CAT_CB_PHONEME    3
#define CAT_CB_USER_INDEX 4
#define CAT_CB_BREAK      5

/* Parameters the engine reports back through the annotation callback. */
#define ANNO_VOLUME      0x0c
#define ANNO_PARAM2      0x12

/* Which parameter says whether the caller wants phonemes reported. */
#define ECI_PARAM_PHONEMES 4

/* Slots of the engine's own table, by byte offset. Every one of them is
   stdcall with the engine pushed like any other argument. */
#define ENG_FETCH_PHONEMES 0x24
#define ENG_INSERT_INDEX   0x5c
#define ENG_PHONEME_INDEX  0x60
#define ENG_FETCH_SPR      0xa4

#define ENGCALL __attribute__((stdcall))
#define ENG_CALL(t, off) \
    (((void **)*(void ***)ST_ENGINE(t))[(off) / 4])

typedef ENGCALL int32_t (*EngFetchPhonemes)(void *engine, char *out,
                                            int32_t room, int32_t *got);
typedef ENGCALL int32_t (*EngInsertIndex)(void *engine, uint32_t id);
typedef ENGCALL int32_t (*EngPhonemeIndex)(void *engine, uint32_t id,
                                           uint32_t k);
typedef ENGCALL int32_t (*EngFetchSPR)(void *engine, char *out, int32_t room,
                                       int32_t *got);

typedef void (*IndexCallback)(int32_t, void *);
typedef void (*UserCallback)(void *);
typedef void (*SynthCallback)(int32_t, int32_t *, void *);

/* A run of samples on its way through the converter: where it starts and how
   many bytes of it there are. It goes across by value. */
typedef struct { void *at; uint32_t bytes; } SDATA;

/* The index queue is one of the library's collection classes, and they all
   put isEmpty first in their table rather than a destructor. Nothing else in
   that table is reached from here. */
typedef struct Collection Collection;
typedef struct {
    THIS int32_t (*isEmpty)(const Collection *self);
} CollectionVtbl;
struct Collection { const CollectionVtbl *vt; };

static int stb_queueIsEmpty(void *q)
{
    Collection *c = (Collection *)q;

    return c->vt->isEmpty(c);
}

extern THIS int32_t pcm_cvt_convert(void *c, SDATA in, SDATA **out)
    MANGLED("?convertSamples@AudioConverter@@QAEJUSDATA@@PAPAU2@@Z");
extern THIS void pcm_cvt_storeHistory(void *c)
    MANGLED("?storeHistory@AudioConverter@@QAEXXZ");

extern THIS int32_t snd_write(void *s, const int32_t *samples, uint32_t n)
    MANGLED("?write@SoundThread@@QAEHPBJI@Z");
extern THIS int32_t snd_insertIndex(void *s, int32_t id)
    MANGLED("?insertIndex@SoundThread@@QAEHJ@Z");
extern THIS int32_t snd_close(void *s)
    MANGLED("?close@SoundThread@@QAEHXZ");

extern THIS int32_t aq_postUser(void *a, int32_t what, int32_t value)
    MANGLED("?postUser@ETIappMessageQueue@@QAEHJJ@Z");
extern THIS int32_t aq_sendUser(void *a, int32_t what, int32_t value)
    MANGLED("?sendUser@ETIappMessageQueue@@QAEHJJ@Z");

extern THIS int32_t sy_eventSignal(void *e)
    MANGLED("?signal@ETIEvent@@QAEHXZ");

extern THIS uint32_t sti_newIndex(void *m)
    MANGLED("?newIndex@IndexManager@@QAEKXZ");
extern THIS Index *sti_getIndex(void *m, uint32_t id)
    MANGLED("?getIndex@IndexManager@@QAEPAUIndex@@K@Z");
extern THIS void sti_deleteIndex(void *m, uint32_t id)
    MANGLED("?deleteIndex@IndexManager@@QAEXK@Z");

extern THIS uint32_t iq_reduceLeadTime(void *q, uint32_t n)
    MANGLED("?reduceLeadTime@IndexQueue@@QAEKK@Z");
extern THIS int32_t sti_indexDue(const void *q)
    MANGLED("?indexDue@IndexQueue@@QBEHXZ");
extern THIS uint32_t iq_remove(void *q)
    MANGLED("?remove@IndexQueue@@QAEHXZ");

extern THIS int32_t cm_usingConcatenativeEngine(void *c)
    MANGLED("?usingConcatenativeEngine@ConcatenationManager@@QAEHXZ");
extern THIS int32_t cm_voiceIsConcatenative(void *c, int32_t voice)
    MANGLED("?voiceIsConcatenative@ConcatenationManager@@QAEHH@Z");
extern THIS int32_t cm_setTorrentParam1(void *c, uint32_t which, int32_t value)
    MANGLED("?setTorrentParam1@ConcatenationManager@@QAEHKJ@Z");
extern THIS int32_t cm_setTorrentParam2(void *c, uint32_t which, int32_t value)
    MANGLED("?setTorrentParam2@ConcatenationManager@@QAEHKJ@Z");
extern THIS void cm_bufferSPR(void *c, const char *text, int32_t len)
    MANGLED("?bufferSPR@ConcatenationManager@@QAEXPBDH@Z");
extern THIS void cm_registerCallbackA(void *c, uint32_t which,
                                           IndexCallback cb, void *param)
    MANGLED("?registerCallback@ConcatenationManager@@QAEXKP6AXHPAX@Z0@Z");
extern THIS void cm_registerCallbackB(void *c, uint32_t which,
                                          UserCallback cb, void *param)
    MANGLED("?registerCallback@ConcatenationManager@@QAEXKP6AXPAX@Z0@Z");
extern THIS void cm_registerCallbackC(void *c, SynthCallback cb,
                                           void *param)
    MANGLED("?registerCallback@ConcatenationManager@@QAEXP6AXHPAJPAX@Z1@Z");

extern THIS void es_paramFromEngine(void *s, int32_t which, int32_t value)
    MANGLED("?paramFromEngine@ECIstate@@QAEXJJ@Z");
extern THIS int32_t es_getParam(void *s, int32_t k, int32_t p, int32_t *out)
    MANGLED("?getParam@ECIstate@@QAEJJJPAJ@Z");

extern THIS int32_t stw_isOldEngine(SynthThread *t)
    MANGLED("?isOldEngine@SynthThread@@QAEHXZ");
extern THIS void stw_removeCharsFromEngine(SynthThread *t, int32_t n)
    MANGLED("?removeCharsFromEngine@SynthThread@@QAEXH@Z");

/* Every phoneme the engine can name, kept once for the whole library rather
   than once per thread. */
extern void *st_phonemes MANGLED("?m_phonemes@SynthThread@@0PAVPhonemes@@A");
extern THIS int32_t ph_getPhoneme(void *p, LangIdentifier *l, int32_t which)
    MANGLED("?getPhoneme@Phonemes@@QAEJPAVLangIdentifier@@J@Z");

/* Declared here because several of these call each other, and because the
   trampolines at the foot need them. */
THIS void stb_postEngineError(SynthThread *t);
THIS void stb_postRomanizerError(SynthThread *t, int32_t language);
THIS void stb_postSoundError(SynthThread *t);
THIS void stb_postIndexToUser(SynthThread *t, int32_t id);
THIS void stb_sendPhonemesToUser(SynthThread *t);
THIS void stb_sendRemainingPhonemesToUser(SynthThread *t);
THIS void stb_sendRemainingSamplesToUser(SynthThread *t);
THIS void stb_torrentPhonemeCallback(SynthThread *t, int32_t phoneme);

void stb_staticSynthCallback(int32_t count, int32_t *samples, void *self);
void stb_staticTorrentPhonemeCallback(int32_t phoneme, void *self);
void stb_staticUserIndexCallback(void *self);
void stb_staticWordCallback(int32_t n, void *self);
void stb_staticWordIndexCallback(int32_t where, void *self);
void stb_staticSynthesisBreakCallback(int32_t where, void *self);

/* Is there anyone listening? Nothing is posted when the caller registered no
   callback, because there would be nobody to take it off the queue. */
#define LISTENING(t) (APP_LISTENING(ST_APP(t)) != 0)

/* ---- telling the caller something went wrong ---- */

/* Each of the three is reported once and then held quiet, because a failing
   engine fails on every sample and the caller wants to be told, not
   buried. */
THIS void stb_postEngineError(SynthThread *t)
{
    if (ST_ENGERR(t))
        return;
    if (!LISTENING(t))
        return;
    aq_postUser(ST_APP(t), APP_ENGINE_ERROR, 0);
    ST_ENGERR(t) = 1;
}

THIS void stb_postRomanizerError(SynthThread *t, int32_t language)
{
    if (ST_ROMERR(t))
        return;
    if (!LISTENING(t))
        return;
    aq_postUser(ST_APP(t),
                 language ? APP_ROM_LANG_ERROR : APP_ROM_ERROR, 0);
    ST_ROMERR(t) = 1;
}

/* The sound one is different: it also shuts the device down and stops the
   thread ever going near it again, which is why it needs the lock. */
THIS void stb_postSoundError(SynthThread *t)
{
    void *lock = ST_LOCK(t);

    sy_mutexWait(lock, -1);
    if (LISTENING(t))
        aq_postUser(ST_APP(t), APP_SOUND_ERROR, 0);
    snd_close(ST_SOUND(t));
    ST_SILENT(t) = 1;
    sy_mutexRelease(lock);
}

/* ---- filling the caller's buffers ---- */

/* Samples go into the caller's buffer a word at a time, and the buffer is
   handed over whenever it is exactly full. The engine works in long words
   and the caller wants shorts, so only the low half of each goes across. */
THIS void stb_sendSamplesToUser(SynthThread *t, int32_t count,
                            const int32_t *samples)
{
    int32_t i;

    for (i = 0; i < count; i++) {
        ST_SAMPBUF(t)[ST_SAMPHELD(t)] = (int16_t)samples[i];
        ST_SAMPHELD(t) += 1;
        if (ST_SAMPHELD(t) * 2 == ST_SAMPROOM(t)) {
            if (LISTENING(t))
                aq_sendUser(ST_APP(t), APP_SAMPLES, ST_SAMPHELD(t) * 2);
            ST_SAMPHELD(t) = 0;
        }
    }
}

/* And at the end of a run, whatever is in the buffer goes across even though
   it is short of full. */
THIS void stb_sendRemainingSamplesToUser(SynthThread *t)
{
    if (ST_SAMPHELD(t) <= 0)
        return;
    if (LISTENING(t))
        aq_sendUser(ST_APP(t), APP_SAMPLES, ST_SAMPHELD(t) * 2);
    ST_SAMPHELD(t) = 0;
}

/* Copy what the engine gave us into the caller's phoneme buffer. An engine
   that names its phonemes in wide characters gets each byte widened; the
   rest get a plain string copy. */
static void stb_copyPhonemes(SynthThread *t, int wide)
{
    if (wide) {
        int32_t i;

        memset(ST_PHONBUF(t), 0, (size_t)ST_ENGPHONROOM(t) + 2);
        for (i = 0; i < ST_PHONHELD(t); i++)
            ((uint16_t *)ST_PHONBUF(t))[i] = (uint8_t)ST_ENGPHON(t)[i];
    } else {
        memset(ST_PHONBUF(t), 0, (size_t)ST_ENGPHONROOM(t) + 1);
        strncpy((char *)ST_PHONBUF(t), ST_ENGPHON(t),
                (size_t)ST_ENGPHONROOM(t));
    }
}

/* Pull phonemes out of the engine and hand them over a bufferful at a time,
   until it has no more to give.

   The loop only ends when a fetch comes back short of a full buffer. A fetch
   that fails outright leaves it spinning, because the flag that ends the
   loop is never set on that road. That is the original's and it is left
   alone: it needs the engine to fail, and it would take a change in
   behaviour to mend. */
THIS void stb_sendPhonemesToUser(SynthThread *t)
{
    int done = 0;
    int wide = (ST_ENGINE_ID(t) & LANG_WIDE_PHONEMES) != 0;
    int32_t want;

    /* A wide buffer holds whole characters, so an odd byte at the end of it
       is no use to anybody. */
    if (wide && ST_ENGPHONROOM(t) % 2 != 0)
        ST_ENGPHONROOM(t) -= 1;
    want = ST_ENGPHONROOM(t);

    while (!done) {
        EngFetchPhonemes fetch =
            (EngFetchPhonemes)ENG_CALL(t, ENG_FETCH_PHONEMES);
        int32_t got = 0;

        if (wide)
            want = ST_ENGPHONROOM(t) / 2;

        if (fetch(ST_ENGINE(t), ST_ENGPHON(t) + ST_PHONHELD(t),
                  want - ST_PHONHELD(t), &got)) {
            stb_postEngineError(t);
            continue;
        }
        ST_PHONHELD(t) += got;
        if (ST_PHONHELD(t) != want) {
            done = 1;
            continue;
        }
        if (LISTENING(t)) {
            stb_copyPhonemes(t, wide);
            aq_sendUser(ST_APP(t), APP_PHONEMES, ST_PHONHELD(t));
        }
        ST_PHONHELD(t) = 0;
    }
}

/* And the same at the end of a run, for the part-full buffer. */
THIS void stb_sendRemainingPhonemesToUser(SynthThread *t)
{
    stb_sendPhonemesToUser(t);
    if (ST_PHONHELD(t) <= 0)
        return;
    if (LISTENING(t)) {
        stb_copyPhonemes(t, (ST_ENGINE_ID(t) & LANG_WIDE_PHONEMES) != 0);
        aq_sendUser(ST_APP(t), APP_PHONEMES, ST_PHONHELD(t));
    }
    ST_PHONHELD(t) = 0;
}

/* ---- index marks coming back out ---- */

/* A mark the engine has now reached. Which message the caller gets depends
   on what kind of mark it was; one kind is not the caller's business at all
   and only wakes whoever is waiting on the thread. */
THIS void stb_postIndexToUser(SynthThread *t, int32_t id)
{
    Index *ix;

    if (!id)
        return;
    ix = sti_getIndex(ST_INDEXMGR(t), (uint32_t)id);
    switch (ix->kind) {
    case MARK_PLAIN:
        if (LISTENING(t))
            aq_postUser(ST_APP(t), APP_INDEX, ix->payload);
        break;

    case MARK_SYNCHRONISE:
        sy_eventSignal(ST_SYNCED(t));
        break;

    case MARK_STRING:
        if (LISTENING(t))
            aq_postUser(ST_APP(t), APP_STRING_INDEX, ix->payload);
        break;

    case MARK_AUDIO:
        if (LISTENING(t))
            aq_postUser(ST_APP(t), APP_AUDIO_INDEX, ix->payload);
        break;

    case MARK_WORD:
        if (LISTENING(t))
            aq_postUser(ST_APP(t), APP_WORD_INDEX, ix->payload);
        break;

    case MARK_PHONEME:
        /* The mark carries the engine's own number for the phoneme; what
           goes to the caller is the name it goes under. */
        ix->payload = ph_getPhoneme(st_phonemes,
                                    &t->lang,
                                    ix->payload);
        if (ix->payload && LISTENING(t))
            aq_postUser(ST_APP(t), APP_PHONEME_INDEX, ix->payload);
        break;

    default:
        break;
    }
    sti_deleteIndex(ST_INDEXMGR(t), (uint32_t)id);
}

/* ---- what the engine and the concatenative side call ---- */

/* Samples. Everything that ever gets heard comes through here.

   The converter is skipped when the concatenative side says its own output
   needs no converting. What comes out goes to the device, to the caller's
   buffer, or both, and the converter is told to remember the tail of it so
   the next run joins on smoothly. */
THIS void stb_synthCallback(SynthThread *t, int32_t count, int32_t *samples)
{
    int convert = 1;

    if (ST_CONCAT(t)) {
        uint8_t raw = *(uint8_t *)((char *)ST_CONCAT(t) + 0x2ac);

        if (!raw)
            convert = 0;
    }

    if (convert && ST_CONVERTER(t)) {
        SDATA in;
        SDATA *out;

        in.at = samples;
        in.bytes = (uint32_t)count << 2;
        if (pcm_cvt_convert(ST_CONVERTER(t), in, &out) == 0) {
            samples = (int32_t *)out->at;
            count = (int32_t)(out->bytes >> 2);
        }
    }

    if (!ST_SILENT(t) && ST_SOUND(t)) {
        if (!snd_write(ST_SOUND(t), samples, (uint32_t)count))
            stb_postSoundError(t);
    }
    if (ST_SAMPBUF(t))
        stb_sendSamplesToUser(t, count, samples);
    if (ST_CONVERTER(t))
        pcm_cvt_storeHistory(ST_CONVERTER(t));
}

/* The engine has reached a mark. Where it goes depends on whether there is a
   device in the way: with one, the mark is queued behind the sound so it
   comes out in time with it, and without one it is reported at once. */
THIS void stb_synthIndexCallback(SynthThread *t, int32_t id)
{
    if (!ST_SILENT(t) && ST_SOUND(t)) {
        if (!snd_insertIndex(ST_SOUND(t), id))
            stb_postSoundError(t);
        return;
    }
    if (ST_SAMPBUF(t))
        stb_sendRemainingSamplesToUser(t);
    if (ST_PHONBUF(t))
        stb_sendRemainingPhonemesToUser(t);
    stb_postIndexToUser(t, id);
}

/* The device has reached a mark. Nothing else to do but report it. */
THIS void stb_deviceIndexCallback(SynthThread *t, int32_t id)
{
    stb_postIndexToUser(t, id);
}

/* A phoneme, from an engine old enough to report them one at a time. A newer
   one goes the concatenative way instead. */
THIS void stb_phonemeCallback(SynthThread *t, int32_t phoneme, uint32_t k)
{
    uint32_t id;

    if (!stw_isOldEngine(t)) {
        stb_torrentPhonemeCallback(t, phoneme);
        return;
    }
    id = sti_newIndex(ST_INDEXMGR(t));
    if (id) {
        Index *ix = sti_getIndex(ST_INDEXMGR(t), id);
        EngPhonemeIndex mark =
            (EngPhonemeIndex)ENG_CALL(t, ENG_PHONEME_INDEX);

        ix->kind = MARK_PHONEME;
        ix->payload = phoneme;
        if (mark(ST_ENGINE(t), id, k))
            stb_postEngineError(t);
    } else if (LISTENING(t)) {
        aq_postUser(ST_APP(t), APP_INDEX_LOST, 0);
    }
}

/* And a phoneme from the concatenative side, which reports them as they are
   spoken rather than as they are marked. Nothing is done unless the caller
   asked to hear about phonemes at all. */
THIS void stb_torrentPhonemeCallback(SynthThread *t, int32_t phoneme)
{
    int32_t wanted = 0;
    int32_t name;

    es_getParam(ST_STATE(t), 0, ECI_PARAM_PHONEMES, &wanted);
    if (!wanted)
        return;

    name = ph_getPhoneme(st_phonemes, &t->lang,
                         phoneme);
    if (!phoneme)
        return;
    if (!LISTENING(t))
        return;
    if (!name)
        return;

    aq_postUser(ST_APP(t), APP_PHONEME_INDEX, name);
    if (ST_SAMPBUF(t))
        stb_sendRemainingSamplesToUser(t);
    if (ST_PHONBUF(t))
        stb_sendRemainingPhonemesToUser(t);
}

/* The start of a word, counted in samples still to be spoken. The counts of
   what has been made and where the last mark went are wound back by it, and
   every mark whose time has now come is handed to the engine. */
THIS void stb_wordCallback(SynthThread *t, int32_t n)
{
    int done = 0;

    if (n > ST_SAMPLES(t))
        n = ST_SAMPLES(t);
    ST_SAMPLES(t) -= n;
    ST_LASTMARK(t) -= n;
    if (ST_LASTMARK(t) < 0)
        ST_LASTMARK(t) = 0;

    while (!stb_queueIsEmpty(ST_INDEXQ(t)) && !done) {
        n -= (int32_t)iq_reduceLeadTime(ST_INDEXQ(t), (uint32_t)n);
        if (sti_indexDue(ST_INDEXQ(t))) {
            uint32_t id = iq_remove(ST_INDEXQ(t));
            EngInsertIndex insert =
                (EngInsertIndex)ENG_CALL(t, ENG_INSERT_INDEX);

            if (insert(ST_ENGINE(t), id))
                stb_postEngineError(t);
        } else {
            done = n == 0;
        }
    }
}

/* A word mark the engine itself will report back when it reaches it. */
THIS void stb_wordIndexCallback(SynthThread *t, int32_t where)
{
    uint32_t id = sti_newIndex(ST_INDEXMGR(t));

    if (id) {
        Index *ix = sti_getIndex(ST_INDEXMGR(t), id);
        EngInsertIndex insert = (EngInsertIndex)ENG_CALL(t, ENG_INSERT_INDEX);

        ix->kind = MARK_WORD;
        ix->payload = where;
        if (insert(ST_ENGINE(t), id))
            stb_postEngineError(t);
    }
}

/* A mark the romanizer put in earlier and the concatenative side has now
   reached. The note it left is taken off the queue and turned into a real
   index for the engine.

   A queue that has nothing to give leaves the note pointer where it was, and
   the original writes through it regardless. It is left as it stands. */
THIS void stb_userIndexCallback(SynthThread *t)
{
    MarkQueue *q = ST_MARKS(t);
    IndexNote *note = 0;
    uint32_t id;

    if (q->vt->pop(q, (void **)&note) == 0) {
        note->payload = 0;
        note->kind = 0;
    }

    id = sti_newIndex(ST_INDEXMGR(t));
    if (id) {
        Index *ix = sti_getIndex(ST_INDEXMGR(t), id);
        EngInsertIndex insert = (EngInsertIndex)ENG_CALL(t, ENG_INSERT_INDEX);

        ix->kind = note->kind;
        ix->payload = note->payload;
        if (insert(ST_ENGINE(t), id))
            stb_postEngineError(t);
    }
    if (note)
        cpp_delete(note);
}

/* The engine reached the end of a piece of speech. */
THIS void stb_synthesisBreakCallback(SynthThread *t, int32_t where)
{
    if (!LISTENING(t))
        return;
    if (!ST_SAMPBUF(t))
        return;
    stb_sendRemainingSamplesToUser(t);
    aq_postUser(ST_APP(t), APP_BREAK, where);
}

/* The engine met an annotation in the text and acted on it. The state block
   is told so the caller can ask what the setting is, and two of them have to
   be passed on to the concatenative side as well. */
THIS void stb_annoCallback(SynthThread *t, int32_t param, int32_t value)
{
    es_paramFromEngine(ST_STATE(t), param, value);
    switch (param) {
    case ANNO_VOLUME:
        if (ST_CONCAT(t) && cm_usingConcatenativeEngine(ST_CONCAT(t)))
            cm_setTorrentParam1(ST_CONCAT(t), (uint32_t)param, (value << 10) / 100);
        break;
    case ANNO_PARAM2:
        if (ST_CONCAT(t) && cm_usingConcatenativeEngine(ST_CONCAT(t)))
            cm_setTorrentParam2(ST_CONCAT(t), (uint32_t)param, value);
        break;
    default:
        break;
    }
}

/* The engine has a line of its own reporting language for us. Where it goes
   depends on who started the engine talking: something we asked for
   ourselves is read here, and anything else is handed to the concatenative
   side to buffer.

   Reading it ourselves means looking for a count of characters the engine
   has finished with, so they can be taken off what is still outstanding. */
THIS void stb_enhancedSPRCallback(SynthThread *t)
{
    char line[0x6c];
    int32_t got = 0;

    if (!ST_ENGINE(t))
        return;

    do {
        EngFetchSPR fetch = (EngFetchSPR)ENG_CALL(t, ENG_FETCH_SPR);

        if (fetch(ST_ENGINE(t), line, 0x64, &got)) {
            stb_postEngineError(t);
        } else if (!ST_DIRECT(t)) {
            cm_bufferSPR(ST_CONCAT(t), line, got);
            break;
        } else {
            int32_t i;

            for (i = 0; i < got; i++) {
                uint32_t n;
                int rest;

                if (line[i] != 'c')
                    continue;
                if (sscanf(line + i + 1, "%i%n", &n, &rest) != 1)
                    continue;
                if (n > (uint32_t)ST_SAMPLES(t))
                    continue;
                stw_removeCharsFromEngine(t, (int32_t)n);
            }
        }
    } while (got != 0);
}

/* The caller asked for a different voice. If the concatenative side has that
   voice it takes over, and the four settings it starts the voice on are
   handed back so the layer above knows where it stands. */
THIS void stb_voiceChangeCallback(SynthThread *t, int32_t voice, int16_t *ok,
                              int16_t *pitch, int16_t *fluctuation,
                              int16_t *speed, int16_t *volume)
{
    if (!ST_CONCAT(t))
        return;
    if (!cm_voiceIsConcatenative(ST_CONCAT(t), voice)) {
        *ok = 0;
        return;
    }

    cm_registerCallbackC(ST_CONCAT(t), stb_staticSynthCallback, t);
    cm_registerCallbackA(ST_CONCAT(t), CAT_CB_PHONEME,
                              stb_staticTorrentPhonemeCallback, t);
    if (ST_FLAGS(t) & STF_ROMANIZING)
        cm_registerCallbackB(ST_CONCAT(t), CAT_CB_USER_INDEX,
                                 stb_staticUserIndexCallback, t);
    else if (ST_FLAGS(t) & STF_WORD_STARTS)
        cm_registerCallbackA(ST_CONCAT(t), CAT_CB_WORD_START,
                                  stb_staticWordCallback, t);
    if (ST_FLAGS(t) & STF_WORD_MARKS)
        cm_registerCallbackA(ST_CONCAT(t), CAT_CB_WORD_MARK,
                                  stb_staticWordIndexCallback, t);
    cm_registerCallbackA(ST_CONCAT(t), CAT_CB_BREAK,
                              stb_staticSynthesisBreakCallback, t);

    *ok = 1;
    *pitch = *(int16_t *)((char *)ST_CONCAT(t) + 0x2b4);
    *fluctuation = *(int16_t *)((char *)ST_CONCAT(t) + 0x2b8);
    *speed = *(int16_t *)((char *)ST_CONCAT(t) + 0x2b0);
    *volume = *(int16_t *)((char *)ST_CONCAT(t) + 0x2bc);

    cm_setTorrentParam1(ST_CONCAT(t), CAT_SPEED, *speed);
    cm_setTorrentParam1(ST_CONCAT(t), CAT_PITCH, *pitch);
    cm_setTorrentParam1(ST_CONCAT(t), CAT_FLUCTUATION, *fluctuation);
    cm_setTorrentParam1(ST_CONCAT(t), CAT_VOLUME, (*volume << 10) / 100);
}

/* Text the concatenative side should keep for later. */
THIS void stb_sendEnhancedSPRToTorrentManager(SynthThread *t, char *text,
                                          int32_t len)
{
    if (ST_CONCAT(t))
        cm_bufferSPR(ST_CONCAT(t), text, len);
}

/* ---- the plain functions the other side holds ----

   Every one of these takes the thread as its last argument, because that is
   the only thing a C callback can be given to say which instance it belongs
   to. They do nothing but hand over. */

void stb_staticSynthCallback(int32_t count, int32_t *samples, void *self)
{
    stb_synthCallback((SynthThread *)self, count, samples);
}

void stb_staticSynthIndexCallback(int32_t id, void *self)
{
    stb_synthIndexCallback((SynthThread *)self, id);
}

void stb_staticDeviceIndexCallback(int32_t id, void *self)
{
    stb_deviceIndexCallback((SynthThread *)self, id);
}

void stb_staticPhonemeCallback(int32_t phoneme, uint32_t k, void *self)
{
    stb_phonemeCallback((SynthThread *)self, phoneme, k);
}

void stb_staticTorrentPhonemeCallback(int32_t phoneme, void *self)
{
    stb_torrentPhonemeCallback((SynthThread *)self, phoneme);
}

void stb_staticWordCallback(int32_t n, void *self)
{
    stb_wordCallback((SynthThread *)self, n);
}

void stb_staticWordIndexCallback(int32_t where, void *self)
{
    stb_wordIndexCallback((SynthThread *)self, where);
}

void stb_staticUserIndexCallback(void *self)
{
    stb_userIndexCallback((SynthThread *)self);
}

void stb_staticSynthesisBreakCallback(int32_t where, void *self)
{
    stb_synthesisBreakCallback((SynthThread *)self, where);
}

void stb_staticAnnoCallback(int32_t param, int32_t value, void *self)
{
    stb_annoCallback((SynthThread *)self, param, value);
}

void stb_staticEnhancedSPRCallback(void *self)
{
    stb_enhancedSPRCallback((SynthThread *)self);
}

void stb_staticVoiceChangeCallback(int32_t voice, int16_t *ok, int16_t *pitch,
                               int16_t *fluctuation, int16_t *speed,
                               int16_t *volume, void *self)
{
    stb_voiceChangeCallback((SynthThread *)self, voice, ok, pitch, fluctuation,
                        speed, volume);
}

ALIAS("?postEngineError@SynthThread@@AAEXXZ", "stb_postEngineError");
ALIAS("?postRomanizerError@SynthThread@@AAEXH@Z", "stb_postRomanizerError");
ALIAS("?postSoundError@SynthThread@@AAEXXZ", "stb_postSoundError");
ALIAS("?sendSamplesToUser@SynthThread@@AAEXHPAJ@Z", "stb_sendSamplesToUser");
ALIAS("?sendRemainingSamplesToUser@SynthThread@@AAEXXZ",
      "stb_sendRemainingSamplesToUser");
ALIAS("?sendPhonemesToUser@SynthThread@@AAEXXZ", "stb_sendPhonemesToUser");
ALIAS("?sendRemainingPhonemesToUser@SynthThread@@AAEXXZ",
      "stb_sendRemainingPhonemesToUser");
ALIAS("?postIndexToUser@SynthThread@@AAEXH@Z", "stb_postIndexToUser");
ALIAS("?sendEnhancedSPRToTorrentManager@SynthThread@@AAEXPADH@Z",
      "stb_sendEnhancedSPRToTorrentManager");

ALIAS("?synthCallback@SynthThread@@IAEXHPAJ@Z", "stb_synthCallback");
ALIAS("?synthIndexCallback@SynthThread@@IAEXH@Z", "stb_synthIndexCallback");
ALIAS("?deviceIndexCallback@SynthThread@@IAEXH@Z", "stb_deviceIndexCallback");
ALIAS("?phonemeCallback@SynthThread@@IAEXHK@Z", "stb_phonemeCallback");
ALIAS("?torrentPhonemeCallback@SynthThread@@IAEXH@Z",
      "stb_torrentPhonemeCallback");
ALIAS("?wordCallback@SynthThread@@IAEXH@Z", "stb_wordCallback");
ALIAS("?wordIndexCallback@SynthThread@@IAEXH@Z", "stb_wordIndexCallback");
ALIAS("?userIndexCallback@SynthThread@@IAEXXZ", "stb_userIndexCallback");
ALIAS("?synthesisBreakCallback@SynthThread@@IAEXH@Z",
      "stb_synthesisBreakCallback");
ALIAS("?annoCallback@SynthThread@@IAEXJJ@Z", "stb_annoCallback");
ALIAS("?enhancedSPRCallback@SynthThread@@IAEXXZ", "stb_enhancedSPRCallback");
ALIAS("?voiceChangeCallback@SynthThread@@IAEXJPAF0000@Z",
      "stb_voiceChangeCallback");

ALIAS("?staticSynthCallback@SynthThread@@CAXHPAJPAX@Z", "stb_staticSynthCallback");
ALIAS("?staticSynthIndexCallback@SynthThread@@CAXHPAX@Z",
      "stb_staticSynthIndexCallback");
ALIAS("?staticDeviceIndexCallback@SynthThread@@CAXHPAX@Z",
      "stb_staticDeviceIndexCallback");
ALIAS("?staticPhonemeCallback@SynthThread@@CAXHKPAX@Z",
      "stb_staticPhonemeCallback");
ALIAS("?staticTorrentPhonemeCallback@SynthThread@@CAXHPAX@Z",
      "stb_staticTorrentPhonemeCallback");
ALIAS("?staticWordCallback@SynthThread@@CAXHPAX@Z", "stb_staticWordCallback");
ALIAS("?staticWordIndexCallback@SynthThread@@CAXHPAX@Z",
      "stb_staticWordIndexCallback");
ALIAS("?staticUserIndexCallback@SynthThread@@CAXPAX@Z",
      "stb_staticUserIndexCallback");
ALIAS("?staticSynthesisBreakCallback@SynthThread@@CAXHPAX@Z",
      "stb_staticSynthesisBreakCallback");
ALIAS("?staticAnnoCallback@SynthThread@@CAXJJPAX@Z", "stb_staticAnnoCallback");
ALIAS("?staticEnhancedSPRCallback@SynthThread@@CAXPAX@Z",
      "stb_staticEnhancedSPRCallback");
ALIAS("?staticVoiceChangeCallback@SynthThread@@CAXJPAF0000PAX@Z",
      "stb_staticVoiceChangeCallback");
