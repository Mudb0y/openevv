/* The buffers and the format the caller asks results in.

   A caller wants either samples or phonemes, never both, and says so by
   handing down a buffer for one of them. Registering either is the same
   shape of thing: refuse it if anything is still outstanding or if the other
   kind is already registered, take the reporting callbacks down, tell the
   engine which mode it is now in, put the reporting back, and only then keep
   the buffer.

   Taking the callbacks down and putting them back is written out four times
   over in the original, identically each time. It is one pair of routines
   here, because four copies of the same thing is the compiler's business and
   not something worth preserving.

   Names are prefixed because a plain C name has to be unique across the
   whole link; the aliases at the foot carry the real ones. */

#include <stdint.h>
#include <string.h>
#include "eci_synththread.h"
#include "evv_abi.h"

/* Answers this layer gives back. */
#define OK               0
#define ERR_BAD_ARG    (-3)
#define ERR_BAD_FORMAT (-5)
#define ERR_REFUSED    (-9)
#define ERR_HAVE_FILE  (-10)
#define ERR_HAVE_SAMP  (-11)
#define ERR_HAVE_PHON  (-12)
#define ERR_ENGINE     (-15)

/* The three rates a caller may ask for. */
#define RATE_8000    0x1f40
#define RATE_11025   0x2b11
#define RATE_22050   0x5622

/* And the only layout and width the engine will hand over. */
#define LAYOUT_PCM   2
#define WIDTH_16     16

/* Which callback a registration is for, on the concatenative side. */
#define CAT_CB_WORD_START 1
#define CAT_CB_WORD_MARK  2
#define CAT_CB_PHONEME    3
#define CAT_CB_USER_INDEX 4
#define CAT_CB_BREAK      5

/* Parameters the two managers understand. */
#define CAT_SAMPLE_RATE   0x11
#define ROM_CONCATENATIVE 0x3e8
#define ROM_PHONEMES      0x3e9
#define ROM_SAMPLE_RATE   0x3eb

/* Slots of the engine's table, all stdcall with the engine pushed. */
#define ENG_COMMAND        0x18
#define ENG_SYNTH_CB       0x44
#define ENG_WORD_START_CB  0x4c
#define ENG_USER_INDEX_CB  0x98

#define ENGCALL __attribute__((stdcall))
#define ENG_CALL(t, off) \
    (((void **)*(void ***)ST_ENGINE(t))[(off) / 4])

typedef void (*IndexCallback)(int32_t, void *);
typedef void (*UserCallback)(void *);
typedef void (*SynthCallback)(int32_t, int32_t *, void *);

typedef ENGCALL int32_t (*EngCommand)(void *engine, const char *line);
typedef ENGCALL int32_t (*EngSetSynth)(void *engine, SynthCallback cb,
                                       void *param);
typedef ENGCALL int32_t (*EngSetIndex)(void *engine, IndexCallback cb,
                                       void *param);
typedef ENGCALL int32_t (*EngSetUser)(void *engine, UserCallback cb,
                                      void *param);

/* The lines that put the engine into one mode or the other. */
static const char CMD_PHONEMES_ON[] = "`espr1 `esp0";
static const char CMD_PHONEMES_OFF[] = "`espr0 `esp1";
static const char CMD_CONCATENATIVE[] = "`esp2";
static const char CMD_SAMPLES[] = "`esp1";

/* What the caller hands down to say how it wants the samples. */

/* And what the sound manager keeps about a format it is already playing.
   Only the fields the comparison reaches are named. */
typedef struct {
    uint8_t pad_00[0x04];
    void   *sound;      /* +0x04, the device this format is played on */
    uint8_t pad_08[0x04];
    int32_t a;          /* +0x0c */
    int32_t b;          /* +0x10 */
    int32_t c;          /* +0x14 */
    char   *name;       /* +0x18 */
    int32_t d;          /* +0x1c */
    int32_t e;          /* +0x20 */
    int32_t f;          /* +0x24 */
    int32_t g;          /* +0x28 */
} AudioFormat;

/* The request it is compared against, which is the same run of fields with
   the name in the middle rather than at +0x18. */
typedef struct {
    int32_t a, b, c;    /* +0x00, +0x04, +0x08 */
    char   *name;       /* +0x0c */
    int32_t d, e, f, g; /* +0x10 .. +0x1c */
} AudioRequest;

extern void *st_soundManager
    MANGLED("?m_soundManager@SynthThread@@0PAVSoundManager@@A");
extern THIS void sm_removeAudioFormat(void *m, AudioFormat *f)
    MANGLED("?removeAudioFormat@SoundManager@@QAEXPAVAudioFormat@@@Z");
extern THIS void sm_replaceDefaultFieldsWithValues(void *m, AudioRequest *want)
    MANGLED("?replaceDefaultFieldsWithValues@SoundManager@@QAEXPAUECIaudioFormat@@@Z");
extern THIS int32_t sm_requestAudioFormat(void *m, AudioRequest *want,
                                          AudioFormat **out)
    MANGLED("?requestAudioFormat@SoundManager@@QAEJPAUECIaudioFormat@@PAPAVAudioFormat@@@Z");
extern THIS int32_t cm_engineSupports(void *c, uint32_t a, uint32_t b)
    MANGLED("?engineSupportsConcatenative@ConcatenationManager@@QAEHKK@Z");
extern THIS void stb_postEngineError(SynthThread *t2)
    MANGLED("?postEngineError@SynthThread@@AAEXXZ");

extern THIS void stb_postEngineError(SynthThread *t)
    MANGLED("?postEngineError@SynthThread@@AAEXXZ");
extern THIS int32_t stw_createAudioConverter(SynthThread *t, void *fmt)
    MANGLED("?createAudioConverter@SynthThread@@AAEJAAUECIsampleFormat@@@Z");

extern void stb_staticSynthCallback(int32_t, int32_t *, void *)
    MANGLED("?staticSynthCallback@SynthThread@@CAXHPAJPAX@Z");
extern void stb_staticTorrentPhonemeCallback(int32_t, void *)
    MANGLED("?staticTorrentPhonemeCallback@SynthThread@@CAXHPAX@Z");
extern void stb_staticWordCallback(int32_t, void *)
    MANGLED("?staticWordCallback@SynthThread@@CAXHPAX@Z");
extern void stb_staticUserIndexCallback(void *)
    MANGLED("?staticUserIndexCallback@SynthThread@@CAXPAX@Z");
extern void stb_staticSynthesisBreakCallback(int32_t, void *)
    MANGLED("?staticSynthesisBreakCallback@SynthThread@@CAXHPAX@Z");

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

extern THIS int32_t rz_setParam(void *r, int32_t which, int32_t value)
    MANGLED("?setParam@RomanizerManager@@QAEHJH@Z");

/* Where the state block records that phonemes are wanted. */
#define STATE_PHONEMES(s) (*(int32_t *)((char *)(s) + 0x50))

/* ---- the reporting callbacks, off and on ---- */

/* Take down whichever of the two ways of reporting word positions this
   engine uses, on both sides at once. */
static void stf_reportingOff(SynthThread *t)
{
    if (ST_FLAGS(t) & STF_ROMANIZING) {
        EngSetUser set = (EngSetUser)ENG_CALL(t, ENG_USER_INDEX_CB);

        set(ST_ENGINE(t), 0, t);
        cm_registerCallbackB(ST_CONCAT(t), CAT_CB_USER_INDEX, 0, t);
    } else if (ST_FLAGS(t) & STF_WORD_STARTS) {
        EngSetIndex set = (EngSetIndex)ENG_CALL(t, ENG_WORD_START_CB);

        set(ST_ENGINE(t), 0, t);
        cm_registerCallbackA(ST_CONCAT(t), CAT_CB_WORD_MARK, 0, t);
    }
}

/* And put it back, along with the one that says a piece of speech ended.
   The concatenative side only gets the word callback when it is the one
   actually doing the speaking. */
static void stf_reportingOn(SynthThread *t)
{
    if (ST_FLAGS(t) & STF_ROMANIZING) {
        EngSetUser set = (EngSetUser)ENG_CALL(t, ENG_USER_INDEX_CB);

        set(ST_ENGINE(t), stb_staticUserIndexCallback, t);
        cm_registerCallbackB(ST_CONCAT(t), CAT_CB_USER_INDEX,
                                 stb_staticUserIndexCallback, t);
    } else if (ST_FLAGS(t) & STF_WORD_STARTS) {
        EngSetIndex set = (EngSetIndex)ENG_CALL(t, ENG_WORD_START_CB);

        set(ST_ENGINE(t), stb_staticWordCallback, t);
        if (ST_CONCAT(t) && cm_usingConcatenativeEngine(ST_CONCAT(t)))
            cm_registerCallbackA(ST_CONCAT(t), CAT_CB_WORD_START,
                                      stb_staticWordCallback, t);
    }
    cm_registerCallbackA(ST_CONCAT(t), CAT_CB_BREAK,
                              stb_staticSynthesisBreakCallback, t);
}

/* ---- the format the sound manager is already playing ---- */

/* Is a format the sound manager holds the one being asked for? The name is
   compared only when either side has one, and then both must. */
THIS int32_t stf_audioFormatEquals(AudioFormat *f, AudioRequest *want)
{
    int32_t same = 0;

    if (f->name || want->name) {
        if (!f->name || !want->name)
            return same;
        if (strcmp(f->name, want->name) != 0)
            return same;
    }
    if (f->a != want->a)  return same;
    if (f->b != want->b)  return same;
    if (f->c != want->c)  return same;
    if (f->d != want->d)  return same;
    if (f->e != want->e)  return same;
    if (f->f != want->f)  return same;
    if (f->g != want->g)  return same;
    same = 1;
    return same;
}

/* Give a format back to the sound manager. The device goes with it, because
   the device belonged to the format rather than to us. */
THIS int32_t stf_deleteAudioFormat(SynthThread *t)
{
    void *lock = ST_LOCK(t);
    int32_t rc = ERR_REFUSED;

    sy_mutexWait(lock, -1);
    if (!ST_POSTED(t)) {
        rc = OK;
        if (ST_OUTFMT(t)) {
            sm_removeAudioFormat(st_soundManager, (AudioFormat *)ST_OUTFMT(t));
            ST_OUTFMT(t) = 0;
            ST_SOUND(t) = 0;
        }
        if (ST_SILENT(t))
            ST_SILENT(t) = 0;
    }
    sy_mutexRelease(lock);
    return rc;
}

/* ---- the caller's phoneme buffer ---- */

/* Hand down a buffer to be given the phonemes in, or a null one to stop.

   The engine is put into the mode by a line of its own command language
   rather than by a parameter, and the romanizer and the state block are both
   told separately so that a caller asking what the setting is gets the
   answer the engine acted on.

   The buffer kept for the engine is four times as long as the one the caller
   gave plus a word, and the room recorded is one less than the count asked
   for. Both are the original's arithmetic. */
THIS int32_t stf_registerPhonemeBuffer(SynthThread *t, void *buf, int32_t n)
{
    void *lock = ST_LOCK(t);
    int32_t rc = ERR_REFUSED;
    EngCommand command;

    sy_mutexWait(lock, -1);
    if (ST_POSTED(t))
        goto done;
    if (ST_OUTFMT(t)) {
        rc = ERR_HAVE_FILE;
        goto done;
    }
    if (ST_SAMPBUF(t)) {
        rc = ERR_HAVE_SAMP;
        goto done;
    }
    if (buf && n < 2) {
        rc = ERR_BAD_ARG;
        goto done;
    }

    rc = OK;
    stf_reportingOff(t);
    command = (EngCommand)ENG_CALL(t, ENG_COMMAND);

    if (buf) {
        if (command(ST_ENGINE(t), CMD_PHONEMES_ON)) {
            rc = ERR_ENGINE;
        } else {
            STATE_PHONEMES(ST_STATE(t)) = 1;
            rz_setParam(ST_ROMAN(t), ROM_PHONEMES, 1);
            ST_DIRECT(t) = 0;
            stf_reportingOn(t);
        }
    } else {
        if (command(ST_ENGINE(t), CMD_PHONEMES_OFF)) {
            rc = ERR_ENGINE;
        } else {
            STATE_PHONEMES(ST_STATE(t)) = 0;
            rz_setParam(ST_ROMAN(t), ROM_PHONEMES, 0);
            stf_reportingOn(t);
        }
    }

    if (rc == OK) {
        ST_PHONBUF(t) = buf;
        if (ST_ENGPHON(t)) {
            cpp_delete(ST_ENGPHON(t));
            ST_ENGPHON(t) = 0;
        }
        ST_ENGPHON(t) = (char *)cpp_new((uint32_t)(4 * n + 4));
        ST_ENGPHONROOM(t) = n - 1;
        ST_PHONHELD(t) = 0;
    }

done:
    sy_mutexRelease(lock);
    return rc;
}

/* ---- the caller's sample buffer ---- */

/* Hand down a buffer to be given the samples in, or a null one to stop.

   A null buffer is the short road out: nothing is said to the engine at all,
   because there is nothing to say. Anything else has to be an even number of
   bytes, and the format has to be one the engine can actually produce. The
   three fields of the format are filled in with defaults where the caller
   left them at nought, and the caller's own record is written back, so it
   can read off what it got. */
THIS int32_t stf_registerSampleBuffer(SynthThread *t, int16_t *buf,
                                      int32_t bytes, SampleFormat *fmt)
{
    void *lock = ST_LOCK(t);
    int32_t rc = ERR_REFUSED;
    EngCommand command;

    sy_mutexWait(lock, -1);
    if (ST_POSTED(t))
        goto done;

    rc = ERR_BAD_ARG;
    if (ST_OUTFMT(t)) {
        rc = ERR_HAVE_FILE;
        goto done;
    }
    if (ST_PHONBUF(t)) {
        rc = ERR_HAVE_PHON;
        goto done;
    }

    if (!buf) {
        ST_SAMPBUF(t) = buf;
        ST_SAMPHELD(t) = 0;
        rc = OK;
        goto done;
    }

    /* An odd count of bytes cannot hold a whole sample. */
    if (!bytes || (bytes & 1))
        goto done;

    if (fmt->layout == 0)
        fmt->layout = LAYOUT_PCM;
    if (fmt->width == 0)
        fmt->width = WIDTH_16;
    if (fmt->rate == 0)
        fmt->rate = RATE_11025;

    if (fmt->layout != LAYOUT_PCM || fmt->width != WIDTH_16
        || (fmt->rate != RATE_8000 && fmt->rate != RATE_11025
            && fmt->rate != RATE_22050)) {
        rc = ERR_BAD_FORMAT;
        goto done;
    }

    stf_reportingOff(t);
    if (cm_setParam(ST_CONCAT(t), CAT_SAMPLE_RATE, fmt->rate, 1) == -1)
        rc = ERR_ENGINE;

    ST_DIRECT(t) = 1;
    command = (EngCommand)ENG_CALL(t, ENG_COMMAND);

    if (ST_CONCAT(t) && cm_usingConcatenativeEngine(ST_CONCAT(t))) {
        cm_registerCallbackC(ST_CONCAT(t), stb_staticSynthCallback, t);
        cm_registerCallbackA(ST_CONCAT(t), CAT_CB_PHONEME,
                                  stb_staticTorrentPhonemeCallback, t);
        if (command(ST_ENGINE(t), CMD_CONCATENATIVE))
            stb_postEngineError(t);
        rz_setParam(ST_ROMAN(t), ROM_CONCATENATIVE, 1);
        rz_setParam(ST_ROMAN(t), ROM_SAMPLE_RATE, fmt->rate);
    } else {
        EngSetSynth setSynth = (EngSetSynth)ENG_CALL(t, ENG_SYNTH_CB);

        if (setSynth(ST_ENGINE(t), stb_staticSynthCallback, t)
            || command(ST_ENGINE(t), CMD_SAMPLES))
            rc = ERR_ENGINE;
        else
            rz_setParam(ST_ROMAN(t), ROM_CONCATENATIVE, 0);
    }
    ST_DIRECT(t) = 0;

    ST_SAMPBUF(t) = buf;
    ST_SAMPROOM(t) = bytes;
    ST_SAMPHELD(t) = 0;
    rc = stw_createAudioConverter(t, fmt) ? ERR_ENGINE : OK;

    stf_reportingOn(t);

done:
    sy_mutexRelease(lock);
    return rc;
}

/* ---- a whole new format from the sound manager ---- */

/* Ask the sound manager for a format, and with it the device that plays it.

   Most of this is the same taking down and putting back of the reporting
   callbacks the two buffer registrations do, with one difference: the word
   callback only goes to the concatenative side if the side says it knows
   this engine, rather than if it is the one speaking.

   One fault kept as it stands and marked below: if the converter cannot be
   made, the format is handed back to the manager but not forgotten, and the
   device is then read out of it anyway. */
THIS int32_t stf_newAudioFormat(SynthThread *t, AudioRequest *want)
{
    void *lock = ST_LOCK(t);
    int32_t rc = ERR_REFUSED;
    EngCommand command;
    SampleFormat fmt;

    sy_mutexWait(lock, -1);
    if (ST_POSTED(t))
        goto done;
    if (ST_SAMPBUF(t)) {
        rc = ERR_HAVE_SAMP;
        goto done;
    }
    if (ST_PHONBUF(t)) {
        rc = ERR_HAVE_PHON;
        goto done;
    }

    rc = OK;
    sm_replaceDefaultFieldsWithValues(st_soundManager, want);
    /* Already playing exactly this? Then there is nothing to do. */
    if (ST_OUTFMT(t)
        && stf_audioFormatEquals((AudioFormat *)ST_OUTFMT(t), want))
        goto done;
    if (ST_OUTFMT(t))
        stf_deleteAudioFormat(t);

    rc = sm_requestAudioFormat(st_soundManager, want,
                               (AudioFormat **)&ST_OUTFMT(t));
    if (rc)
        goto done;

    stf_reportingOff(t);

    /* Put the reporting back. The word branch asks whether the
       concatenative side knows this engine at all, which is not the same
       question the buffer registrations ask. */
    if (ST_FLAGS(t) & STF_ROMANIZING) {
        EngSetUser set = (EngSetUser)ENG_CALL(t, ENG_USER_INDEX_CB);

        set(ST_ENGINE(t), stb_staticUserIndexCallback, t);
    } else if (ST_FLAGS(t) & STF_WORD_STARTS) {
        EngSetIndex set = (EngSetIndex)ENG_CALL(t, ENG_WORD_START_CB);

        set(ST_ENGINE(t), stb_staticWordCallback, t);
        if (cm_engineSupports(ST_CONCAT(t), (ST_ENGINE_ID(t) >> 16) & 0xff,
                               ST_ENGINE_ID(t) & 0xff))
            cm_registerCallbackA(ST_CONCAT(t), CAT_CB_WORD_START,
                                      stb_staticWordCallback, t);
    }
    cm_registerCallbackA(ST_CONCAT(t), CAT_CB_BREAK,
                              stb_staticSynthesisBreakCallback, t);

    if (cm_setParam(ST_CONCAT(t), CAT_SAMPLE_RATE, want->b, 1) == -1)
        rc = ERR_ENGINE;

    fmt.layout = want->a;
    fmt.rate = want->b;
    fmt.width = want->c;
    if (stw_createAudioConverter(t, &fmt)) {
        /* Marked: given back but not forgotten. */
        sm_removeAudioFormat(st_soundManager, (AudioFormat *)ST_OUTFMT(t));
        rc = ERR_ENGINE;
    }

    rz_setParam(ST_ROMAN(t), ROM_CONCATENATIVE, ST_CONCAT(t) ? 1 : 0);

    command = (EngCommand)ENG_CALL(t, ENG_COMMAND);
    if (ST_CONCAT(t) && cm_usingConcatenativeEngine(ST_CONCAT(t))) {
        cm_registerCallbackC(ST_CONCAT(t), stb_staticSynthCallback, t);
        if (ST_FLAGS(t) & STF_ROMANIZING)
            cm_registerCallbackB(ST_CONCAT(t), CAT_CB_USER_INDEX,
                                     stb_staticUserIndexCallback, t);
        cm_registerCallbackA(ST_CONCAT(t), CAT_CB_PHONEME,
                                  stb_staticTorrentPhonemeCallback, t);
        ST_DIRECT(t) = 1;
        if (command(ST_ENGINE(t), CMD_CONCATENATIVE))
            stb_postEngineError(t);
        ST_DIRECT(t) = 0;
        if (ST_FLAGS(t) & STF_WORD_STARTS)
            cm_registerCallbackA(ST_CONCAT(t), CAT_CB_WORD_START,
                                      stb_staticWordCallback, t);
        cm_registerCallbackA(ST_CONCAT(t), CAT_CB_BREAK,
                                  stb_staticSynthesisBreakCallback, t);
    } else {
        if (command(ST_ENGINE(t), CMD_PHONEMES_OFF))
            stb_postEngineError(t);
    }

    /* The device belongs to the format, so it comes out of it. */
    ST_SOUND(t) = ((AudioFormat *)ST_OUTFMT(t))->sound;

done:
    sy_mutexRelease(lock);
    return rc;
}

ALIAS("?newAudioFormat@SynthThread@@QAEJPAUECIaudioFormat@@@Z",
      "stf_newAudioFormat");
ALIAS("?equals@AudioFormat@@QAEHPAUECIaudioFormat@@@Z",
      "stf_audioFormatEquals");
ALIAS("?deleteAudioFormat@SynthThread@@QAEJXZ", "stf_deleteAudioFormat");
ALIAS("?registerPhonemeBuffer@SynthThread@@QAEJPAXJ@Z",
      "stf_registerPhonemeBuffer");
ALIAS("?registerSampleBuffer@SynthThread@@QAEJPAFJPAUECIsampleFormat@@@Z",
      "stf_registerSampleBuffer");
