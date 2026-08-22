/* The environment and the parameters, as the older interface keeps them.

   An instance holds three copies of the same eighteen settings. The live one
   is what the caller has asked for. The saved one is what the layer beneath
   has actually been told. And a third, the standard tables, is where both
   started. Nearly everything here is the business of noticing that the first
   two have drifted apart and closing the gap one setting at a time.

   Four of the eighteen are not settings at all but the shape of the sound:
   which rate, and three numbers that go with the device. Changing any of them
   means building an audio format afresh and handing it down, which is why the
   output setters live here beside the rest.

   Names are prefixed and the aliases at the foot carry the real ones. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "eci_synththread.h"
#include "evv_abi.h"
#include "eci_old.h"

/* Where the samples are going. */
#define WHERE_DEVICE    0
#define WHERE_SAMPLES   1
#define WHERE_PHONEMES  2

/* The environment's own eighteen, by the index the caller uses. */
#define ENV_SYNTHMODE   0
#define ENV_INPUTTYPE   1
#define ENV_TEXTMODE    2
#define ENV_DICTIONARY  3
#define ENV_RATE        5
#define ENV_WANTPHONEMES 7
#define ENV_LANGUAGE    9
#define ENV_NUMBERMODE  10
#define ENV_ROMAN       12
#define ENV_ENV_FIRST   13
#define ENV_ENV_LAST    16

/* The environment and the voice both travel by value, which is how the
   original's callers push them. */
typedef struct Environment { int32_t w[0x12]; } Environment;
typedef struct ECIVoice { int32_t w[0x14]; } ECIVoice;

/* The eight kinds of voice the older interface will move, and where they sit
   in a voice record: three words of name first. */
#define VOICE_FIRST_PARAM 5
#define VOICE_LAST_PARAM  12
#define VOICE_HEAD        0x0c
#define VOICE_WORDS       0x14
#define ENV_WORDS         0x12

/* The concatenative table, by family, dialect, rate and voice. */
#define CV_FAMILY_BYTES  0x1e18
#define CV_DIALECT_BYTES 0x0f0c
#define CV_RATE_BYTES    0x0504
#define CV_VOICE_BYTES   0x0050
#define CV_PRESENT       0x44
#define SV_FAMILY_BYTES  0x0a08
#define SV_DIALECT_BYTES 0x0504
#define SV_FIRST         4

extern int32_t STDCALL api_set_param(void *h2, int32_t k, int32_t p,
                                      int32_t v) MANGLED("_eciSetParam2@16");
extern int32_t STDCALL api_register_samples(void *h2, void *buf,
                                                  int32_t bytes, void *fmt)
    MANGLED("_eciRegisterSampleBuffer2@16");
extern int32_t STDCALL api_register_phonemes(void *h2, void *buf,
                                                   int32_t n, int32_t kind)
    MANGLED("_eciRegisterPhonemeBuffer2@16");
extern int32_t STDCALL api_new_audio_format(void *h2, void *fmt)
    MANGLED("_eciNewAudioFormat2@8");
extern int32_t STDCALL api_delete_audio_format(void *h2)
    MANGLED("_eciDeleteAudioFormat2@4");
extern int ealQueryDevCaps(int32_t dev, int32_t kind, int32_t *n, void *out)
    MANGLED("_ealQueryDevCaps");
extern char standardVoices[] MANGLED("_standardVoices");
extern int32_t g_DefaultEnvironment[] MANGLED("_g_DefaultEnvironment");

extern int32_t setECIerror(int32_t rc, OldInst *h);

/* ---- how much of each setting the caller may ask for ---------------- */

/* Read out of the original's own table. A pair to a setting: the least it
   will take and the most. */
const int32_t ev_paramRange[0x12][2] = {
    { 0, 1 }, { 0, 1 }, { 0, 3 }, { 0, 1 },
    { 0, 100 }, { 0, 2 }, { 0, 100 }, { 0, 1 },
    { 0, 1 }, { 0, 0x7fffffff }, { 0, 1 }, { 0, 1 },
    { 0, 1 }, { 2, 0x7fffffff }, { 220, 0x7fffffff }, { 0, 0x7fffffff },
    { 220, 0x7fffffff }, { 0, 0 },
};

/* The same for a voice, in the caller's units and in the engine's. Kept here
   with their neighbour because they come out of one table. */
const int32_t ev_voiceParamRange[8][2] = {
    { 0, 1 }, { 0, 100 }, { 0, 100 }, { 0, 100 },
    { 0, 100 }, { 0, 100 }, { 0, 250 }, { 0, 100 },
};

const int32_t ev_realWorldVoiceParamRange[8][2] = {
    { 0, 1 }, { 1, 0xffff }, { 40, 422 }, { 1, 0xffff },
    { 1, 0xffff }, { 1, 0xffff }, { 1, 0xffff }, { 1, 0xffff },
};

/* ---- what the machine will actually do ------------------------------ */

/* Whether a language may be asked for at all. In the original the
   comparison is settled when the object is compiled -- 0x10000 in the
   English module, 0x40000 in the German one -- because a library was one
   language. Here it is every language linked in, asked in turn; the arms
   for the four that group dialects together are kept because the original
   keeps them. */
#include "delta_lang.h"

static int checkone(int32_t have, int32_t want)
{
    switch (have) {
    case 0x60000:
        return want == 0x60000 || want == 0x60100 || want == 0x60800;
    case 0x60001:
        return want == 0x60001 || want == 0x60101 || want == 0x60201
            || want == 0x60801;
    case 0x80000:
        return want == 0x80000 || want == 0x80100 || want == 0x80200
            || want == 0x80300 || want == 0x80400 || want == 0x80800;
    case 0xa0000:
        return want == 0xa0000 || want == 0xa0800;
    default:
        return want == have;
    }
}

int ev_checklang(int32_t want)
{
    int i;

    for (i = 0; delta_languages[i] != 0; i++)
        if (checkone(delta_languages[i]->id, want))
            return 1;
    return 0;
}

/* Whether the device will give us a rate. The four it knows about each have
   a bit in the word the device answers with. */
int ev_sampleRateSupported(int32_t rate)
{
    int32_t caps[6];
    int32_t room = 0x10;

    if (ealQueryDevCaps(0, 1, &room, caps) != 0)
        return 0;

    switch (rate) {
    case 0:  return (caps[5] & 0x00001) != 0;
    case 1:  return (caps[5] & 0x00010) != 0;
    case 2:  return (caps[5] & 0x00100) != 0;
    case 3:  return (caps[5] & 0x10000) != 0;
    default: return 0;
    }
}

/* ---- the shape of the sound ----------------------------------------- */

/* What an audio format says. The last four are the device's own business
   and only the device form carries them. */
typedef struct AudioFormat {
    int32_t  kind;          /* +0x00 */
    int32_t  hz;            /* +0x04 */
    int32_t  flags;         /* +0x08 */
    char    *filename;      /* +0x0c */
    int32_t  a, b, c, d;    /* +0x10 */
} AudioFormat;

/* The four rates the device form knows, and the three the sample form does.
   Out of range leaves nought, which the layer beneath refuses. */
static int32_t ev_deviceHz(int32_t rate)
{
    switch (rate) {
    case 0:  return 8000;
    case 1:  return 11025;
    case 2:  return 22050;
    case 3:  return 16000;
    default: return 0;
    }
}

static int32_t ev_callbackHz(int32_t rate)
{
    switch (rate) {
    case 0:  return 8000;
    case 1:  return 11025;
    case 2:  return 22050;
    default: return 0;
    }
}

/* Send the samples to the device. Whatever they were going to before has to
   be let go of first, and only then is the new format built. */
int ev_setOutputToDevice(OldInst *h, int32_t rate, int32_t a, int32_t b,
                         int32_t c, int32_t d)
{
    AudioFormat fmt;

    if (OI_WHERE(h) == WHERE_SAMPLES) {
        if (setECIerror(api_register_samples(OI_NEW(h), 0, 0, 0), h))
            return 0;
        OI_SAMPROOM(h) = 0;
        OI_SAMPBUF(h) = 0;
    } else if (OI_WHERE(h) == WHERE_PHONEMES) {
        if (setECIerror(api_register_phonemes(OI_NEW(h), 0, 0, 7), h))
            return 0;
    }

    memset(&fmt, 0, sizeof fmt);
    fmt.kind = 0;
    fmt.filename = OI_FILENAME(h);
    fmt.a = a;
    fmt.b = b;
    fmt.c = c;
    fmt.d = d;
    fmt.hz = ev_deviceHz(rate);
    fmt.flags = 0;

    if (setECIerror(api_new_audio_format(OI_NEW(h), &fmt), h))
        return 0;
    OI_WHERE(h) = WHERE_DEVICE;
    return 1;
}

/* Send them to the caller's own buffer instead. */
int ev_setOutputToSampleCallback(OldInst *h, int32_t rate)
{
    AudioFormat fmt;

    if (OI_WHERE(h) == WHERE_DEVICE) {
        if (setECIerror(api_delete_audio_format(OI_NEW(h)), h))
            return 0;
    } else if (OI_WHERE(h) == WHERE_PHONEMES) {
        if (setECIerror(api_register_phonemes(OI_NEW(h), 0, 0, 7), h))
            return 0;
    }

    memset(&fmt, 0, 0x0c);
    fmt.kind = 0;
    fmt.hz = ev_callbackHz(rate);
    fmt.flags = 0;

    /* The room is counted in samples here and in bytes underneath. */
    if (setECIerror(api_register_samples(OI_NEW(h), OI_SAMPBUF(h),
                                             OI_SAMPROOM(h) * 2, &fmt), h))
        return 0;
    OI_WHERE(h) = WHERE_SAMPLES;
    return 1;
}

/* Or report phonemes rather than sound at all. */
int ev_setOutputToPhonemeCallback(OldInst *h, int32_t n, void *buf)
{
    if (OI_WHERE(h) == WHERE_DEVICE) {
        if (setECIerror(api_delete_audio_format(OI_NEW(h)), h))
            return 0;
    } else if (OI_WHERE(h) == WHERE_SAMPLES) {
        if (setECIerror(api_register_samples(OI_NEW(h), 0, 0, 0), h))
            return 0;
    }

    if (setECIerror(api_register_phonemes(OI_NEW(h), buf, n, 7), h))
        return 0;
    OI_WHERE(h) = WHERE_PHONEMES;
    return 1;
}

/* ---- putting things by while the output is rebuilt ------------------ */

void ev_saveInstanceData(OldInst *h)
{
    strcpy(OI_FILENAME_SAVED(h), OI_FILENAME(h));
    OI_SAMPROOM_SAVED(h) = OI_SAMPROOM(h);
    OI_SAMPBUF_SAVED(h) = OI_SAMPBUF(h);
}

void ev_restoreInstanceData(OldInst *h)
{
    strcpy(OI_FILENAME(h), OI_FILENAME_SAVED(h));
    OI_SAMPROOM(h) = OI_SAMPROOM_SAVED(h);
    OI_SAMPBUF(h) = OI_SAMPBUF_SAVED(h);
}

/* ---- closing the gap between what was asked and what was sent ------- */

/* Six of the settings put back as they stand, whatever the saved copy says.
   The second argument is not looked at. */
int ev_setDefaultEnvironment(OldInst *h, int32_t unused)
{
    static const struct { int env; int param; } SEND[6] = {
        { 2, 0 }, { 3, 3 }, { 7, 4 }, { 10, 1 }, { 11, 0x0d }, { 12, 0x0e },
    };
    int i;

    (void)unused;
    for (i = 0; i < 6; i++) {
        int32_t rc = api_set_param(OI_NEW(h), 0, SEND[i].param,
                                  OI_ENV(h)[SEND[i].env]);
        if (setECIerror(rc, h))
            return 0;
    }
    return 1;
}

/* The eight voice settings, each sent only if it has moved. */
int ev_sendChangedActiveVoice(OldInst *h, ECIVoice v, int32_t force)
{
    int32_t err = 0;
    int i;

    for (i = VOICE_FIRST_PARAM; i < VOICE_LAST_PARAM + 1 && err == 0; i++) {
        int32_t *want = (int32_t *)((char *)&v + VOICE_HEAD) + i;
        int32_t *sent = (int32_t *)(OI_VOICE_SAVED(h) + VOICE_HEAD) + i;

        if (!force && *want == *sent)
            continue;
        err = setECIerror(api_set_param(OI_NEW(h), 1, i, *want), h);
        *sent = *want;
    }
    return err == 0;
}

/* The environment, in the order the original sends it. The rate and the
   three numbers that go with it move together, because all four go into one
   audio format; the rest go one at a time.

   The language is sent twice. After the first it matches the saved copy, so
   the second does nothing unless everything is being forced. */
int ev_sendChangedEnvironment(OldInst *h, Environment env, int32_t force)
{
    static const struct { int env; int param; } SEND[] = {
        { 17, 0x10 }, { 2, 0 }, { 3, 3 }, { 11, 0x0d }, { 7, 4 },
        { 9, 2 }, { 10, 1 },
    };
    const int32_t *e = env.w;
    int32_t *sent = OI_ENV_SAVED(h);
    int32_t langMoved = 0;
    int i;

    if (force
        || e[ENV_RATE] != sent[ENV_RATE]
        || e[13] != sent[13] || e[14] != sent[14]
        || e[15] != sent[15] || e[16] != sent[16]) {
        if (OI_WHERE(h) == WHERE_DEVICE) {
            if (ev_setOutputToDevice(h, OI_RATE(h), OI_ENV(h)[13],
                                     OI_ENV(h)[14], OI_ENV(h)[15],
                                     OI_ENV(h)[16])) {
                sent[ENV_RATE] = e[ENV_RATE];
                sent[13] = e[13];
                sent[14] = e[14];
                sent[15] = e[15];
                sent[16] = e[16];
            }
        } else if (OI_WHERE(h) == WHERE_SAMPLES) {
            if (ev_setOutputToSampleCallback(h, e[ENV_RATE]))
                sent[ENV_RATE] = e[ENV_RATE];
        }
    }

    if (OI_READY2(h) == 1) {
        ev_setDefaultEnvironment(h, 0);
        OI_READY2(h) = 0;
    }

    /* The language first and on its own, because a change of family has to
       be noticed before the romanizer setting is decided. */
    if (force || e[ENV_LANGUAGE] != sent[ENV_LANGUAGE]) {
        if (setECIerror(api_set_param(OI_NEW(h), 0, 2, e[ENV_LANGUAGE]), h))
            return 0;
        if (((sent[ENV_LANGUAGE] & 0xff0000) >> 16)
            != ((e[ENV_LANGUAGE] & 0xff0000) >> 16))
            langMoved = 1;
        sent[ENV_LANGUAGE] = e[ENV_LANGUAGE];
    }

    for (i = 0; i < (int)(sizeof SEND / sizeof SEND[0]); i++) {
        int n = SEND[i].env;

        if (!force && e[n] == sent[n])
            continue;
        if (setECIerror(api_set_param(OI_NEW(h), 0, SEND[i].param, e[n]), h))
            return 0;
        sent[n] = e[n];
    }

    /* And the romanizer last, forced as well when the family moved. */
    if (force || langMoved || e[ENV_ROMAN] != sent[ENV_ROMAN]) {
        if (setECIerror(api_set_param(OI_NEW(h), 0, 0x0e, e[ENV_ROMAN]), h))
            return 0;
        sent[ENV_ROMAN] = e[ENV_ROMAN];
    }
    return 1;
}

/* Everything at once, forced the first time round. */
int ev_sendParameters(OldInst *h)
{
    int32_t force = OI_READY(h) ? 1 : 0;

    if (!ev_sendChangedEnvironment(h, *(Environment *)OI_ENV(h), force))
        return 0;
    if (!ev_sendChangedActiveVoice(h, *(ECIVoice *)OI_VOICE(h), force))
        return 0;
    if (force)
        OI_READY(h) = 0;
    return 1;
}

/* ---- the entry point ------------------------------------------------ */

/* One entry in the concatenative table, or nought if that voice is not in
   it at this rate. */
static char *ev_concatVoice(OldInst *h, int family, int dialect, int32_t rate)
{
    char *p = (char *)OI_CONCAT(h);

    p += (family - 1) * CV_FAMILY_BYTES;
    p += dialect * CV_DIALECT_BYTES;
    p += rate * CV_RATE_BYTES;
    p += (OI_VOICENO(h) - 1) * CV_VOICE_BYTES;
    return p;
}

static char *ev_standardVoice(OldInst *h, int family, int dialect)
{
    return standardVoices
        + (family - 1) * SV_FAMILY_BYTES
        + dialect * SV_DIALECT_BYTES
        + (OI_VOICENO(h) - 1) * CV_VOICE_BYTES;
}

/* Set one of the eighteen and answer with what it was.

   The answer is the old value, or minus one when the setting could not be
   made. The dictionary setting is the one place where the caller's sense of
   the number is the opposite of the engine's, so it is turned round on the
   way in and the old value turned back on the way out. */
int32_t STDCALL ev_setParam(OldInst *h, int32_t which, int32_t value)
{
    OldInst *inst;
    int32_t v;
    int32_t old = -1;
    int accepted;

    if (h && OI_BUSY(h)) {
        OI_REFUSED(h) = 0x800;
        OI_REFUSEDALL(h) |= 0x800;
        return -1;
    }

    inst = h;
    v = value;
    if (!inst)
        goto done;

    if (which >= 0x11)
        return -1;
    if (which == 0x0b)
        return -1;
    if (which < 0 || which >= 0x12)
        return -1;
    if (value < ev_paramRange[which][0] || value > ev_paramRange[which][1])
        return -1;

    accepted = 1;
    if (which == ENV_LANGUAGE) {
        accepted = ev_checklang(value);
        if (accepted) {
            old = OI_ENV(inst)[which];
            OI_ENV(inst)[which] = v;
        } else {
            old = -1;
        }
        /* Either way the language is finished with here. */
        accepted = 0;
    }
    if (!accepted)
        goto done;

    old = OI_ENV(inst)[which];

    if (which == ENV_DICTIONARY)
        v = (value == 1) ? 0 : 1;

    if (which == ENV_RATE) {
        /* Wherever the samples are going now, they have to go on going
           there. Rebuilding as a device regardless, which is what IBM's
           engine does, loses a buffer registered with eciSetOutputBuffer:
           ev_setOutputToDevice hands the engine a null one on its way past
           WHERE_SAMPLES, and the instance reports the new rate ever after and
           answers no more samples. ev_sendChangedEnvironment has always
           chosen on OI_WHERE; this is the same choice. */
        int made = 1;

        if (OI_WHERE(inst) == WHERE_DEVICE)
            made = ev_setOutputToDevice(inst, value, OI_ENV(inst)[13],
                                        OI_ENV(inst)[14], OI_ENV(inst)[15],
                                        OI_ENV(inst)[16]);
        else if (OI_WHERE(inst) == WHERE_SAMPLES)
            made = ev_setOutputToSampleCallback(inst, value);

        if (made)
            OI_ENV(inst)[which] = value;
        else
            old = -1;
    }

    /* A rate the device would not take leaves the old one in place, and the
       voice has to be fetched again for whichever rate is really in force. */
    if (which == ENV_RATE && OI_ENV(inst)[which] != value) {
        int family = (int8_t)((OI_LANG(inst) & 0xff0000) >> 16);
        int dialect = (int8_t)(OI_LANG(inst) & 0xff);
        int32_t rate = OI_RATE(inst);
        int useConcat = 1;

        if (*(int32_t *)(ev_concatVoice(inst, family, dialect, rate)
                         + CV_PRESENT) == 0
            && *(int32_t *)(ev_concatVoice(inst, family, dialect, value)
                            + CV_PRESENT) == 0)
            useConcat = 0;

        if (useConcat) {
            char *entry = ev_concatVoice(inst, family, dialect, value);

            if (*(int32_t *)(entry + CV_PRESENT) != 0)
                memcpy(OI_VOICE(inst), entry + SV_FIRST, VOICE_WORDS * 4);
            else
                memcpy(OI_VOICE(inst),
                       ev_standardVoice(inst, family, dialect) + SV_FIRST,
                       VOICE_WORDS * 4);
        }
    }

    if (which >= ENV_ENV_FIRST && which <= ENV_ENV_LAST) {
        int32_t n[4];

        n[0] = OI_ENV(inst)[13];
        n[1] = OI_ENV(inst)[14];
        n[2] = OI_ENV(inst)[15];
        n[3] = OI_ENV(inst)[16];
        n[which - ENV_ENV_FIRST] = value;

        /* These four describe a device and go into the same audio format the
           rate does, so rebuilding regardless lost a registered buffer here
           too -- and here there is nothing to weigh against it, since the
           sample form is built from the rate alone and never reads them. So
           where the samples are not going to a device the number is recorded
           and nothing is rebuilt. */
        old = -1;
        if (OI_WHERE(inst) != WHERE_DEVICE) {
            OI_ENV(inst)[which] = value;
            old = 0;
        } else if (ev_setOutputToDevice(inst, OI_RATE(inst), n[0], n[1],
                                        n[2], n[3])) {
            OI_ENV(inst)[which] = value;
            old = 0;
        }
    } else {
        OI_ENV(inst)[which] = v;
    }

done:
    if (which == ENV_DICTIONARY) {
        if (old == 1)
            old = 0;
        else if (old == 0)
            old = 1;
    }
    return old;
}

/* ---- the two ways out ----------------------------------------------- */

/* Refuse a call that arrived while another was still running. */
static int ev_reentered(OldInst *h)
{
    if (!h || !OI_BUSY(h))
        return 0;
    OI_REFUSED(h) = 0x800;
    OI_REFUSEDALL(h) |= 0x800;
    return 1;
}

/* Send the sound to a device, named by its number. The name is what the
   layer beneath opens, so it is written into the instance before the format
   is built, and put back if the format is refused. */
int STDCALL ev_setOutputDevice(OldInst *h, int32_t dev)
{
    OldInst *inst;
    int32_t n;

    if (ev_reentered(h))
        return 0;

    inst = h;
    n = (dev > 0) ? dev : 0;
    if (!inst)
        return 0;

    ev_saveInstanceData(inst);
    sprintf(OI_FILENAME(inst), "%d", n);
    if (!ev_setOutputToDevice(inst, OI_RATE(inst), OI_ENV(inst)[13],
                              OI_ENV(inst)[14], OI_ENV(inst)[15],
                              OI_ENV(inst)[16])) {
        ev_restoreInstanceData(inst);
        return 0;
    }

    OI_ENV_SAVED(inst)[ENV_RATE] = OI_RATE(inst);
    OI_ENV_SAVED(inst)[13] = OI_ENV(inst)[13];
    OI_ENV_SAVED(inst)[14] = OI_ENV(inst)[14];
    OI_ENV_SAVED(inst)[15] = OI_ENV(inst)[15];
    OI_ENV_SAVED(inst)[16] = OI_ENV(inst)[16];
    return 1;
}

/* Or into the caller's own buffer. Asking for no buffer at all means going
   back to the device at whatever rate the defaults name. */
int STDCALL ev_setOutputBuffer(OldInst *h, int32_t n, void *buf)
{
    OldInst *inst;

    if (ev_reentered(h))
        return 0;

    inst = h;
    if (!inst)
        return 0;
    if (OI_CALLBACK(inst) == 0)
        return 0;

    if (n == 0 || buf == 0) {
        OI_RATE(inst) = g_DefaultEnvironment[ENV_RATE];
        return ev_setOutputDevice(inst, 0) ? 1 : 0;
    }

    ev_saveInstanceData(inst);
    OI_SAMPBUF(inst) = buf;
    OI_SAMPROOM(inst) = n;
    if (!ev_setOutputToSampleCallback(inst, OI_RATE(inst))) {
        ev_restoreInstanceData(inst);
        return 0;
    }
    OI_ENV_SAVED(inst)[ENV_RATE] = OI_RATE(inst);
    return 1;
}

ALIAS("?checklang@@YAHH@Z", "ev_checklang");
ALIAS("?sampleRateSupported@@YAHH@Z", "ev_sampleRateSupported");
ALIAS("?saveInstanceData@@YAXPAUoldECIInstData@@@Z", "ev_saveInstanceData");
ALIAS("?restoreInstanceData@@YAXPAUoldECIInstData@@@Z",
      "ev_restoreInstanceData");
ALIAS("?setDefaultEnvironment@@YAHPAUoldECIInstData@@H@Z",
      "ev_setDefaultEnvironment");
ALIAS("?setOutputToDevice@@YAHPAUoldECIInstData@@HHHHH@Z",
      "ev_setOutputToDevice");
ALIAS("?setOutputToSampleCallback@@YAHPAUoldECIInstData@@H@Z",
      "ev_setOutputToSampleCallback");
ALIAS("?setOutputToPhonemeCallback@@YAHPAUoldECIInstData@@HPAX@Z",
      "ev_setOutputToPhonemeCallback");
ALIAS("?sendChangedEnvironment@@YAHPAUoldECIInstData@@UEnvironment@@H@Z",
      "ev_sendChangedEnvironment");
ALIAS("?sendChangedActiveVoice@@YAHPAUoldECIInstData@@UECIVoice@@H@Z",
      "ev_sendChangedActiveVoice");
ALIAS("?sendParameters@@YAHPAUoldECIInstData@@@Z", "ev_sendParameters");
ALIAS_N("_eciSetParam@12", "ev_setParam", 12);
ALIAS_N("_eciSetOutputDevice@8", "ev_setOutputDevice", 8);
ALIAS_N("_eciSetOutputBuffer@12", "ev_setOutputBuffer", 12);
