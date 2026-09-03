/* The published interface, as it was before the engine grew a second one.

   Everything a program links against is here. Underneath it there is a newer
   interface, the one whose entry points end in a two, and most of what this
   layer does is hold a record of its own beside that one and hand the call
   on. The record is what the older interface promised: a callback, a queue
   the caller can fill before speaking, eight voices it can edit, and the
   settings that go with them.

   Two things run through nearly every entry point. The first is a guard
   against being called from inside its own callback: a call that arrives
   while another is still running is refused, and the bit it would have used
   is remembered so the layer above can tell what was missed. The second is
   that the answer from underneath is turned into the older interface's own
   notion of an error before anyone sees it.

   This is the first part of that object. Names are prefixed and the aliases
   at the foot carry the real ones. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "eci_synththread.h"
#include "evv_abi.h"
#include "evv_arena.h"
#include "eci_old.h"

/* Which call was refused. Every entry point has its own bit; the ones here
   share the same one because the original gives them the same one. */
#define REFUSED_GENERAL 0x800
#define REFUSED_SYNTH   0x80

/* What the layer underneath answers when the caller has gone away. */
#define POLL_ABORTED   (-18)
/* And the two answers that mean it is still speaking. */
#define POLL_WORKING     1
#define POLL_BUSY        3

/* One thing the caller put on the queue before asking for it to be spoken.
   Only what this file touches is named. */
typedef struct QueueElement {
    int32_t kind;                   /* +0x00, nought means it owns text */
    void   *text;                   /* +0x04 */
    uint8_t pad_08[0xa0 - 0x08];
    struct QueueElement *next;      /* +0xa0 */
} QueueElement;

/* The old interface's own record. Named by offset because most of it still
   belongs to the original. */

extern int32_t STDCALL api_poll(void *h) MANGLED("_eciPoll2@4");
extern int32_t STDCALL api_stop(void *h) MANGLED("_eciStop2@4");
extern int32_t STDCALL api_synthesize(void *h) MANGLED("_eciSynthesize2@4");
extern void STDCALL api_version(int32_t *a, int32_t *b, int32_t *c,
                                  int32_t *d) MANGLED("_eciVersion2@16");
extern int lg_eciGetAvailableLanguages2(uint32_t *out, int *count)
    MANGLED("?eciGetAvailableLanguages2@@YAHPAW4ECILanguageDialect@@PAH@Z");
extern int32_t STDCALL eo_getParam(OldInst *h, int32_t which)
    MANGLED("_eciGetParam@8");
extern int32_t STDCALL api_new(void **out, int32_t language)
    MANGLED("_eciNew2@8");
extern int32_t STDCALL api_delete(void *h2) MANGLED("_eciDelete2@4");
extern int32_t STDCALL api_get_param(void *h2, int32_t k, int32_t p,
                                      int32_t *out) MANGLED("_eciGetParam2@16");
extern void STDCALL api_register_callback(void *h2, void *cb, void *inst,
                                           void *a, void *b)
    MANGLED("_eciRegisterCallback2@20");
extern void *api_get_rom_mngr(void *h2) MANGLED("_eciGetRomMngr2");
extern void *api_get_filter_mngr(void *h2) MANGLED("_eciGetFilterMngr2");
extern int STDCALL ev_setOutputDevice(OldInst *h, int32_t which)
    MANGLED("_eciSetOutputDevice@8");
extern void setRealWorldParamsFromECIParams(void *voice, int32_t which)
    MANGLED("_setRealWorldParamsFromECIParams");
extern THIS void *scv_ctor(void *v)
    MANGLED("??0StandardConcatenativeVoices@@QAE@XZ");

/* Two tables the original keeps to itself. The build makes them visible so
   this file can read them, the same way it already does for two others. */
extern int32_t g_DefaultEnvironment[] MANGLED("_g_DefaultEnvironment");
extern char standardVoices[] MANGLED("_standardVoices");
extern int isUnicodeCodeSet(int32_t bit, int32_t mode)
    MANGLED("_isUnicodeCodeSet");

int32_t setECIerror(int32_t rc, OldInst *h);

/* Declared here because the entry points call one another. The queue that
   the caller fills is still the original's; only emptying it is ours. */
int STDCALL eo_stop(OldInst *h);
int STDCALL eo_speaking(OldInst *h);
int32_t eo_callbackFn(void *inst, int32_t msg, int32_t param, void *data);

/* Refuse a call that arrived while another was still running, and remember
   which one it was. Answers true when the call must not go on. */
static int eo_reentered(OldInst *h, uint32_t bit)
{
    if (!h || !OI_BUSY(h))
        return 0;
    OI_REFUSED(h) = bit;
    OI_REFUSEDALL(h) |= bit;
    return 1;
}

/* How big an instance is, and how big the voice table it carries. */
#define INSTANCE_BYTES  sizeof(OldInst)
#define CONCAT_VOICES_BYTES 0x21db0

/* The environment, and the copy kept beside it so a change can be undone. */

/* The voice in play, its saved copy, and the eight the caller may edit. */

/* Where the standard voices sit: two dialects to a family, sixteen voices to
   a dialect after a word of its own, and the old interface shows eight. */
#define SV_FAMILY_BYTES  0xa08
#define SV_DIALECT_BYTES 0x504
#define SV_FIRST         4

/* ---- turning the newer interface's answers into the older one's ---- */

/* Every refusal the newer interface can give has a bit in the older one's
   word of errors. This is that table, read off the original's own two: a
   byte table from the answer to a case, and a table of cases to bits.
   Answers outside the range, and four inside it, set no bit at all.

   The answer itself is handed straight back, so a caller that looks at the
   number rather than the bits sees what really happened. */
static const uint16_t ERROR_BIT[21] = {
    /* -21 */ 0x4000,
    /* -20 */ 0, 0, 0, 0,
    /* -16 */ 0x0020,
    /* -15 */ 0x0010,
    /* -14 */ 0x4000,
    /* -13 */ 0x0200,
    /* -12 */ 0, 0, 0,
    /*  -9 */ 0x0100,
    /*  -8 */ 0x0080,
    /*  -7 */ 0x0080,
    /*  -6 */ 0x0080,
    /*  -5 */ 0x0020,
    /*  -4 */ 0x0020,
    /*  -3 */ 0x0080,
    /*  -2 */ 0x0002,
    /*  -1 */ 0x0001,
};

int32_t setECIerror(int32_t rc, OldInst *h)
{
    uint32_t at = (uint32_t)(rc + 21);

    if (at <= 20 && ERROR_BIT[at]) {
        OI_REFUSED(h) = ERROR_BIT[at];
        OI_REFUSEDALL(h) |= ERROR_BIT[at];
    }
    return rc;
}

/* ---- the bridge from the engine's callback to the caller's ---- */

/* What the older interface calls each kind of report. */
#define ECI_WAVEFORM      0
#define ECI_PHONEMES      1
#define ECI_INDEX         2
#define ECI_PHONEME_INDEX 3
#define ECI_WORD_INDEX    4
#define ECI_STRING_INDEX  5
#define ECI_AUDIO_INDEX   6
#define ECI_BREAK      0x32

/* And what the caller can answer. */
#define CALLER_ABORT   0
#define CALLER_STOP    2
#define CALLER_TOOK    1

/* What this bridge answers back down. */
#define BRIDGE_ABORT   (-1)
#define BRIDGE_STOP    (-18)

/* Which parameter names the text mode, and the bit that means it is wide. */
#define PARAM_CODESET  9
#define CODESET_WIDE   0x800

/* Where the phoneme report is built before the caller is shown it. */

/* The published callback is stdcall. Calling it as cdecl leaves the stack
   sixteen bytes high and the return lands in data, one call later. */
typedef int32_t (__attribute__((stdcall)) *OldCallback)(void *inst, int32_t msg,
                                                        int32_t param,
                                                        void *data);

/* Tell the caller one thing, and turn its answer into ours. */
static int32_t eo_tell(OldInst *h, void *inst, int32_t msg, int32_t param,
                       int32_t *ret)
{
    int32_t said = ((OldCallback)OI_CALLBACK(h))(inst, msg, param,
                                                 OI_CBDATA(h));

    if (said == CALLER_ABORT)
        *ret = BRIDGE_ABORT;
    else if (said == CALLER_STOP)
        *ret = BRIDGE_STOP;
    return said;
}

/* The engine reports everything here, and this is where it is turned into
   what the older interface promised. The instance the caller knows is the
   one handed back as the data, so it is passed on as the handle. */
int32_t eo_callbackFn(void *inst, int32_t msg, int32_t param, void *data)
{
    OldInst *h = (OldInst *)data;
    int32_t ret = 0;

    switch (msg) {
    case 0:     /* an index mark reached */
        if (h && OI_CALLBACK(h))
            eo_tell(h, inst, ECI_INDEX, param, &ret);
        if (h)
            OI_LASTINDEX(h) = param;
        break;

    case 1:     /* samples, counted in bytes here and in samples there */
        if (h && OI_CALLBACK(h))
            eo_tell(h, inst, ECI_WAVEFORM, param / 2, &ret);
        break;

    case 2:     /* phonemes */
        if (h && OI_CALLBACK(h))
            eo_tell(h, inst, ECI_PHONEMES, param, &ret);
        break;

    case 3: {   /* a phoneme reached, with everything known about it */
        char *rec = (char *)(size_t)param;
        int32_t mode;

        if (!h || !OI_CALLBACK(h))
            break;
        if (!param) {
            OI_REFUSED(h) = 0x10;
            OI_REFUSEDALL(h) |= 0x10;
            break;
        }
        mode = eo_getParam(h, PARAM_CODESET);
        OI_REPORT_MODE(h) = *(int32_t *)(rec + 0x0c);

        /* The name of the phoneme is four characters, wide or narrow
           depending on what the caller asked to be spoken to in. */
        if (isUnicodeCodeSet(CODESET_WIDE, mode)) {
            uint16_t *out = (uint16_t *)OI_REPORT(h);
            int i;

            OI_REPORT_MODE(h) |= CODESET_WIDE;
            for (i = 0; i < 4; i++)
                out[i] = (uint8_t)rec[i];
        } else {
            char *out = OI_REPORT(h);
            int i;

            for (i = 0; i < 4; i++)
                out[i] = rec[i];
        }

        /* And eight more things about it, each a word in the engine's
           record and a byte in the caller's. */
        {
            int i;

            for (i = 0; i < 8; i++)
                OI_REPORT(h)[0x0e + i] = rec[0x10 + i * 4];
        }
        eo_tell(h, inst, ECI_PHONEME_INDEX, (int32_t)(size_t)OI_REPORT(h),
                &ret);
        break;
    }

    case 4:     /* the engine gave up */
        if (h) {
            OI_REFUSED(h) = 0x10;
            OI_REFUSEDALL(h) |= 0x10;
        }
        break;

    case 5:     /* the device gave up */
        if (h) {
            OI_REFUSED(h) = 0x20;
            OI_REFUSEDALL(h) |= 0x20;
        }
        break;

    case 6:     /* a mark was lost */
        if (h) {
            OI_REFUSED(h) = 0x02;
            OI_REFUSEDALL(h) |= 0x02;
        }
        break;

    case 8:     /* the start of a word */
        if (h && OI_CALLBACK(h))
            eo_tell(h, inst, ECI_WORD_INDEX, param, &ret);
        if (h)
            OI_LASTINDEX(h) = param;
        break;

    case 9:     /* the romanizer gave up */
        if (h) {
            OI_REFUSED(h) = 0x1000;
            OI_REFUSEDALL(h) |= 0x1000;
        }
        break;

    case 10:    /* and gave up over a language */
        if (h) {
            OI_REFUSED(h) = 0x4000;
            OI_REFUSEDALL(h) |= 0x4000;
        }
        break;

    case 12:    /* a piece of speech ended */
        if (h && OI_CALLBACK(h))
            eo_tell(h, inst, ECI_BREAK, param, &ret);
        break;

    case 13:    /* an index mark named by a string */
    case 14: {  /* and one naming a piece of audio */
        int32_t which = msg == 13 ? ECI_STRING_INDEX : ECI_AUDIO_INDEX;

        /* The name was copied for the caller and is ours to give back.
           A caller that says it took the name keeps it; one that does not,
           and one that never asked for a callback at all, does not. */
        if (h && OI_CALLBACK(h)) {
            if (eo_tell(h, inst, which, param, &ret) == CALLER_TOOK
                && param) {
                free((void *)(size_t)param);
                param = 0;
            }
            OI_LASTINDEX(h) = param;
        } else if (param) {
            free((void *)(size_t)param);
            param = 0;
        }
        break;
    }

    default:
        break;
    }
    return ret;
}

/* ---- the queue the caller fills before speaking ---- */

/* Throw away everything waiting, and the text any of it owns. */
void eo_clearManualQueue(OldInst *h)
{
    QueueElement *e = OI_QHEAD(h);

    while (e) {
        QueueElement *gone;

        if (e->kind == 0)
            free(e->text);
        gone = e;
        e = e->next;
        free(gone);
    }
    OI_QHEAD(h) = 0;
    OI_QTAIL(h) = 0;
}

/* ---- what a new instance starts out as ---- */

/* Copy the settings every instance starts with. The language is not among
   them: it is asked of the instance itself, because the instance may have
   settled on something other than what was asked for.

   Two of the eighteen settings are left as they were found, which is the
   original's doing rather than an oversight here; they are the two the
   caller has no say in. */
static const uint8_t ENV_COPIED[] = {
    0x00, 0x04, 0x08, 0x0c, 0x14, 0x1c, 0x20, 0x28,
    0x2c, 0x30, 0x34, 0x38, 0x3c, 0x40, 0x44
};

int eo_getDefaultEnvironment(OldInst *h, int32_t check)
{
    size_t i;

    /* Changing anything while it is speaking is refused. */
    if (check && eo_speaking(h)) {
        OI_REFUSED(h) = 0x100;
        OI_REFUSEDALL(h) |= 0x100;
        return 0;
    }

    for (i = 0; i < sizeof ENV_COPIED; i++) {
        int32_t at = ENV_COPIED[i];

        *(int32_t *)((char *)OI_ENV(h) + at) =
            *(int32_t *)((char *)g_DefaultEnvironment + at);
    }

    if (api_get_param(OI_NEW(h), 0, 2, &OI_LANG(h))) {
        OI_REFUSED(h) = 0x80;
        OI_REFUSEDALL(h) |= 0x80;
        return 0;
    }
    memcpy(OI_ENV_SAVED(h), OI_ENV(h), OI_ENV_WORDS * 4);
    return 1;
}

/* And the voice it starts out speaking with. The concatenative side is asked
   first, because a voice it knows about overrides the standard one; a voice
   it has nothing to say about falls back to the table. */
int eo_getDefaultActiveVoice(OldInst *h, int32_t check)
{
    uint8_t family, dialect;
    char *entry;

    if (check && eo_speaking(h)) {
        OI_REFUSED(h) = 0x100;
        OI_REFUSEDALL(h) |= 0x100;
        return 0;
    }

    family = (uint8_t)((OI_LANG(h) & 0xff0000) >> 16);
    dialect = (uint8_t)(OI_LANG(h) & 0xff);

    entry = (char *)OI_CONCAT(h)
          + ((int8_t)family - 1) * 0x1e18
          + (int8_t)dialect * 0xf0c
          + OI_DEVICE(h) * SV_DIALECT_BYTES
          + (OI_VOICENO(h) - 1) * VOICE_BYTES;

    if (*(int32_t *)(entry + 0x44)) {
        memcpy(OI_VOICE(h), entry + SV_FIRST, VOICE_BYTES);
    } else {
        char *std = standardVoices
                  + ((int8_t)family - 1) * SV_FAMILY_BYTES
                  + (int8_t)dialect * SV_DIALECT_BYTES
                  + (OI_VOICENO(h) - 1) * VOICE_BYTES;

        memcpy(OI_VOICE(h), std + SV_FIRST, VOICE_BYTES);
    }

    /* The settings the caller reads back are in its own units, not the
       engine's, so they are converted once here. */
    setRealWorldParamsFromECIParams(OI_VOICE(h), -1);
    memcpy(OI_VOICE_SAVED(h), OI_VOICE(h), VOICE_BYTES);
    return 1;
}

/* ---- making an instance ---- */

/* Both ways of making one, which differ only in where the language comes
   from: one is told, the other takes whatever the settings say.

   Everything after the allocation can fail, and all of it unwinds through
   the same tail. The four attempts at an output device are tried in turn
   because a machine with no sound card should still be able to synthesise
   into a buffer. */
static OldInst *eo_newInstance(int32_t language, int told)
{
    OldInst *h;
    void *voices;
    int failed = 1;
    int i;

    h = (OldInst *)malloc(INSTANCE_BYTES);
    if (!h)
        return 0;
    memset(h, 0, INSTANCE_BYTES);

    voices = cpp_new(CONCAT_VOICES_BYTES);
    OI_CONCAT(h) = voices ? scv_ctor(voices) : 0;
    if (!OI_CONCAT(h)) {
        free(h);
        return 0;
    }

    OI_DEVICE(h) = g_DefaultEnvironment[5];
    if (told)
        OI_LANG(h) = language;
    OI_VOICENO(h) = g_DefaultEnvironment[17];

    if (api_new(&OI_NEW(h), language) == 0 && OI_NEW(h)) {
        failed = 0;
        if (!eo_getDefaultEnvironment(h, 0) || !eo_getDefaultActiveVoice(h, 0)) {
            failed = 1;
        } else {
            uint8_t family = (uint8_t)((OI_LANG(h) & 0xff0000) >> 16);
            uint8_t dialect = (uint8_t)(OI_LANG(h) & 0xff);
            char *base = standardVoices
                       + ((int8_t)family - 1) * SV_FAMILY_BYTES
                       + (int8_t)dialect * SV_DIALECT_BYTES;

            OI_READY(h) = 1;
            OI_READY2(h) = 1;

            /* The eight the caller may edit start as the standard ones but
               under a name that says they are its own. */
            for (i = 0; i < OLD_VOICES; i++) {
                memcpy(OI_VOICES(h) + i * VOICE_BYTES,
                       base + SV_FIRST + i * VOICE_BYTES, VOICE_BYTES);
                strcpy(OI_VOICES(h) + i * VOICE_BYTES, "User-Defined");
            }

            api_register_callback(OI_NEW(h), (void *)eo_callbackFn, h, 0, 0);

            if (!ev_setOutputDevice(h, 0)) {
                OI_DEVICE(h) = 1;
                if (!ev_setOutputDevice(h, 0)) {
                    OI_DEVICE(h) = 0;
                    if (!ev_setOutputDevice(h, 0)) {
                        OI_DEVICE(h) = 3;
                        if (!ev_setOutputDevice(h, 0))
                            failed = 1;
                    }
                }
            }
            OI_ROMMGR(h) = api_get_rom_mngr(OI_NEW(h));
            OI_FILTERMGR(h) = api_get_filter_mngr(OI_NEW(h));
        }
    }

    if (!failed)
        return h;

    if (OI_NEW(h)) {
        api_delete(OI_NEW(h));
        OI_NEW(h) = 0;
    }
    if (OI_CONCAT(h))
        cpp_delete(OI_CONCAT(h));
    free(h);
    return 0;
}

OldInst *STDCALL eo_new(void)
{
    return eo_newInstance(g_DefaultEnvironment[9], 0);
}

OldInst *STDCALL eo_newEx(int32_t language)
{
    return eo_newInstance(language, 1);
}

/* ---- the entry points ---- */

/* Nothing to do: errors are cleared as they are read. */
void STDCALL eo_clearErrors(OldInst *h)
{
    (void)h;
}

/* Nor here: the newer interface has no separate step for this. */
void STDCALL eo_synchronizeSynth(OldInst *h)
{
    (void)h;
}

int STDCALL eo_getAvailableLanguages(uint32_t *out, int *count)
{
    return lg_eciGetAvailableLanguages2(out, count);
}

/* The last index mark the callback was told about, kept so a caller that did
   not want a callback can ask instead. */
int32_t STDCALL eo_getIndex(OldInst *h)
{
    if (!h)
        return 0;
    return OI_LASTINDEX(h);
}

/* Where the caller's own callback is put. Nothing else happens: the bridge
   between it and the engine's was installed when the instance was made. */
void STDCALL eo_registerCallback(OldInst *h, void *cb, void *data)
{
    if (eo_reentered(h, REFUSED_GENERAL))
        return;
    if (!h)
        return;
    OI_CALLBACK(h) = cb;
    OI_CBDATA(h) = data;
}

/* Throw away what has been given but not yet spoken. */
int STDCALL eo_clearInput(OldInst *h)
{
    if (eo_reentered(h, REFUSED_GENERAL))
        return 0;
    if (!h)
        return 0;
    eo_clearManualQueue(h);
    return 1;
}

/* Stop speaking and forget what was queued. Answering that it worked is not
   the same as the engine agreeing: a refusal from underneath leaves the
   instance marked not busy and answers false. */
int STDCALL eo_stop(OldInst *h)
{
    if (eo_reentered(h, REFUSED_GENERAL))
        return 0;
    if (h)
        OI_BUSY(h) = 1;
    if (!h)
        return 0;

    eo_clearManualQueue(h);
    if (setECIerror(api_stop(OI_NEW(h)), h)) {
        OI_BUSY(h) = 0;
        return 0;
    }
    OI_STOPPED(h) = 1;
    OI_BUSY(h) = 0;
    return 1;
}

/* eciSynthesize is not here. It has to push the caller's own queue across
   first, and the routine that does that is one the original keeps to
   itself, so nothing outside its object can reach it. It comes with the
   text path, which is where that queue is built. */

/* Is it still speaking? Asking is also what drives the queue of results, so
   a caller that never asks never hears anything.

   A caller that has gone away is answered by stopping outright. */
int STDCALL eo_speaking(OldInst *h)
{
    int32_t rc;

    if (eo_reentered(h, REFUSED_GENERAL))
        return 0;
    if (h)
        OI_BUSY(h) = 1;
    if (!h)
        return 0;

    rc = setECIerror(api_poll(OI_NEW(h)), h);
    OI_BUSY(h) = 0;
    if (rc == POLL_ABORTED) {
        eo_stop(h);
        return 0;
    }
    return rc == POLL_BUSY || rc == POLL_WORKING;
}

/* The version, as four numbers in a string. */
void STDCALL eo_version(char *out)
{
    int32_t a = 0, b = 0, c = 0, d = 0;

    if (!out)
        return;
    api_version(&a, &b, &c, &d);
    sprintf(out, "%d.%d.%d.%d", a, b, c, d);
}

ALIAS_N("_eciClearErrors@4", "eo_clearErrors", 4);
ALIAS_N("_eciSynchronizeSynth@4", "eo_synchronizeSynth", 4);
ALIAS_N("_eciGetAvailableLanguages@8", "eo_getAvailableLanguages", 8);
ALIAS_N("_eciGetIndex@4", "eo_getIndex", 4);
ALIAS_N("_eciRegisterCallback@12", "eo_registerCallback", 12);
ALIAS_N("_eciClearInput@4", "eo_clearInput", 4);
ALIAS_N("_eciStop@4", "eo_stop", 4);
ALIAS_N("_eciSpeaking@4", "eo_speaking", 4);
ALIAS_N("_eciVersion@4", "eo_version", 4);
ALIAS("?clearManualQueue@@YAXPAUoldECIInstData@@@Z", "eo_clearManualQueue");
ALIAS("?setECIerror@@YAJJPAUoldECIInstData@@@Z", "setECIerror");
ALIAS("?eciCallbackFn@@YAJPAXJJ0@Z", "eo_callbackFn");
ALIAS("?getDefaultEnvironment@@YAHPAUoldECIInstData@@H@Z",
      "eo_getDefaultEnvironment");
ALIAS("?getDefaultActiveVoice@@YAHPAUoldECIInstData@@H@Z",
      "eo_getDefaultActiveVoice");
ALIAS_N("_eciNew@0", "eo_new", 0);
ALIAS_N("_eciNewEx@4", "eo_newEx", 4);
