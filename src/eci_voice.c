/* Voices, and the two sets of units they are kept in.

   A voice is eighty bytes: a name, then eight settings in the engine's own
   units, then three of those same settings a second time in the units a
   person would use. Pitch, speed and volume are the three; the engine counts
   them nought to a hundred, while a person wants hertz and words a minute.
   Both copies are kept because the caller may ask for either, and which one
   it gets is decided by a setting in the environment.

   The third of those duplicated words does double duty. A voice whose
   real-world pitch is nought has never been filled in, so that same word is
   what every lookup in this file tests to decide whether a voice exists at
   all.

   There are four places a voice can come from. The one in play, the eight
   the caller may edit, the eight standard ones that come with a language,
   and the concatenative table, which has its own set for every rate. The
   lookups below try them in that order and it is the same order everywhere.

   Names are prefixed and the aliases at the foot carry the real ones. */

#include <stdint.h>
#include <string.h>
#include "eci_synththread.h"
#include "evv_abi.h"
#include "eci_old.h"

/* Whether the caller wants the engine's units or a person's. */
#define ENV_REALWORLD   8
#define ENV_CODESET     9

/* What a voice is made of. */
#define VOICE_NAME_ROOM   0x1e
#define VOICE_PARAMS      0x20
#define VOICE_PARAM(v, i) (*(int32_t *)((char *)(v) + VOICE_PARAMS + (i) * 4))
#define VOICE_PITCH_HZ    0x44
#define VOICE_SPEED_WPM   0x48
#define VOICE_VOLUME_RW   0x4c

/* The three settings kept in both units, and where the real-world copy of
   each one sits. A voice whose pitch word is nought is an empty slot. */
#define PARAM_PITCH     2
#define PARAM_SPEED     6
#define PARAM_VOLUME    7
#define VOICE_PRESENT   VOICE_PITCH_HZ

/* The two tables, and how a voice is found in them. */
#define CV_FAMILY_BYTES  0x1e18
#define CV_DIALECT_BYTES 0x0f0c
#define CV_RATE_BYTES    0x0504
#define SV_FAMILY_BYTES  0x0a08
#define SV_DIALECT_BYTES 0x0504
#define SV_FIRST         4

/* The eight the caller may edit are numbered nine to sixteen. */
#define EDITABLE_FIRST  9
#define EDITABLE_LAST   16
#define STANDARD_LAST   8

/* Whether an instance is in the middle of speaking. */
#define SYNTH_BUSY      3

/* What the older interface calls the answer to a voice call. */
#define VOICE_OK        0
#define VOICE_FAILED    1
#define VOICE_BAD_ARG   2
#define VOICE_NO_ROOM   3

typedef struct ECIVoice { int32_t w[0x14]; } ECIVoice;

/* What the engine hands back when a voice is registered. */
typedef struct VoiceRegistration {
    int32_t rate;               /* +0x00 */
    int32_t language;           /* +0x04 */
    char    name[0x20];         /* +0x08 */
    int32_t params[8];          /* +0x28 */
} VoiceRegistration;

extern int32_t STDCALL eo_getParam(OldInst *h, int32_t which)
    MANGLED("_eciGetParam@8");
extern int32_t STDCALL api_check_synth(void *h2)
    MANGLED("_eciCheckSynthesizing2@4");
extern int32_t STDCALL api_register_voice(void *h2, int32_t voiceno,
                                           void *a, VoiceRegistration *out)
    MANGLED("_eciRegisterVoice2@16");
extern int32_t STDCALL api_unregister_voice(void *h2, int32_t voiceno,
                                             void *reg, void *a)
    MANGLED("_eciUnregisterVoice2@16");
extern int eci2RealWorld(int32_t realWorld, int32_t which, int32_t value)
    MANGLED("?eci2RealWorld@@YAHHW4ECIVoiceParam@@H@Z");
extern int realWorld2eci(int32_t realWorld, int32_t which, int32_t value,
                         int32_t lo, int32_t hi)
    MANGLED("?realWorld2eci@@YAHHW4ECIVoiceParam@@HHH@Z");
extern int isUnicodeCodeSet(int32_t bit, int32_t mode)
    MANGLED("_isUnicodeCodeSet");
extern int UnicodeConverter(OldInst *h, const char *text, char **out,
                            int32_t want) MANGLED("_UnicodeConverter");
extern int MBCSConverter(OldInst *h, const char *text, uint16_t **out)
    MANGLED("_MBCSConverter");
extern char standardVoices[] MANGLED("_standardVoices");

extern int eo_getDefaultActiveVoice(OldInst *h, int32_t which);

extern const int32_t ev_voiceParamRange[8][2];
extern const int32_t ev_realWorldVoiceParamRange[8][2];

int getRealWorldVoiceParam(ECIVoice v, int32_t which);
void setRealWorldParamsFromECIParams(char *voice, int32_t which);

/* ---- the two sets of units ------------------------------------------ */

/* Read one setting out of a voice, in a person's units where there are any
   and the engine's where there are not. The voice arrives by value, which is
   how the original's callers push it. */
int getRealWorldVoiceParam(ECIVoice v, int32_t which)
{
    char *p = (char *)&v;

    switch (which) {
    case PARAM_PITCH:  return *(int32_t *)(p + VOICE_PITCH_HZ);
    case PARAM_SPEED:  return *(int32_t *)(p + VOICE_SPEED_WPM);
    case PARAM_VOLUME: return *(int32_t *)(p + VOICE_VOLUME_RW);
    default:           return VOICE_PARAM(p, which);
    }
}

/* Write one, and answer with what it was. Only the three that are kept in
   both units have anywhere to go; the rest are quietly dropped, because the
   engine's copy is the only one they have and the caller sets that
   elsewhere. */
int setRealWorldVoiceParam(char *voice, int32_t which, int32_t value)
{
    int was = getRealWorldVoiceParam(*(ECIVoice *)voice, which);

    switch (which) {
    case PARAM_PITCH:
        *(int32_t *)(voice + VOICE_PITCH_HZ) = value;
        break;
    case PARAM_SPEED:
        *(int32_t *)(voice + VOICE_SPEED_WPM) = value;
        break;
    case PARAM_VOLUME:
        *(int32_t *)(voice + VOICE_VOLUME_RW) = value;
        break;
    default:
        break;
    }
    return was;
}

/* Bring the real-world copies back into step with the engine's, either for
   one setting or, when asked for minus one, for all three. */
void setRealWorldParamsFromECIParams(char *voice, int32_t which)
{
    if (which == -1) {
        static const int ALL[3] = { PARAM_PITCH, PARAM_SPEED, PARAM_VOLUME };
        int i;

        for (i = 0; i < 3; i++)
            setRealWorldVoiceParam(voice, ALL[i],
                                   eci2RealWorld(1, ALL[i],
                                                 VOICE_PARAM(voice, ALL[i])));
        return;
    }

    if (which != PARAM_PITCH && (which <= 5 || which > PARAM_VOLUME))
        return;

    setRealWorldVoiceParam(voice, which,
                           eci2RealWorld(1, which, VOICE_PARAM(voice, which)));
}

/* ---- finding a voice ------------------------------------------------ */

static char *vc_concatEntry(OldInst *h, int family, int dialect,
                            int32_t rate, int32_t voiceno)
{
    return (char *)OI_CONCAT(h)
        + (family - 1) * CV_FAMILY_BYTES
        + dialect * CV_DIALECT_BYTES
        + rate * CV_RATE_BYTES
        + (voiceno - 1) * VOICE_BYTES;
}

static char *vc_standardEntry(int family, int dialect, int32_t voiceno)
{
    return standardVoices
        + (family - 1) * SV_FAMILY_BYTES
        + dialect * SV_DIALECT_BYTES
        + (voiceno - 1) * VOICE_BYTES;
}

static int vc_present(const char *entry)
{
    return *(const int32_t *)(entry + VOICE_PRESENT) != 0;
}

static int vc_family(OldInst *h)
{
    return (int8_t)((OI_LANG(h) & 0xff0000) >> 16);
}

static int vc_dialect(OldInst *h)
{
    return (int8_t)(OI_LANG(h) & 0xff);
}

/* One of the eight that come with the language, taken from the
   concatenative table if it is there and the standard one if not. Answers
   the voice itself, past the word that says whether it is filled in. */
static char *vc_languageVoice(OldInst *h, int32_t voiceno)
{
    int family = vc_family(h);
    int dialect = vc_dialect(h);
    char *entry = vc_concatEntry(h, family, dialect, OI_RATE(h), voiceno);

    if (vc_present(entry))
        return entry + SV_FIRST;
    return vc_standardEntry(family, dialect, voiceno) + SV_FIRST;
}

/* One of the eight the caller may edit. */
static char *vc_editableVoice(OldInst *h, int32_t voiceno)
{
    return OI_VOICES(h) + (voiceno - EDITABLE_FIRST) * VOICE_BYTES;
}

/* ---- turning the engine's answers into the older interface's -------- */

int vc_rc_to_VoiceError(int32_t rc)
{
    if (rc == -24)
        return VOICE_NO_ROOM;
    if (rc < -24)
        return VOICE_FAILED;
    if (rc <= -22)
        return VOICE_BAD_ARG;
    if (rc == -20)
        return VOICE_BAD_ARG;
    if (rc == 0)
        return VOICE_OK;
    return VOICE_FAILED;
}

/* ---- the entry points ----------------------------------------------- */

static int vc_reentered(OldInst *h)
{
    if (!h || !OI_BUSY(h))
        return 0;
    OI_REFUSED(h) = 0x800;
    OI_REFUSEDALL(h) |= 0x800;
    return 1;
}

/* A voice number the caller may write to: the one in play, or one of the
   eight editable ones. */
static int vc_writable(int32_t voiceno)
{
    return voiceno == 0
        || (voiceno > STANDARD_LAST && voiceno <= EDITABLE_LAST);
}

/* Copy one voice over another. Copying onto the one in play also remembers
   which number it came from and forces everything to be sent down again. */
int STDCALL vc_copyVoice(OldInst *h, int32_t from, int32_t to)
{
    OldInst *inst;
    char buf[VOICE_BYTES];
    int family, dialect;

    if (vc_reentered(h))
        return 0;

    inst = h;
    if (!inst)
        return 0;

    family = vc_family(inst);
    dialect = vc_dialect(inst);

    /* Only a source that is a real standard voice will do, and only when
       the destination is somewhere the caller owns. */
    if (!vc_writable(to) || from != 0) {
        if (from <= 0 || from > EDITABLE_LAST)
            return 0;
        if (from <= STANDARD_LAST
            && !vc_present(vc_standardEntry(family, dialect, from)))
            return 0;
    }

    if (from == 0) {
        memcpy(buf, OI_VOICE(inst), VOICE_BYTES);
    } else if (from > STANDARD_LAST) {
        char *entry = vc_concatEntry(inst, family, dialect, OI_RATE(inst),
                                     from);

        if (vc_present(entry))
            memcpy(buf, entry + SV_FIRST, VOICE_BYTES);
        else
            memcpy(buf, vc_editableVoice(inst, from), VOICE_BYTES);
    } else {
        memcpy(buf, vc_languageVoice(inst, from), VOICE_BYTES);
    }

    if (to == 0) {
        memcpy(OI_VOICE(inst), buf, VOICE_BYTES);
        OI_PREV_VOICENO(inst) = OI_VOICENO(inst);
        OI_VOICENO(inst) = from;
        OI_READY(inst) = 1;
    } else {
        memcpy(vc_editableVoice(inst, to), buf, VOICE_BYTES);
    }
    return 1;
}

/* Read one setting of one voice. */
int STDCALL vc_getVoiceParam(OldInst *h, int32_t voiceno, int32_t which)
{
    OldInst *inst = h;
    int realWorld;

    if (!inst)
        return -1;
    if (which == 8)
        return -1;
    if (voiceno < 0 || voiceno > EDITABLE_LAST)
        return -1;
    if (which < 0 || which > 8)
        return -1;

    realWorld = (OI_ENV(inst)[ENV_REALWORLD] == 1);

    if (voiceno == 0) {
        if (realWorld)
            return getRealWorldVoiceParam(*(ECIVoice *)OI_VOICE(inst), which);
        return VOICE_PARAM(OI_VOICE(inst), which);
    }

    if (voiceno > STANDARD_LAST) {
        char *v = vc_editableVoice(inst, voiceno);

        if (realWorld)
            return getRealWorldVoiceParam(*(ECIVoice *)v, which);
        return VOICE_PARAM(v, which);
    }

    {
        char *v = vc_languageVoice(inst, voiceno);

        if (realWorld)
            return getRealWorldVoiceParam(*(ECIVoice *)v, which);
        return VOICE_PARAM(v, which);
    }
}

/* Write one, and answer with what it was.

   The number the caller gives is in whichever units it asked for, so it is
   checked twice: once against what that unit will take at all, and once
   against the engine's own range converted into those units. */
int STDCALL vc_setVoiceParam(OldInst *h, int32_t voiceno, int32_t which,
                               int32_t value)
{
    OldInst *inst;
    int32_t old = -1;
    int realWorld;
    char *v;

    if (vc_reentered(h))
        return -1;

    inst = h;
    realWorld = (OI_ENV(inst)[ENV_REALWORLD] == 1);

    if (realWorld) {
        if (value > ev_realWorldVoiceParamRange[which][1]
            || value < ev_realWorldVoiceParamRange[which][0])
            return -1;
    }

    if (!inst)
        return old;
    if (!vc_writable(voiceno))
        return old;

    if (value < eci2RealWorld(OI_ENV(inst)[ENV_REALWORLD], which,
                              ev_voiceParamRange[which][0]))
        return old;
    if (value > eci2RealWorld(OI_ENV(inst)[ENV_REALWORLD], which,
                              ev_voiceParamRange[which][1]))
        return old;
    if (which < 0 || which >= 8)
        return old;

    v = (voiceno == 0) ? OI_VOICE(inst) : vc_editableVoice(inst, voiceno);

    if (realWorld) {
        old = setRealWorldVoiceParam(v, which, value);
        VOICE_PARAM(v, which) = realWorld2eci(1, which, value, 0, 250);
    } else {
        old = VOICE_PARAM(v, which);
        VOICE_PARAM(v, which) = value;
        setRealWorldParamsFromECIParams(v, which);
    }
    return old;
}

/* Read a voice's name, in whichever encoding the caller is being spoken
   to in. */
int STDCALL vc_getVoiceName(OldInst *h, int32_t voiceno, void *out)
{
    OldInst *inst = h;
    char name[VOICE_NAME_ROOM];
    uint16_t *wide;
    unsigned i;

    memset(name, 0, sizeof name);

    if (!inst || !out) {
        OI_REFUSED(inst) = 0x80;
        OI_REFUSEDALL(inst) |= 0x80;
        return 0;
    }
    if (voiceno < 0 || voiceno > EDITABLE_LAST)
        return 0;

    if (voiceno == 0)
        strcpy(name, OI_VOICE(inst));
    else if (voiceno > STANDARD_LAST)
        strcpy(name, vc_editableVoice(inst, voiceno));
    else
        strcpy(name, vc_languageVoice(inst, voiceno));

    if (!isUnicodeCodeSet(0x800, eo_getParam(h, ENV_CODESET))) {
        strcpy(out, name);
        return 1;
    }

    if (MBCSConverter(h, name, &wide))
        return 0;
    for (i = 0; i < strlen(name); i++)
        ((uint16_t *)out)[i] = wide[i];
    ((uint16_t *)out)[i] = 0;
    return 1;
}

/* Write one. Only the voice in play and the eight editable ones have a name
   the caller owns, but an out-of-range number is still answered with
   success, which is what the original does. */
int STDCALL vc_setVoiceName(OldInst *h, int32_t voiceno, const char *name)
{
    OldInst *inst;
    char *s = 0;
    int ok = 0;

    if (vc_reentered(h))
        return 0;

    inst = h;

    if (isUnicodeCodeSet(0x800, eo_getParam(h, ENV_CODESET))) {
        if (UnicodeConverter(h, name, &s, 1)) {
            OI_REFUSED(inst) = 0x1000;
            OI_REFUSEDALL(inst) |= 0x1000;
            ok = 0;
        }
    } else {
        s = (char *)name;
    }

    if (!inst)
        return ok;

    if (vc_writable(voiceno)) {
        char *v = (voiceno == 0) ? OI_VOICE(inst)
                                 : vc_editableVoice(inst, voiceno);

        strncpy(v, s, VOICE_NAME_ROOM);
        v[VOICE_NAME_ROOM] = 0;
    }
    return 1;
}

/* Tell the engine about a voice of the caller's own, and write what it
   answers into the concatenative table so the rest of this file can find
   it. */
int STDCALL vc_registerVoice(OldInst *h, int32_t voiceno, void *a,
                               VoiceRegistration *out)
{
    OldInst *inst;
    VoiceRegistration reg;
    int32_t rc = 1;
    int family, dialect;
    char *entry;

    if (!h || !a || !out || voiceno < 1 || voiceno > EDITABLE_LAST)
        return 1;

    inst = h;
    if (!h || !OI_NEW(inst))
        return vc_rc_to_VoiceError(rc);

    if (api_check_synth(OI_NEW(inst)) == SYNTH_BUSY) {
        OI_REFUSED(inst) = 0x2000;
        OI_REFUSEDALL(inst) |= 0x2000;
        return vc_rc_to_VoiceError(0x2000);
    }

    rc = api_register_voice(OI_NEW(inst), voiceno, a, &reg);
    if (rc != 0)
        return vc_rc_to_VoiceError(rc);

    out->rate = reg.rate;
    out->language = reg.language;

    family = (uint8_t)((reg.language & 0xff0000) >> 16);
    dialect = (uint8_t)(reg.language & 0xff);

    /* The first word of a rate's block says that block has anything in it
       at all, and the voice's own pitch word says the voice does. */
    *(int32_t *)((char *)OI_CONCAT(inst)
                 + (family - 1) * CV_FAMILY_BYTES
                 + dialect * CV_DIALECT_BYTES
                 + reg.rate * CV_RATE_BYTES) = 1;

    entry = vc_concatEntry(inst, family, dialect, reg.rate, voiceno);
    *(int32_t *)(entry + VOICE_PRESENT) = 1;

    memcpy(entry + SV_FIRST, reg.name, 0x1f);
    memcpy(entry + SV_FIRST + VOICE_PARAMS, reg.params, 0x20);
    setRealWorldParamsFromECIParams(entry + SV_FIRST, -1);

    if (voiceno == OI_VOICENO(inst))
        eo_getDefaultActiveVoice(inst, 0);

    return vc_rc_to_VoiceError(rc);
}

/* And take it away again. */
int STDCALL vc_unregisterVoice(OldInst *h, int32_t voiceno,
                                 VoiceRegistration *reg, void *a)
{
    OldInst *inst;
    int32_t rc = 1;
    int family, dialect;

    if (!h || !a || !reg || voiceno < 1 || voiceno > EDITABLE_LAST)
        return 1;

    inst = h;
    if (!h || !OI_NEW(inst))
        return vc_rc_to_VoiceError(rc);

    if (api_check_synth(OI_NEW(inst)) == SYNTH_BUSY) {
        OI_REFUSED(inst) = 0x2000;
        OI_REFUSEDALL(inst) |= 0x2000;
        return vc_rc_to_VoiceError(0x2000);
    }

    family = (uint8_t)((reg->language & 0xff0000) >> 16);
    dialect = (uint8_t)(reg->language & 0xff);
    *(int32_t *)(vc_concatEntry(inst, family, dialect, reg->rate, voiceno)
                 + VOICE_PRESENT) = 0;

    rc = api_unregister_voice(OI_NEW(inst), voiceno, reg, a);
    OI_READY(inst) = 1;

    if (voiceno == OI_VOICENO(inst))
        eo_getDefaultActiveVoice(inst, 0);

    return vc_rc_to_VoiceError(rc);
}

ALIAS("?rc_to_VoiceError@@YA?AW4ECIVoiceError@@J@Z", "vc_rc_to_VoiceError");
/* These three already carry the names the object uses, so they need no
   alias; the swap stands the original's aside on the strength of the name
   alone. */

ALIAS_N("_eciCopyVoice@12", "vc_copyVoice", 12);
ALIAS_N("_eciGetVoiceParam@12", "vc_getVoiceParam", 12);
ALIAS_N("_eciSetVoiceParam@16", "vc_setVoiceParam", 16);
ALIAS_N("_eciGetVoiceName@12", "vc_getVoiceName", 12);
ALIAS_N("_eciSetVoiceName@12", "vc_setVoiceName", 12);
ALIAS_N("_eciRegisterVoice@16", "vc_registerVoice", 16);
ALIAS_N("_eciUnregisterVoice@16", "vc_unregisterVoice", 16);
