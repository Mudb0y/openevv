/* The synthesis thread's own working parts.

   Neither the message side nor the callbacks: the pieces both of those lean
   on. Handing text to the engine, winding the counts back when the engine
   says it has finished with some, flushing what is left at the end of a run,
   shutting the device down, settling on a sample rate, and the short
   accessors the layer above asks its questions through.

   Most of these are on the path of every spoken sentence, which is what
   makes them worth taking before the rest.

   Everything here is prefixed rather than named after the method it becomes,
   because a plain C name has to be unique across the whole link and several
   of these words are used elsewhere for other things entirely. The names the
   engine reaches them by are on the alias lines at the foot. */

#include <stdint.h>
#include <string.h>
#include "eci_synththread.h"
#include "evv_abi.h"

#define APP_INDEX_LOST     0x06
#define APP_SPEAKING_DONE  0x07

#define MARK_SYNCHRONISE   2

/* What the application queue answers when it has caught up. */
#define QUEUE_CAUGHT_UP    4

/* Answers this layer gives back. */
#define OK              0
#define ERR_FAILED     (-2)
#define ERR_BAD_RATE   (-4)
#define ERR_NO_SOUND   (-16)
#define ERR_REFUSED    (-9)

/* Sample rates, as the format record spells them. */
#define RATE_8000   0x1f40
#define RATE_11025  0x2b11
#define RATE_16000  0x3e80

/* One line of the engine's own command language for each rate it can be put
   into. */
static const char CMD_RATE_8000[] = "`esr0";
static const char CMD_RATE_11025[] = "`esr1";
static const char CMD_RATE_OTHER[] = "`esr2";

/* Slots of the engine's table, by byte offset. All stdcall, all with the
   engine pushed like any other argument. */
#define ENG_ASK            0x00
#define ENG_ADD_TEXT       0x14
#define ENG_COMMAND        0x18
#define ENG_RESET          0x2c
#define ENG_START          0x30
#define ENG_SYNTH_CB       0x44
#define ENG_WORD_START_CB  0x4c
#define ENG_SYNTH_INDEX_CB 0x50
#define ENG_PHONEME_CB     0x54
#define ENG_ANNO_CB        0x58
#define ENG_INSERT_INDEX   0x5c
#define ENG_WORD_CB        0x94
#define ENG_USER_INDEX_CB  0x98
#define ENG_SPR_CB         0x9c
#define ENG_VOICE_CB       0xa0

#define ENGCALL __attribute__((stdcall))
#define ENG_CALL(t, off) \
    (((void **)*(void ***)ST_ENGINE(t))[(off) / 4])
/* The engine's table of virtual functions, by the byte a slot sat at in
   the original. A slot is a pointer, not four bytes, so the number is
   divided rather than added. */
#define ENG_CALL_ON(e, off) (((void **)*(void ***)(e))[(off) / 4])

typedef void (*IndexCallback)(int32_t, void *);
typedef void (*UserCallback)(void *);
typedef void (*SynthCallback)(int32_t, int32_t *, void *);
typedef void (*PhonemeCallback)(int32_t, uint32_t, void *);
typedef void (*AnnoCallback)(int32_t, int32_t, void *);
typedef void (*VoiceCallback)(int32_t, int16_t *, int16_t *, int16_t *,
                              int16_t *, int16_t *, void *);

typedef ENGCALL int32_t (*EngAsk)(void *engine, int32_t what, void **out);
typedef ENGCALL int32_t (*EngAddText)(void *engine, const char *text);
typedef ENGCALL int32_t (*EngCommand)(void *engine, const char *line);
typedef ENGCALL int32_t (*EngReset)(void *engine, int32_t how);
typedef ENGCALL int32_t (*EngStart)(void *engine);
typedef ENGCALL int32_t (*EngInsertIndex)(void *engine, uint32_t id);
typedef ENGCALL int32_t (*EngSetSynth)(void *engine, SynthCallback cb,
                                       void *param);
typedef ENGCALL int32_t (*EngSetIndex)(void *engine, IndexCallback cb,
                                       void *param);
typedef ENGCALL int32_t (*EngSetUser)(void *engine, UserCallback cb,
                                      void *param);
typedef ENGCALL int32_t (*EngSetPhoneme)(void *engine, PhonemeCallback cb,
                                         void *param);
typedef ENGCALL int32_t (*EngSetAnno)(void *engine, AnnoCallback cb,
                                      void *param);
typedef ENGCALL int32_t (*EngSetVoice)(void *engine, VoiceCallback cb,
                                       void *param);

/* Whatever the engine hands back from a question is a counted object and has
   to be let go of again. Slot two of its table is the release. */
typedef ENGCALL void (*ObjRelease)(void *obj);
#define OBJ_RELEASE 0x08

/* The format the sound layer wants, laid out as the platform's own audio
   header. Three of its fields are worked out from the other three. */
typedef struct {
    uint16_t tag;
    uint16_t channels;
    uint32_t rate;
    uint32_t bytesPerSecond;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
    uint16_t extra;
} WaveFormat;

#define WAVE_PCM     1
#define WAVE_MONO    1
#define WAVE_BITS    32

/* And the format the layer above hands down: how the samples are laid out,
   what rate they are at, and one more word this layer only carries. */

extern THIS void stb_postEngineError(SynthThread *t)
    MANGLED("?postEngineError@SynthThread@@AAEXXZ");
extern THIS void stb_postRomanizerError(SynthThread *t, int32_t language)
    MANGLED("?postRomanizerError@SynthThread@@AAEXH@Z");
extern THIS void stb_postSoundError(SynthThread *t)
    MANGLED("?postSoundError@SynthThread@@AAEXXZ");
extern THIS void stb_wordCallback(SynthThread *t, int32_t n)
    MANGLED("?wordCallback@SynthThread@@IAEXH@Z");
extern THIS void stb_sendPhonemesToUser(SynthThread *t)
    MANGLED("?sendPhonemesToUser@SynthThread@@AAEXXZ");
extern THIS void stb_sendRemainingSamplesToUser(SynthThread *t)
    MANGLED("?sendRemainingSamplesToUser@SynthThread@@AAEXXZ");
extern THIS void stb_sendRemainingPhonemesToUser(SynthThread *t)
    MANGLED("?sendRemainingPhonemesToUser@SynthThread@@AAEXXZ");

extern void stb_staticSynthCallback(int32_t, int32_t *, void *)
    MANGLED("?staticSynthCallback@SynthThread@@CAXHPAJPAX@Z");
extern void stb_staticSynthIndexCallback(int32_t, void *)
    MANGLED("?staticSynthIndexCallback@SynthThread@@CAXHPAX@Z");
extern void stb_staticPhonemeCallback(int32_t, uint32_t, void *)
    MANGLED("?staticPhonemeCallback@SynthThread@@CAXHKPAX@Z");
extern void stb_staticAnnoCallback(int32_t, int32_t, void *)
    MANGLED("?staticAnnoCallback@SynthThread@@CAXJJPAX@Z");
extern void stb_staticWordCallback(int32_t, void *)
    MANGLED("?staticWordCallback@SynthThread@@CAXHPAX@Z");
extern void stb_staticWordIndexCallback(int32_t, void *)
    MANGLED("?staticWordIndexCallback@SynthThread@@CAXHPAX@Z");
extern void stb_staticUserIndexCallback(void *)
    MANGLED("?staticUserIndexCallback@SynthThread@@CAXPAX@Z");
extern void stb_staticEnhancedSPRCallback(void *)
    MANGLED("?staticEnhancedSPRCallback@SynthThread@@CAXPAX@Z");
extern void stb_staticVoiceChangeCallback(int32_t, int16_t *, int16_t *,
                                          int16_t *, int16_t *, int16_t *,
                                          void *)
    MANGLED("?staticVoiceChangeCallback@SynthThread@@CAXJPAF0000PAX@Z");

extern THIS int32_t rz_processRemaining(void *r, char **out)
    MANGLED("?processRemaining@RomanizerManager@@QAEHPAPAD@Z");
extern THIS void rz_romClearErrors(void *r)
    MANGLED("?romClearErrors@RomanizerManager@@QAEXXZ");

extern THIS char *fm_filterText(void *m, const char *text, int32_t engine)
    MANGLED("?filterText@FilterManager@@QAEPADPBDJ@Z");

extern THIS int32_t cm_registerCallbackA(void *c, uint32_t which,
                                              IndexCallback cb, void *param)
    MANGLED("?registerCallback@ConcatenationManager@@QAEXKP6AXHPAX@Z0@Z");
extern THIS int32_t cm_registerCallbackB(void *c, uint32_t which,
                                             UserCallback cb, void *param)
    MANGLED("?registerCallback@ConcatenationManager@@QAEXKP6AXPAX@Z0@Z");
extern THIS int32_t cm_engineSupports(void *c, uint32_t a, uint32_t b)
    MANGLED("?engineSupportsConcatenative@ConcatenationManager@@QAEHKK@Z");

extern THIS void *ea_getEngine(void *a, const LangIdentifier *l)
    MANGLED("?getEngine@EngineArray@@QAEPAVEngineWrapper@@QBVLangIdentifier@@@Z");
extern THIS void lang_setString(LangIdentifier *l)
    MANGLED("?setString@LangIdentifier@@AAEXXZ");

extern THIS int32_t aq_postUser(void *a, int32_t what, int32_t value)
    MANGLED("?postUser@ETIappMessageQueue@@QAEHJJ@Z");
extern THIS void stm_pauseMessageQueue(void *a, int32_t how)
    MANGLED("?pauseMessageQueue@ETIappMessageQueue@@QAEXH@Z");
extern THIS int32_t aq_poll(void *a)
    MANGLED("?poll@ETIappMessageQueue@@QAEJXZ");
extern THIS int32_t aq_synchronize(void *a)
    MANGLED("?synchronize@ETIappMessageQueue@@QAEJXZ");
extern THIS void aq_registerCallback(void *a, void *inst, void *cb,
                                      void *param, int16_t n, void *extra)
    MANGLED("?registerCallback@ETIappMessageQueue@@QAEXPAXP6AJ0JJ0@Z0F0@Z");

extern THIS int16_t snd_getStatus(void *s)
    MANGLED("?getStatus@SoundThread@@QAEFXZ");
extern THIS int32_t snd_close(void *s)
    MANGLED("?close@SoundThread@@QAEHXZ");
extern THIS int32_t snd_flush(void *s)
    MANGLED("?flush@SoundThread@@QAEHXZ");
extern THIS int32_t snd_hold(void *s, int32_t how)
    MANGLED("?hold@SoundThread@@QAEHH@Z");
extern THIS int32_t snd_write(void *s, const int32_t *samples, uint32_t n)
    MANGLED("?write@SoundThread@@QAEHPBJI@Z");
extern THIS int32_t snd_insertIndex(void *s, int32_t id)
    MANGLED("?insertIndex@SoundThread@@QAEHJ@Z");

extern THIS uint32_t sti_newIndex(void *m)
    MANGLED("?newIndex@IndexManager@@QAEKXZ");
extern THIS Index *sti_getIndex(void *m, uint32_t id)
    MANGLED("?getIndex@IndexManager@@QAEPAUIndex@@K@Z");

extern THIS uint32_t iq_reduceLeadTime(void *q, uint32_t n)
    MANGLED("?reduceLeadTime@IndexQueue@@QAEKK@Z");
extern THIS int32_t sti_indexDue(const void *q)
    MANGLED("?indexDue@IndexQueue@@QBEHXZ");
extern THIS uint32_t iq_remove(void *q)
    MANGLED("?remove@IndexQueue@@QAEHXZ");

extern THIS int32_t sy_eventWait(void *e, int32_t ms)
    MANGLED("?wait@ETIEvent@@QAEHJ@Z");
extern THIS int32_t sy_eventUnsignal(void *e)
    MANGLED("?unsignal@ETIEvent@@QAEHXZ");
extern THIS int32_t sy_semRelease(void *s, int32_t n)
    MANGLED("?release@Semaphore@@QAEHJ@Z");

extern const uint32_t pcm_cvt_bytes;
extern THIS void *pcm_cvt_ctor(void *c) MANGLED("??0AudioConverter@@QAE@XZ");
extern THIS void pcm_cvt_dtor(void *c) MANGLED("??1AudioConverter@@QAE@XZ");
extern THIS int32_t pcm_cvt_setSource(void *c, WaveFormat *f)
    MANGLED("?setSourceFormat@AudioConverter@@QAEJPAUtWAVEFORMATEX@@@Z");
extern THIS int32_t pcm_cvt_setDest(void *c, WaveFormat *f)
    MANGLED("?setDestFormat@AudioConverter@@QAEJPAUtWAVEFORMATEX@@@Z");

/* Same collection table as the callbacks use: isEmpty comes first. */
typedef struct Collection Collection;
typedef struct {
    THIS int32_t (*isEmpty)(const Collection *self);
} CollectionVtbl;
struct Collection { const CollectionVtbl *vt; };

static int stw_queueIsEmpty(void *q)
{
    Collection *c = (Collection *)q;

    return c->vt->isEmpty(c);
}

/* ---- the short answers ---- */

THIS void *stw_getRomMngr(SynthThread *t)      { return ST_ROMAN(t); }
THIS void *stw_getFilterMngr(SynthThread *t)   { return ST_FILTERS(t); }
THIS void *stw_getConcatMngr(SynthThread *t)   { return ST_CONCAT(t); }
THIS int32_t stw_checkSynthesizing(SynthThread *t) { return ST_POSTED(t); }

/* Forget every error we have already reported, so the next one is heard. */
THIS int32_t stw_clearErrors(SynthThread *t)
{
    ST_ENGERR(t) = 0;
    ST_ROMERR(t) = 0;
    ST_SILENT(t) = 0;
    rz_romClearErrors(ST_ROMAN(t));
    return OK;
}

/* Text through the filters, or straight back if there are none. */
THIS char *stw_filterText(SynthThread *t, const char *text, int32_t engine)
{
    if (!ST_FILTERS(t))
        return (char *)text;
    return fm_filterText(ST_FILTERS(t), text, engine);
}

/* Whether this engine is one of the old ones. The engine is asked once and
   the answer kept, because it cannot change while the engine is in play.

   Asking hands back an object that has to be let go of again, and an engine
   that answers at all is by that token not old. */
THIS int32_t stw_isOldEngine(SynthThread *t)
{
    void *answer = 0;
    EngAsk ask;
    uint8_t old;

    if (!ST_ENGINE(t))
        return 1;

    ask = (EngAsk)ENG_CALL(t, ENG_ASK);
    old = (uint8_t)(ask(ST_ENGINE(t), 3, &answer) == 0);
    ST_OLD_ENGINE(t) = old;
    if (answer) {
        ObjRelease release = (ObjRelease)ENG_CALL_ON(answer, OBJ_RELEASE);

        release(answer);
    }
    return old;
}

/* ---- what the caller asks of the thread from outside ---- */

/* Stand still, or start again. Both the queue of messages waiting for the
   caller and the device itself have to be told. */
THIS int32_t stw_pause(SynthThread *t, int32_t how)
{
    int32_t rc = OK;

    stm_pauseMessageQueue(ST_APP(t), how);
    if (ST_SOUND(t) && !snd_hold(ST_SOUND(t), how))
        rc = ERR_NO_SOUND;
    return rc;
}

/* Let a blocked thread go. */
THIS int32_t stw_unblock(SynthThread *t)
{
    int32_t rc = OK;

    if (!ST_BLOCKER(t) || !sy_semRelease(ST_BLOCKER(t), 1))
        rc = ERR_FAILED;
    return rc;
}

/* Take whatever the caller is owed off the queue, once or until there is
   nothing left. Either way, catching up means nothing is outstanding. */
THIS int32_t stw_poll(SynthThread *t)
{
    int32_t rc = aq_poll(ST_APP(t));

    if (rc == QUEUE_CAUGHT_UP)
        ST_POSTED(t) = 0;
    return rc;
}

THIS int32_t stw_synchronize(SynthThread *t)
{
    int32_t rc = aq_synchronize(ST_APP(t));

    if (rc == QUEUE_CAUGHT_UP)
        ST_POSTED(t) = 0;
    return rc;
}

/* The caller's own callback. It is refused while anything is outstanding,
   because changing it half way through a run would lose whatever the old one
   had not been given yet. */
THIS int32_t stw_registerCallback(SynthThread *t, void *inst, void *cb,
                                  void *param, int16_t n, void *extra)
{
    void *lock = ST_LOCK(t);
    int32_t rc = ERR_REFUSED;

    sy_mutexWait(lock, -1);
    if (!ST_POSTED(t)) {
        aq_registerCallback(ST_APP(t), inst, cb, param, n, extra);
        rc = OK;
    }
    sy_mutexRelease(lock);
    return rc;
}

/* ---- text into the engine ---- */

/* A sentence the romanizer has finished with, on its way to the engine.

   The count of characters the engine has been given is kept here, and it is
   not simply the length: a run that does not end in a space is one character
   short of a word boundary and gets one added, and a backslash escape counts
   as one character rather than two. Both of those are how the engine itself
   counts, and the counts have to agree or every index mark lands in the
   wrong place. */
THIS void stw_addTextToEngine(SynthThread *t, char *text, int32_t len)
{
    char *copy = (char *)cpp_new((uint32_t)len + 1);
    int escaped;
    int32_t i;
    EngAddText add;

    if (!copy) {
        if (APP_LISTENING(ST_APP(t)))
            aq_postUser(ST_APP(t), APP_INDEX_LOST, 0);
        return;
    }

    memcpy(copy, text, (size_t)len);
    copy[len] = 0;

    if (copy[len - 1] != ' ')
        ST_SAMPLES(t) += 1;
    ST_SAMPLES(t) += len;

    escaped = 0;
    for (i = 0; copy[i]; i++) {
        if (!escaped && copy[i] == '\\') {
            ST_SAMPLES(t) -= 1;
            escaped = 1;
        } else {
            escaped = 0;
        }
    }

    add = (EngAddText)ENG_CALL(t, ENG_ADD_TEXT);
    if (add(ST_ENGINE(t), copy))
        stb_postEngineError(t);
    if (ST_PHONBUF(t))
        stb_sendPhonemesToUser(t);
    cpp_delete(copy);
}

/* Everything the romanizer is still holding, pushed through whether or not
   it makes a whole sentence, and the engine told there is no more coming. */
THIS void stw_processRemaining(SynthThread *t)
{
    char *left = 0;
    int32_t n;
    EngCommand command;

    n = rz_processRemaining(ST_ROMAN(t), &left);
    if (n == -1)
        stb_postRomanizerError(t, 0);
    else if (n == 0)
        left = 0;

    command = (EngCommand)ENG_CALL(t, ENG_COMMAND);
    if (command(ST_ENGINE(t), left))
        stb_postEngineError(t);

    if (ST_SAMPLES(t) > 0)
        stb_wordCallback(t, ST_SAMPLES(t));
}

/* The engine has finished with some characters. Wind the counts back by that
   many and hand over any mark whose time has come, exactly as the word
   callback does; the two differ only in where the count comes from. */
THIS void stw_removeCharsFromEngine(SynthThread *t, int32_t n)
{
    int done = 0;

    if (n > ST_SAMPLES(t))
        n = ST_SAMPLES(t);
    ST_SAMPLES(t) -= n;
    ST_LASTMARK(t) -= n;
    if (ST_LASTMARK(t) < 0)
        ST_LASTMARK(t) = 0;

    while (!stw_queueIsEmpty(ST_INDEXQ(t)) && !done) {
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

/* ---- winding down ---- */

/* Put a mark of our own at the very end of the sound and wait for the device
   to reach it, so that nothing is cut off. Then close the device, but only
   if nothing else has arrived in the meantime. */
THIS void stw_shutDownSound(SynthThread *t)
{
    int16_t status = snd_getStatus(ST_SOUND(t));
    uint32_t id;
    void *lock;

    if (status != 3 && status != 1) {
        if (status == 5)
            stb_postSoundError(t);
        return;
    }

    id = sti_newIndex(ST_INDEXMGR(t));
    if (id) {
        Index *ix = sti_getIndex(ST_INDEXMGR(t), id);
        int32_t silence = 0;

        ix->kind = MARK_SYNCHRONISE;
        ix->payload = 0;
        /* One sample of nothing after the mark, so the device has something
           to carry it to the end of what it is already playing. */
        if (snd_insertIndex(ST_SOUND(t), (int32_t)id)
            && snd_write(ST_SOUND(t), &silence, 1)
            && snd_flush(ST_SOUND(t))) {
            sy_eventWait(ST_SYNCED(t), -1);
            sy_eventUnsignal(ST_SYNCED(t));
        } else {
            stb_postSoundError(t);
        }
    } else if (APP_LISTENING(ST_APP(t))) {
        aq_postUser(ST_APP(t), APP_INDEX_LOST, 0);
    }

    lock = ST_LOCK(t);
    sy_mutexWait(lock, -1);
    if (ST_PENDING(t) == 0 && !ST_SILENT(t)) {
        if (!snd_close(ST_SOUND(t)))
            stb_postSoundError(t);
    }
    sy_mutexRelease(lock);
}

/* The end of a run. Only the message that finds nothing else outstanding
   does the winding down; the rest let go of the lock and leave. */
THIS void stw_shutDown(SynthThread *t)
{
    void *lock = ST_LOCK(t);
    int last;

    sy_mutexWait(lock, -1);
    last = ST_PENDING(t) == 0;
    if (!last) {
        sy_mutexRelease(lock);
        return;
    }

    ST_SAMPLES(t) = 0;
    ST_LASTMARK(t) = 0;
    if (!ST_SILENT(t) && ST_SOUND(t))
        stw_shutDownSound(t);
    sy_mutexRelease(lock);

    if (ST_SAMPBUF(t))
        stb_sendRemainingSamplesToUser(t);
    if (ST_PHONBUF(t))
        stb_sendRemainingPhonemesToUser(t);
}

/* And tell the caller it has all been said. */
THIS void stw_stopSynthesis(SynthThread *t, int32_t seq)
{
    void *lock = ST_LOCK(t);

    sy_mutexWait(lock, -1);
    if (ST_PENDING(t) == 0)
        aq_postUser(ST_APP(t), APP_SPEAKING_DONE, seq);
    sy_mutexRelease(lock);
}

/* ---- choosing a language and a rate ---- */

/* Is there an engine for this language, and if the caller left the dialect
   open, which one?

   A dialect of three means "whichever there is". The dialects are tried in
   turn from nought, and the one that answers is written back into the
   caller's record so it knows what it got. Three itself is never tried,
   because by then the search has run out. */
THIS int32_t stw_checkLanguage(SynthThread *t, LangIdentifier *want)
{
    void *engine = 0;
    uint8_t family = (uint8_t)(want->packed >> 16);
    uint8_t dialect = (uint8_t)(want->packed & 0xff);
    uint8_t variant = (uint8_t)((want->packed & 0xff00) >> 8);
    LangIdentifier *probe;
    uint8_t tried = 0;

    probe = (LangIdentifier *)cpp_new(sizeof(LangIdentifier));
    if (probe) {
        probe->packed = 0;
        probe->packed = ((uint32_t)family << 16) | ((uint32_t)variant << 8)
                    | (dialect == 3 ? 0u : dialect);
        lang_setString(probe);
    }

    if (dialect == 3) {
        while (!engine && tried != 3) {
            engine = ea_getEngine(ST_ENGINES(t), probe);
            if (!engine) {
                tried += 1;
                probe->packed = ((uint32_t)family << 16)
                            | ((uint32_t)variant << 8) | tried;
                lang_setString(probe);
            } else {
                want->packed = ((uint32_t)family << 16)
                           | ((uint32_t)variant << 8) | tried;
                lang_setString(want);
            }
        }
    } else {
        engine = ea_getEngine(ST_ENGINES(t), want);
    }

    if (probe)
        cpp_delete(probe);
    return engine != 0;
}

/* Put the engine into whichever rate it can actually work at, and stand a
   converter between it and the caller if the two do not agree.

   The engine has two rates of its own. Eight and sixteen thousand it does
   itself; anything else it does at eleven thousand and something and lets
   the converter make up the difference. */
THIS int32_t stw_createAudioConverter(SynthThread *t, SampleFormat *fmt)
{
    int32_t wanted = fmt->rate;
    int32_t native;
    int32_t rc = ERR_BAD_RATE;
    WaveFormat wave;
    EngCommand command;
    const char *line;

    switch (fmt->rate) {
    case RATE_8000:  native = RATE_8000; break;
    case RATE_16000: native = RATE_8000; break;
    default:         native = RATE_11025; break;
    }

    if (native == wanted) {
        /* Nothing to convert, so anything already standing in the way is
           taken out again. */
        rc = OK;
        if (ST_CONVERTER(t)) {
            pcm_cvt_dtor(ST_CONVERTER(t));
            cpp_delete(ST_CONVERTER(t));
            ST_CONVERTER(t) = 0;
        }
    } else {
        if (!ST_CONVERTER(t)) {
            void *c = cpp_new(pcm_cvt_bytes);

            ST_CONVERTER(t) = c ? pcm_cvt_ctor(c) : 0;
        }

        wave.tag = WAVE_PCM;
        wave.channels = WAVE_MONO;
        wave.bitsPerSample = WAVE_BITS;
        wave.extra = 0;
        wave.blockAlign = (uint16_t)(wave.bitsPerSample / 8);
        wave.rate = (uint32_t)native;
        wave.bytesPerSecond = (uint32_t)(wave.blockAlign * native);
        rc = pcm_cvt_setSource(ST_CONVERTER(t), &wave);
        if (rc == OK) {
            wave.rate = (uint32_t)wanted;
            wave.bytesPerSecond = (uint32_t)(wave.blockAlign * wanted);
            rc = pcm_cvt_setDest(ST_CONVERTER(t), &wave);
        }
    }

    if (rc == OK) {
        ST_DIRECT(t) = 1;
        line = native == RATE_8000 ? CMD_RATE_8000
             : native == RATE_11025 ? CMD_RATE_11025
             : CMD_RATE_OTHER;
        command = (EngCommand)ENG_CALL(t, ENG_COMMAND);
        rc = command(ST_ENGINE(t), line);
        ST_DIRECT(t) = 0;
        if (rc == OK) {
            SampleFormat *kept = ST_FORMAT(t);

            kept->layout = fmt->layout;
            kept->rate = fmt->rate;
            kept->width = fmt->width;
        }
    }

    if (rc != OK && ST_CONVERTER(t)) {
        pcm_cvt_dtor(ST_CONVERTER(t));
        cpp_delete(ST_CONVERTER(t));
        ST_CONVERTER(t) = 0;
    }
    return rc;
}

/* ---- getting an engine going ---- */

/* Hand the engine every callback it will need, then start it.

   Which of the two ways of reporting word positions is used depends on what
   this engine can do, and the concatenative side is told the same thing so
   the two agree. The voice and reporting callbacks only go in for an engine
   the concatenative side knows about. */
THIS int32_t stw_engineInitialize(SynthThread *t, void *engine)
{
    int32_t ok = 1;
    EngSetIndex setIndex;
    EngSetSynth setSynth;
    EngStart start;
    EngReset reset;

    if (ST_FLAGS(t) & STF_ROMANIZING) {
        EngSetUser setUser = (EngSetUser)ENG_CALL_ON(engine, ENG_USER_INDEX_CB);

        setUser(engine, stb_staticUserIndexCallback, t);
        cm_registerCallbackB(ST_CONCAT(t), 4,
                                 stb_staticUserIndexCallback, t);
    } else if (ST_FLAGS(t) & STF_WORD_STARTS) {
        EngSetIndex setWord =
            (EngSetIndex)ENG_CALL_ON(engine, ENG_WORD_START_CB);

        setWord(engine, stb_staticWordCallback, t);
        cm_registerCallbackA(ST_CONCAT(t), 2,
                                  stb_staticWordIndexCallback, t);
    }

    /* Word marks go to the engine as nothing at all. Whatever wants them
       puts itself in later. */
    if (ST_FLAGS(t) & STF_WORD_MARKS) {
        EngSetIndex setMarks = (EngSetIndex)ENG_CALL_ON(engine, ENG_WORD_CB);

        setMarks(engine, 0, t);
    }

    setIndex = (EngSetIndex)ENG_CALL_ON(engine, ENG_SYNTH_INDEX_CB);
    setIndex(engine, stb_staticSynthIndexCallback, t);
    {
        EngSetPhoneme setPhoneme =
            (EngSetPhoneme)ENG_CALL_ON(engine, ENG_PHONEME_CB);
        EngSetAnno setAnno = (EngSetAnno)ENG_CALL_ON(engine, ENG_ANNO_CB);

        setPhoneme(engine, stb_staticPhonemeCallback, t);
        setAnno(engine, stb_staticAnnoCallback, t);
    }

    if (cm_engineSupports(ST_CONCAT(t), (ST_ENGINE_ID(t) >> 16) & 0xff,
                           ST_ENGINE_ID(t) & 0xff)) {
        EngSetVoice setVoice = (EngSetVoice)ENG_CALL_ON(engine, ENG_VOICE_CB);
        EngSetUser setSPR = (EngSetUser)ENG_CALL_ON(engine, ENG_SPR_CB);

        setVoice(engine, stb_staticVoiceChangeCallback, t);
        setSPR(engine, stb_staticEnhancedSPRCallback, t);
    }

    setSynth = (EngSetSynth)ENG_CALL_ON(engine, ENG_SYNTH_CB);
    start = (EngStart)ENG_CALL_ON(engine, ENG_START);
    reset = (EngReset)ENG_CALL_ON(engine, ENG_RESET);

    if (setSynth(engine, stb_staticSynthCallback, t)
        || start(engine)
        || reset(engine, 0))
        ok = 0;
    return ok;
}

ALIAS("?getRomMngr@SynthThread@@QAEPAXXZ", "stw_getRomMngr");
ALIAS("?getFilterMngr@SynthThread@@QAEPAXXZ", "stw_getFilterMngr");
ALIAS("?getConcatMngr@SynthThread@@QAEPAXXZ", "stw_getConcatMngr");
ALIAS("?checkSynthesizing@SynthThread@@QAEHXZ", "stw_checkSynthesizing");
ALIAS("?clearErrors@SynthThread@@QAEJXZ", "stw_clearErrors");
ALIAS("?filterText@SynthThread@@QAEPADPBDJ@Z", "stw_filterText");
ALIAS("?isOldEngine@SynthThread@@QAEHXZ", "stw_isOldEngine");
ALIAS("?pause@SynthThread@@QAEJH@Z", "stw_pause");
ALIAS("?unblock@SynthThread@@QAEJXZ", "stw_unblock");
ALIAS("?poll@SynthThread@@QAEJXZ", "stw_poll");
ALIAS("?synchronize@SynthThread@@QAEJXZ", "stw_synchronize");
ALIAS("?registerCallback@SynthThread@@QAEJPAXP6AJ0JJ0@Z0F0@Z",
      "stw_registerCallback");
ALIAS("?addTextToEngine@SynthThread@@QAEXPADH@Z", "stw_addTextToEngine");
ALIAS("?processRemaining@SynthThread@@QAEXXZ", "stw_processRemaining");
ALIAS("?removeCharsFromEngine@SynthThread@@QAEXH@Z",
      "stw_removeCharsFromEngine");
ALIAS("?shutDownSound@SynthThread@@AAEXXZ", "stw_shutDownSound");
ALIAS("?shutDown@SynthThread@@AAEXXZ", "stw_shutDown");
ALIAS("?stopSynthesis@SynthThread@@AAEXJ@Z", "stw_stopSynthesis");
ALIAS("?checkLanguage@SynthThread@@AAEHPAVLangIdentifier@@@Z",
      "stw_checkLanguage");
ALIAS("?createAudioConverter@SynthThread@@AAEJAAUECIsampleFormat@@@Z",
      "stw_createAudioConverter");
ALIAS("?engineInitialize@SynthThread@@AAEHPAVEngineWrapper@@@Z",
      "stw_engineInitialize");
