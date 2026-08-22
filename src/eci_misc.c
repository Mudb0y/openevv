/* The rest of the older interface: the calls that are one thing each.

   Ending an instance, putting one back the way it started, reading and
   writing the defaults that a new instance inherits, the shorthand that
   speaks a string and tidies up after itself, and a handful of calls that
   were published and then never implemented.

   Those last are not oversights on my part. Eight entry points in this
   object do nothing whatever: two return one, four return nought, and two
   simply return. Logging, licensing, the dialog box and the error message
   are all of that kind. They are transcribed as they stand so that a program
   linking against this still finds them.

   Names are prefixed and the aliases at the foot carry the real ones. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "eci_synththread.h"
#include "evv_abi.h"
#include "evv_arena.h"
#include "eci_old.h"

#define ENV_DICTIONARY  3
#define ENV_RATE        5
#define ENV_LANGUAGE    9
#define ENV_WORDS       0x12
#define SV_FAMILY_BYTES 0x0a08
#define SV_DIALECT_BYTES 0x0504
#define SV_FIRST        4

/* What the layer beneath answers when the caller has gone away. */
#define POLL_ABORTED    (-18)

extern int32_t STDCALL api_reset(void *h2, int32_t language)
    MANGLED("_eciReset2@8");
extern int32_t STDCALL api_delete(void *h2) MANGLED("_eciDelete2@4");
extern int32_t STDCALL api_synchronize(void *h2)
    MANGLED("_eciSynchronize2@4");
extern int32_t STDCALL api_pause(void *h2, int32_t on)
    MANGLED("_eciPause2@8");
extern int FilterText2(OldInst *h, void *a, void *b, void *c)
    MANGLED("_FilterText2");
extern int isUnicodeCodeSet(int32_t bit, int32_t mode)
    MANGLED("_isUnicodeCodeSet");
extern int MBCSConverter(OldInst *h, const char *text, uint16_t **out)
    MANGLED("_MBCSConverter");
extern char standardVoices[] MANGLED("_standardVoices");
extern int32_t g_DefaultEnvironment[] MANGLED("_g_DefaultEnvironment");

/* Entry points of the same interface that these ones lean on. */
extern OldInst *STDCALL eo_newEx(int32_t language) MANGLED("_eciNewEx@4");
extern int STDCALL eo_stop(OldInst *h) MANGLED("_eciStop@4");
extern int STDCALL et_addText(OldInst *h, const char *text)
    MANGLED("_eciAddText@8");
extern int STDCALL et_synthesize(OldInst *h) MANGLED("_eciSynthesize@4");
extern int32_t STDCALL ev_setParam(OldInst *h, int32_t which, int32_t v)
    MANGLED("_eciSetParam@12");
extern int STDCALL vc_copyVoice(OldInst *h, int32_t from, int32_t to)
    MANGLED("_eciCopyVoice@12");

extern int32_t setECIerror(int32_t rc, OldInst *h);
extern void eo_clearManualQueue(OldInst *h);
extern int eo_getDefaultEnvironment(OldInst *h, int32_t which);
extern int eo_getDefaultActiveVoice(OldInst *h, int32_t which);
extern int ev_checklang(int32_t want);
extern int ev_sampleRateSupported(int32_t rate);
extern void ev_saveInstanceData(OldInst *h);
extern void ev_restoreInstanceData(OldInst *h);
extern int ev_setOutputToDevice(OldInst *h, int32_t rate, int32_t a,
                                int32_t b, int32_t c, int32_t d);
extern const int32_t ev_paramRange[0x12][2];

static int es_reentered(OldInst *h)
{
    if (!h || !OI_BUSY(h))
        return 0;
    OI_REFUSED(h) = 0x800;
    OI_REFUSEDALL(h) |= 0x800;
    return 1;
}

/* The dictionary setting is the one the caller and the engine disagree
   about, so it is turned round every time it crosses. */
static int32_t es_flipDictionary(int32_t v)
{
    if (v == 0)
        return 1;
    return 0;
}

/* ---- reading and writing settings ----------------------------------- */

int32_t STDCALL eo_getParam(OldInst *h, int32_t which)
{
    OldInst *inst = h;
    int32_t v = -1;

    if (!inst)
        return v;
    if (which >= 0x11)
        return v;
    if (which == 0x0b)
        return v;
    if (which < 0 || which >= ENV_WORDS)
        return v;

    v = OI_ENV(inst)[which];
    if (which == ENV_DICTIONARY)
        v = es_flipDictionary(v);
    return v;
}

/* Which language a new instance starts in. The original writes the number
   straight in here, because a library was one language; here it is the
   first one linked in, which is the one the build calls its default. */
#include "delta_lang.h"

/* The same, but of the defaults a new instance would start from. A language
   of nought means none has been chosen, and the answer is the one built
   in. */
int32_t STDCALL es_getDefaultParam(int32_t which)
{
    int32_t v = -1;

    if (which < 0 || which >= ENV_WORDS)
        return v;

    v = g_DefaultEnvironment[which];
    if (which == ENV_DICTIONARY)
        v = es_flipDictionary(v);
    if (v == 0 && which == ENV_LANGUAGE)
        return delta_languages[0]->id;
    return v;
}

int32_t STDCALL es_setDefaultParam(int32_t which, int32_t value)
{
    int32_t v = value;
    int32_t old = -1;
    int accepted;

    if (which < 0 || which >= ENV_WORDS)
        return -1;
    if (value < ev_paramRange[which][0] || value > ev_paramRange[which][1])
        return -1;

    accepted = 1;
    if (which == ENV_LANGUAGE) {
        accepted = ev_checklang(value);
        if (accepted) {
            old = g_DefaultEnvironment[which];
            g_DefaultEnvironment[which] = v;
        } else {
            old = -1;
        }
        /* Either way the language is finished with here. */
        accepted = 0;
    }
    if (!accepted)
        return old;

    old = g_DefaultEnvironment[which];

    if (which == ENV_DICTIONARY) {
        v = (value == 1) ? 0 : 1;
        old = es_flipDictionary(old);
        g_DefaultEnvironment[which] = v;
        return old;
    }

    if (which == ENV_RATE) {
        if (ev_sampleRateSupported(v))
            g_DefaultEnvironment[which] = v;
        else
            old = -1;
        return old;
    }

    g_DefaultEnvironment[which] = v;
    return old;
}

/* ---- waiting, pausing, ending --------------------------------------- */

/* Wait for everything queued to be spoken. The instance is marked busy for
   the whole wait, so a callback that tries to call back in is refused. */
int STDCALL es_synchronize(OldInst *h)
{
    OldInst *inst;
    int32_t rc;
    int ok;

    if (es_reentered(h))
        return 0;
    if (h)
        OI_BUSY(h) = 1;

    inst = h;
    if (!inst)
        return 0;

    rc = setECIerror(api_synchronize(OI_NEW(inst)), inst);
    ok = (rc == -2 || rc == 1) ? 0 : 1;
    if (rc == 1) {
        OI_REFUSED(inst) = 0x400;
        OI_REFUSEDALL(inst) |= 0x400;
    }
    OI_BUSY(inst) = 0;
    if (rc == POLL_ABORTED)
        eo_stop(inst);
    return ok;
}

int STDCALL es_pause(OldInst *h, int32_t on)
{
    OldInst *inst;

    if (es_reentered(h))
        return 0;
    inst = h;
    if (!inst)
        return 0;
    return setECIerror(api_pause(OI_NEW(inst), on), inst) >= 0;
}

/* End an instance and give back everything it holds. Answers nought
   always. */
int STDCALL es_delete(OldInst *h)
{
    OldInst *inst;

    if (es_reentered(h))
        return 0;
    if (h)
        OI_BUSY(h) = 1;

    inst = h;
    if (!inst)
        return 0;

    api_delete(OI_NEW(inst));

    if (OI_DIRECT(inst))
        OI_DIRECT(inst) = 0;
    if (OI_DIRECT2(inst))
        OI_DIRECT2(inst) = 0;
    if (OI_OWNED1(inst)) {
        cpp_delete(OI_OWNED1(inst));
        OI_OWNED1(inst) = 0;
    }
    if (OI_OWNED2(inst)) {
        cpp_delete(OI_OWNED2(inst));
        OI_OWNED2(inst) = 0;
    }
    if (OI_CONCAT(inst))
        cpp_delete(OI_CONCAT(inst));

    eo_clearManualQueue(inst);
    free(inst);
    return 0;
}

/* Put an instance back the way a new one would be: the defaults afresh, the
   eight editable voices back to the standard ones, the queue emptied, and
   the output rebuilt. The rate is tried as it stands, then eleven thousand,
   then eight; only if all three are refused does this fail. */
int STDCALL es_reset(OldInst *h)
{
    OldInst *inst;
    int failed = 0;
    int family, dialect, i;

    if (es_reentered(h))
        return 0;
    if (h)
        OI_BUSY(h) = 1;

    inst = h;
    if (!inst)
        return 0;

    if (setECIerror(api_reset(OI_NEW(inst),
                              g_DefaultEnvironment[ENV_LANGUAGE]), inst)) {
        OI_BUSY(inst) = 0;
        return 0;
    }

    eo_clearManualQueue(inst);
    if (!eo_getDefaultEnvironment(inst, 0)
        || !eo_getDefaultActiveVoice(inst, 0)) {
        OI_BUSY(inst) = 0;
        return 0;
    }

    OI_READY(inst) = 1;
    OI_READY2(inst) = 1;

    family = (int8_t)((OI_LANG(inst) & 0xff0000) >> 16);
    dialect = (int8_t)(OI_LANG(inst) & 0xff);
    for (i = 0; i < OLD_VOICES; i++) {
        char *from = standardVoices
                   + (family - 1) * SV_FAMILY_BYTES
                   + dialect * SV_DIALECT_BYTES
                   + i * VOICE_BYTES + SV_FIRST;

        memcpy(OI_VOICES(inst) + i * VOICE_BYTES, from, VOICE_BYTES);
        strcpy(OI_VOICES(inst) + i * VOICE_BYTES, "User-Defined");
    }

    ev_saveInstanceData(inst);
    sprintf(OI_FILENAME(inst), "%d", 0);

    if (!ev_setOutputToDevice(inst, OI_RATE(inst), OI_ENV(inst)[13],
                              OI_ENV(inst)[14], OI_ENV(inst)[15],
                              OI_ENV(inst)[16])) {
        ev_restoreInstanceData(inst);
        OI_RATE(inst) = 1;
        if (!ev_setOutputToDevice(inst, OI_RATE(inst), OI_ENV(inst)[13],
                                  OI_ENV(inst)[14], OI_ENV(inst)[15],
                                  OI_ENV(inst)[16])) {
            ev_restoreInstanceData(inst);
            OI_RATE(inst) = 0;
            if (!ev_setOutputToDevice(inst, OI_RATE(inst), OI_ENV(inst)[13],
                                      OI_ENV(inst)[14], OI_ENV(inst)[15],
                                      OI_ENV(inst)[16])) {
                ev_restoreInstanceData(inst);
                failed = 1;
            }
        }
    }

    if (failed) {
        OI_BUSY(inst) = 0;
        return 0;
    }

    OI_REFUSED(inst) = 0;
    OI_REFUSEDALL(inst) = 0;
    OI_BUSY(inst) = 0;
    return 1;
}

/* ---- the shorthand -------------------------------------------------- */

/* Make an instance, say one thing with it, and take it away again. */
static int es_speakWith(OldInst *inst, const char *text, int32_t annotate)
{
    if (!inst)
        return 0;

    if (annotate)
        ev_setParam(inst, 1, 1);

    if (!et_addText(inst, text) || !et_synthesize(inst)
        || !es_synchronize(inst))
        return 0;

    es_delete(inst);
    return 1;
}

int STDCALL es_speakText(const char *text, int32_t annotate)
{
    return es_speakWith(eo_newEx(g_DefaultEnvironment[ENV_LANGUAGE]), text,
                        annotate);
}

int STDCALL es_speakTextEx(const char *text, int32_t annotate,
                             int32_t language)
{
    return es_speakWith(eo_newEx(language), text, annotate);
}

/* Say the phrase the engine keeps for checking that it works at all, in the
   first of the standard voices. */
int STDCALL es_testPhrase(OldInst *h)
{
    OldInst *inst;
    char narrow[0xd0];
    uint16_t *wide = 0;

    if (es_reentered(h))
        return 0;
    inst = h;
    if (!inst)
        return 0;

    memset(narrow, 0, sizeof narrow);
    sprintf(narrow, "1 2 3.");

    if (isUnicodeCodeSet(0x800, eo_getParam(h, ENV_LANGUAGE))) {
        if (MBCSConverter(h, narrow, &wide))
            return 0;
        if (!eo_stop(h) || !vc_copyVoice(h, 1, 0)
            || !et_addText(h, (const char *)wide) || !et_synthesize(h))
            return 0;
        return 1;
    }

    if (!eo_stop(h) || !vc_copyVoice(h, 1, 0) || !et_addText(h, narrow)
        || !et_synthesize(h))
        return 0;
    return 1;
}

/* ---- run the filters over a string without speaking it -------------- */

int STDCALL es_getFilteredText(OldInst *h, void *a, void *b, void *c)
{
    if (!h)
        return 0;
    FilterText2(h, a, b, c);
    return 0;
}

/* ---- and the ones that were never written --------------------------- */

/* Naming a file to write to was published and does nothing. */
int STDCALL es_setOutputFilename(OldInst *h, const char *name)
{
    (void)name;
    if (es_reentered(h))
        return 0;
    return 0;
}

int STDCALL es_synthesizeFile(OldInst *h, const char *name)
{
    (void)name;
    if (es_reentered(h))
        return 0;
    return 0;
}

int STDCALL es_isBeingReentered(OldInst *h)
{
    (void)h;
    return 0;
}

int STDCALL es_progStatus(OldInst *h)
{
    (void)h;
    return 0;
}

int STDCALL es_requestLicense(void *a)
{
    (void)a;
    return 0;
}

void STDCALL es_errorMessage(OldInst *h, void *out)
{
    (void)h;
    (void)out;
}

void STDCALL es_startLogging(int32_t what)
{
    (void)what;
}

void STDCALL es_stopLogging(int32_t what)
{
    (void)what;
}

int STDCALL es_getLog(void *a)
{
    (void)a;
    return 0;
}

int STDCALL es_getIntLog(void *a, void *b)
{
    (void)a;
    (void)b;
    return 0;
}

int STDCALL es_dialogBox(void *a, void *b, void *c, void *d, void *e)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    return 1;
}

ALIAS_N("_eciGetParam@8", "eo_getParam", 8);
ALIAS_N("_eciGetDefaultParam@4", "es_getDefaultParam", 4);
ALIAS_N("_eciSetDefaultParam@8", "es_setDefaultParam", 8);
ALIAS_N("_eciSynchronize@4", "es_synchronize", 4);
ALIAS_N("_eciPause@8", "es_pause", 8);
ALIAS_N("_eciDelete@4", "es_delete", 4);
ALIAS_N("_eciReset@4", "es_reset", 4);
ALIAS_N("_eciSpeakText@8", "es_speakText", 8);
ALIAS_N("_eciSpeakTextEx@12", "es_speakTextEx", 12);
ALIAS_N("_eciTestPhrase@4", "es_testPhrase", 4);
ALIAS_N("_eciGetFilteredText@16", "es_getFilteredText", 16);
ALIAS_N("_eciSetOutputFilename@8", "es_setOutputFilename", 8);
ALIAS_N("_eciSynthesizeFile@8", "es_synthesizeFile", 8);
ALIAS_N("_eciIsBeingReentered@4", "es_isBeingReentered", 4);
ALIAS_N("_eciProgStatus@4", "es_progStatus", 4);
ALIAS_N("_eciRequestLicense@4", "es_requestLicense", 4);
ALIAS_N("_eciErrorMessage@8", "es_errorMessage", 8);
ALIAS_N("_eciStartLogging@4", "es_startLogging", 4);
ALIAS_N("_eciStopLogging@4", "es_stopLogging", 4);
ALIAS_N("_eciGetLog@4", "es_getLog", 4);
ALIAS_N("_eciGetIntLog@8", "es_getIntLog", 8);
ALIAS_N("_eciDialogBox@20", "es_dialogBox", 20);
