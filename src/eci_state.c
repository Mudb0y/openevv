/* The parameters an ECI instance holds, and what the engine is told about
   them.

   Almost everything here turns a number into a two-letter annotation and
   posts it into the text stream ahead of the words: speed becomes `vs,
   volume `vv, the standard voice `v, and so on. That is how ECI changes
   the voice — not by reaching into the synthesizer, but by saying so in the
   text, which is why a parameter set between two sentences takes effect
   between them and not in the middle of one.

   This is transcribed rather than rewritten. Every one of these numbers can
   change what comes out of the speaker, and the annotations are the
   language's own, so there is nothing here to improve on. The layout is the
   original's too, because the constructor still belongs to the object next
   door. */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "evv_abi.h"
#include "evv_arena.h"
#include "eci_synththread.h"
#include "eci_instance.h"

#define STD  __attribute__((stdcall))

/* How ECI names a language: a packed number and the same thing as text. */

/* What the engine wrapper offers, at the two places used here. Its methods
   are virtual and take their object as an ordinary first argument. */
typedef struct {
    void *slot_00[6];
    int32_t (STD *processRemaining)(void *self, const char *text);  /* +0x18 */
    void *slot_1c[18];
    void (STD *wantPhonemeIndices)(void *self, int32_t on);         /* +0x64 */
} EngineVtbl;

typedef struct { const EngineVtbl *vt; } EngineWrapper;

/* ---- what the original supplies -------------------------------------- */

extern THIS int32_t st_changeRomParam(void *t, int32_t p, int32_t v)
    MANGLED("?changeRomParam@SynthThread@@QAEJJJ@Z");
extern THIS int32_t st_changeLanguage(void *t, LangIdentifier *l)
    MANGLED("?changeLanguage@SynthThread@@QAEJPAVLangIdentifier@@@Z");
extern THIS int32_t stl_initialize(void *t, LangIdentifier *l)
    MANGLED("?initialize@SynthThread@@QAEJPAVLangIdentifier@@@Z");
extern THIS int32_t st_changeVoice(void *t, int32_t v)
    MANGLED("?changeVoice@SynthThread@@QAEJJ@Z");
extern THIS int32_t st_setPhonemeIndicies(void *t, int32_t v)
    MANGLED("?setPhonemeIndicies@SynthThread@@QAEJJ@Z");
extern THIS int32_t st_changeFilter(void *t, int32_t a, int32_t b, int32_t c,
                                    int8_t d)
    MANGLED("?changeFilter@SynthThread@@QAEJJJJ_N@Z");
extern THIS int32_t stm_newFilter(void *t, int32_t a, int32_t b, void **out)
    MANGLED("?newFilter@SynthThread@@QAEJJJPAPAX@Z");
extern THIS int32_t stm_activateFilterByIdFlag(void *t, uint32_t id, int8_t on)
    MANGLED("?activateFilter@SynthThread@@QAEJK_N@Z");
extern THIS int32_t stm_deactivateFilterByIdFlag(void *t, uint32_t id, int8_t on)
    MANGLED("?deactivateFilter@SynthThread@@QAEJK_N@Z");
extern THIS int32_t stm_deleteFilterByNumbers(void *t, int32_t a, int32_t b)
    MANGLED("?deleteFilter@SynthThread@@QAEJJJ@Z");
extern THIS int32_t st_changePitch(void *t, int32_t p, int32_t v)
    MANGLED("?changePitch@SynthThread@@QAEJJJ@Z");
extern THIS int32_t st_changePitchString(void *t, int32_t p, int32_t v)
    MANGLED("?changePitchString@SynthThread@@QAEJJJ@Z");
extern THIS int32_t st_changeFluctuation(void *t, int32_t p, int32_t v)
    MANGLED("?changeFluctuation@SynthThread@@QAEJJJ@Z");
extern THIS int32_t st_changeFluctuationString(void *t, int32_t p, int32_t v)
    MANGLED("?changeFluctuationString@SynthThread@@QAEJJJ@Z");
extern THIS int32_t st_changeSpeed(void *t, int32_t p, int32_t v)
    MANGLED("?changeSpeed@SynthThread@@QAEJJJ@Z");
extern THIS int32_t st_changeSpeedString(void *t, int32_t p, int32_t v)
    MANGLED("?changeSpeedString@SynthThread@@QAEJJJ@Z");
extern THIS int32_t st_changeVolume(void *t, int32_t p, int32_t v)
    MANGLED("?changeVolume@SynthThread@@QAEJJJ@Z");
extern THIS int32_t st_changeETIEmphasis(void *t)
    MANGLED("?changeETIEmphasis@SynthThread@@QAEJXZ");
extern THIS int32_t st_addParam(void *t, char *s, uint32_t n)
    MANGLED("?addParam@SynthThread@@QAEJPADK@Z");

extern THIS int32_t sy_mutexWait(void *m, int32_t ms)
    MANGLED("?wait@Mutex@@QAEHJ@Z");
extern THIS int32_t sy_mutexRelease(void *m)
    MANGLED("?release@Mutex@@QAEHXZ");

extern THIS void li_setPackedInt(LangIdentifier *l)
    MANGLED("?setPackedInt@LangIdentifier@@AAEXXZ");

/* The reader keeps its own buffer, so the object is bigger than anything
   here needs to know; only its size matters, and it is the original's. */
#define INI_BYTES 0x128
extern THIS void *ini_ctor(void *r) MANGLED("??0IniFileReader@@QAE@XZ");
extern THIS void  ini_dtor(void *r) MANGLED("??1IniFileReader@@QAE@XZ");
extern THIS const char *ini_getFirstSection(void *r)
    MANGLED("?getFirstSection@IniFileReader@@QAEPBDXZ");
extern THIS const char *ini_getNextSection(void *r)
    MANGLED("?getNextSection@IniFileReader@@QAEPBDXZ");

extern void *cpp_new(uint32_t n) MANGLED("??2@YAPAXI@Z");
extern void  cpp_delete(void *p) MANGLED("??3@YAXPAX@Z");

/* What the original's error codes mean here: minus two is a general
   refusal, minus six a parameter out of range, minus fourteen no language. */
#define ECI_FAILED    (-2)
#define ECI_BAD_PARAM (-6)
#define ECI_NO_LANG   (-14)

/* Every annotation goes in as one control character and a short name. */
static const char ANNO[] = "`%s%u";
static const char ANNO_S[] = "`%s%s";

/* ---- the language identifier ------------------------------------------ */

THIS LangIdentifier *lang_ctor(LangIdentifier *l, const char *s)
{
    l->packed = 0;
    l->packed = 0;
    strncpy(l->text, s, sizeof l->text);
    l->pad_10[0] = 0;
    li_setPackedInt(l);
    return l;
}

THIS void lang_setString(LangIdentifier *l)
{
    sprintf(l->text, "%u.%u", (unsigned)((l->packed >> 16) & 0xff),
            (unsigned)(l->packed & 0xff));
}

/* ---- saying it to the engine ------------------------------------------ */

static THIS int32_t es_sendAnnotationInt(ECIstate *s, const char *code,
                                         int32_t value, void *thread)
{
    int32_t rc = ECI_FAILED;
    char *buf;

    (void)s;
    buf = cpp_new((uint32_t)strlen(code) + 0x20);
    if (buf == 0)
        return rc;
    sprintf(buf, ANNO, code, (unsigned)value);
    rc = st_addParam(thread, buf, (uint32_t)strlen(buf));
    cpp_delete(buf);
    return rc;
}

static THIS int32_t es_sendAnnotationStr(ECIstate *s, const char *code,
                                         char *text, void *thread)
{
    int32_t rc = ECI_FAILED;
    char *buf;

    (void)s;
    buf = cpp_new((uint32_t)(strlen(code) + strlen(text) + 2));
    if (buf == 0)
        return rc;
    sprintf(buf, ANNO_S, code, text);
    rc = st_addParam(thread, buf, (uint32_t)strlen(buf));
    cpp_delete(buf);
    return rc;
}

THIS int32_t es_setStandardVoice(ECIstate *s, int32_t v, void *thread)
{
    return es_sendAnnotationInt(s, "v", v, thread);
}

/* One parameter as the engine hears it. The sample rate is the odd one
   out: it is not an annotation but a call, because it changes the
   synthesizer rather than the voice. */
THIS int32_t es_paramToEngine(ECIstate *s, int32_t p, EngineWrapper *engine)
{
    char buf[0x48];
    int say = 1;
    int32_t ok = 1;
    const char *code = 0;

    buf[0] = 0;
    switch (p) {
    case 0:   code = "ts"; break;
    case 1:   code = "ty"; break;
    case 3:   code = "da"; break;
    case 4:
        engine->vt->wantPhonemeIndices(engine, s->param[p]);
        say = 0;
        break;
    case 5:   code = "vg"; break;
    case 6:   code = "vh"; break;
    case 7:   code = "vb"; break;
    case 8:   code = "vf"; break;
    case 9:   code = "vr"; break;
    case 10:  code = "vy"; break;
    case 11:  code = "vs"; break;
    case 12:  code = "vv"; break;
    case 13:  code = "pp"; break;
    case 16:
        /* The standard voices are numbered one to eight, and nought means
           the first. A number past eight leaves the line empty, which is
           the original's own oversight; there it leaves the line whatever
           the stack held. */
        if (s->param[p] <= 8) {
            int32_t v = s->param[p] ? s->param[p] : 1;

            sprintf(buf, ANNO, "v", (unsigned)v);
        }
        break;
    default:
        say = 0;
        break;
    }

    if (code != 0)
        sprintf(buf, ANNO, code, (unsigned)s->param[p]);
    if (say && engine->vt->processRemaining(engine, buf))
        ok = 0;
    return ok;
}

/* Everything the engine has to be told when it starts, or after it has
   been reset. The standard voice is only sent when the caller asks for it,
   because sending it undoes any of the individual voice settings. */
THIS int32_t es_setCurrentState(ECIstate *s, EngineWrapper *engine,
                                int32_t with_voice)
{
    int32_t ok = 1;
    int32_t i;

    for (i = 0; ok && i < 20; i++) {
        if (i == 16) {
            if (with_voice)
                ok = es_paramToEngine(s, i, engine);
        } else {
            ok = es_paramToEngine(s, i, engine);
        }
    }

    if (ok && s->spr) {
        if (engine->vt->processRemaining(engine, "`espr1 `esp0"))
            ok = 0;
    } else if (ok && !s->spr) {
        if (engine->vt->processRemaining(engine, "`espr0 `esp1"))
            ok = 0;
    }
    return ok;
}

THIS void es_paramFromEngine(ECIstate *s, int32_t p, int32_t v)
{
    void *m = s->mutex;

    sy_mutexWait(m, -1);
    s->param[p] = v;
    sy_mutexRelease(m);
}

/* Nought asks for a general parameter, one for a voice parameter, and each
   answers only for the range that belongs to it. */
THIS int32_t es_getParam(ECIstate *s, int32_t kind, int32_t p, int32_t *out)
{
    void *m = s->mutex;
    int32_t rc = -3;

    sy_mutexWait(m, -1);
    if (out != 0) {
        rc = 0;
        *out = -1;
        if (kind == 0) {
            if (p >= 0 && (p <= 4 || (p > 12 && p <= 16)))
                *out = s->param[p];
            else
                rc = ECI_BAD_PARAM;
        } else if (kind == 1) {
            if (p >= 5 && p <= 12)
                *out = s->param[p];
            else
                rc = ECI_BAD_PARAM;
        } else {
            rc = ECI_BAD_PARAM;
        }
    }
    sy_mutexRelease(m);
    return rc;
}

/* ---- setting one --------------------------------------------------- */

static THIS int32_t setGeneral(ECIstate *s, int32_t p, int32_t v,
                               void *thread, int32_t extra)
{
    int32_t rc = 0;

    switch (p) {
    case 0:
        if (v < 0 || v > 3)
            return ECI_BAD_PARAM;
        rc = es_sendAnnotationInt(s, "ts", v, thread);
        st_changeRomParam(thread, p, v);
        return rc;

    case 1:
        if (v < 0 || v > 1)
            return ECI_BAD_PARAM;
        return es_sendAnnotationInt(s, "ty", v, thread);

    case 2: {
        LangIdentifier want;

        want.packed = v;
        want.packed = v;
        lang_setString(&want);
        return st_changeLanguage(thread, &want);
    }

    case 3:
        if (v < 0 || v > 1)
            return ECI_BAD_PARAM;
        st_changeRomParam(thread, p, v);
        return es_sendAnnotationInt(s, "da", v, thread);

    case 4:
        if (v < 0 || v > 1)
            return ECI_BAD_PARAM;
        return st_setPhonemeIndicies(thread, v);

    case 13:
        if (v < 0 || v > 1)
            return ECI_BAD_PARAM;
        return es_sendAnnotationInt(s, "pp", v, thread);

    case 14:
        s->param[p] = v;
        return st_changeRomParam(thread, p, v);

    case 15:
        return st_changeRomParam(thread, p, v);

    case 16:
        if (v > 8)
            return 0;
        rc = st_changeVoice(thread, v);
        if (rc == 0) {
            rc = es_sendAnnotationInt(s, "v", v, thread);
            s->param[p] = v;
        }
        return rc;

    case 19:
        return st_changeFilter(thread, p, v, extra, 0);

    case 20:
        if (extra == 1) {
            void *made = 0;

            stm_newFilter(thread, 0, v, &made);
            if (made != 0)
                rc = stm_activateFilterByIdFlag(thread, (uint32_t)v, 1);
            return rc;
        }
        rc = stm_deactivateFilterByIdFlag(thread, (uint32_t)v, 1);
        if (rc == 0 && stm_deleteFilterByNumbers(thread, 0, v) == 0)
            rc = 0;
        return rc;

    default:
        return ECI_BAD_PARAM;
    }
}

/* A voice parameter that the synthesizer also has to be told about is set
   there first; the annotation only goes in if that worked. The three
   string ones take a copy first, because the call that follows may free
   what it was handed. */
static THIS int32_t setVoice(ECIstate *s, int32_t p, int32_t v, void *thread)
{
    int32_t rc = 0;
    char *copy;

    switch (p) {
    case 5:
        if (v < 0 || v > 1)
            return ECI_BAD_PARAM;
        return es_sendAnnotationInt(s, "vg", v, thread);

    case 6:
        return es_sendAnnotationInt(s, "vh", v, thread);

    case 7:
        rc = st_changePitch(thread, p, v);
        if (rc == 0)
            rc = es_sendAnnotationInt(s, "vb", v, thread);
        return rc;

    case 8:
        rc = st_changeFluctuation(thread, p, v);
        if (rc == 0)
            rc = es_sendAnnotationInt(s, "vf", v, thread);
        return rc;

    case 9:
        return es_sendAnnotationInt(s, "vr", v, thread);

    case 10:
        return es_sendAnnotationInt(s, "vy", v, thread);

    case 11:
        rc = st_changeSpeed(thread, p, v);
        if (rc == 0)
            rc = es_sendAnnotationInt(s, "vs", v, thread);
        return rc;

    case 12:
        /* Volume is told to the synthesizer before the annotation when the
           thread has no output of its own, and after it when it has. */
        if (ST_CORPORA((SynthThread *)thread) == 0)
            rc = st_changeVolume(thread, p, v);
        if (rc == 0)
            rc = es_sendAnnotationInt(s, "vv", v, thread);
        if (ST_CORPORA((SynthThread *)thread) != 0)
            rc = st_changeVolume(thread, p, v);
        return rc;

    case 13:
        return st_changeETIEmphasis(thread);

    case 30:
        copy = strdup((char *)(size_t)v);
        rc = st_changeSpeedString(thread, p, v);
        if (rc == 0)
            rc = es_sendAnnotationStr(s, "vs", copy, thread);
        free(copy);
        return rc;

    case 31:
        copy = strdup((char *)(size_t)v);
        rc = st_changePitchString(thread, p, v);
        if (rc == 0)
            rc = es_sendAnnotationStr(s, "vb", copy, thread);
        free(copy);
        return rc;

    case 32:
        copy = strdup((char *)(size_t)v);
        rc = st_changeFluctuationString(thread, p, v);
        if (rc == 0)
            rc = es_sendAnnotationStr(s, "vf", copy, thread);
        free(copy);
        return rc;

    default:
        return ECI_BAD_PARAM;
    }
}

THIS int32_t es_setParam(ECIstate *s, int32_t kind, int32_t p, int32_t v,
                         void *thread, int32_t extra)
{
    if (kind == 0)
        return setGeneral(s, p, v, thread, extra);
    if (kind == 1)
        return setVoice(s, p, v, thread);
    return ECI_BAD_PARAM;
}

/* ---- starting up ------------------------------------------------------ */

/* A section name in the settings file is a number in brackets, and this
   reads it as tenths so that one and two tenths sorts after one. */
static THIS int32_t es_iniToIntTimes10(ECIstate *s, char *name)
{
    char *p;
    int32_t v;
    char *dot;

    (void)s;
    if (name == 0 || *name == 0)
        return 0;
    p = name + 1;
    v = atoi(p) * 10;
    dot = strchr(p, '.');
    if (dot != 0)
        v += atoi(dot + 1);
    return v;
}

/* Name a language, dropping the brackets the settings file puts round it. */
static LangIdentifier *langFromSection(const char *section)
{
    size_t n = strlen(section);
    char *name = cpp_new((uint32_t)(n + 1));
    LangIdentifier *l = 0;

    if (name == 0)
        return 0;
    strncpy(name, section + 1, n - 2);
    name[n - 2] = 0;
    l = cpp_new(sizeof *l);
    if (l != 0)
        lang_ctor(l, name);
    cpp_delete(name);
    return l;
}

/* Settle on a language and get the thread going with it.

   Asked for one, that is the one. Asked for none, the settings file is
   read and its lowest-numbered section tried; if the thread will not start
   on that one, the next lowest is tried, and so on until one works or
   there are none left. */
THIS int32_t es_setInitialState(ECIstate *s, void *thread, int32_t lang)
{
    uint8_t ini[INI_BYTES];
    int32_t rc = ECI_FAILED;
    char high[8];
    char low[8];
    char *high_name = high;
    char *low_name = low;
    int found = 0;

    ini_ctor(ini);
    if (thread == 0) {
        ini_dtor(ini);
        return ECI_FAILED;
    }

    if (s->lang != 0) {
        cpp_delete(s->lang);
        s->lang = 0;
    }
    if (lang != 0) {
        LangIdentifier *l = cpp_new(sizeof *l);

        if (l != 0) {
            l->packed = lang;
            l->packed = lang;
            lang_setString(l);
        }
        s->lang = l;
    }

    if (s->lang == 0) {
        const char *first = ini_getFirstSection(ini);

        rc = ECI_NO_LANG;
        if (first != 0) {
            for (;;) {
                const char *next = ini_getNextSection(ini);

                if (next == 0)
                    break;
                if (strcmp(next, first) < 0)
                    first = next;
            }
            s->lang = langFromSection(first);
        }
    }

    if (s->lang != 0)
        rc = stl_initialize(thread, s->lang);
    if (rc != 0 && lang != 0) {
        ini_dtor(ini);
        return rc;
    }

    strcpy(high, "[10.0]");
    strcpy(low, "[0.0]");

    while (rc != 0 && !found) {
        const char *sec;
        int32_t here, hi, lo;

        high_name = high;
        sec = ini_getFirstSection(ini);
        here = es_iniToIntTimes10(s, (char *)sec);
        hi = es_iniToIntTimes10(s, high_name);
        lo = es_iniToIntTimes10(s, low_name);

        while (sec != 0) {
            if (here > lo && here < hi) {
                high_name = (char *)sec;
                hi = es_iniToIntTimes10(s, high_name);
            }
            sec = ini_getNextSection(ini);
            here = es_iniToIntTimes10(s, (char *)sec);
        }

        if (strcmp(high_name, low_name) == 0)
            found = 1;
        if (found)
            continue;

        if (s->lang != 0) {
            cpp_delete(s->lang);
            s->lang = 0;
        }
        strncpy(low_name, high_name, strlen(high_name));
        s->lang = langFromSection(high_name);
        if (s->lang != 0)
            rc = stl_initialize(thread, s->lang);
    }

    ini_dtor(ini);
    return rc;
}

ALIAS("??0LangIdentifier@@QAE@PBD@Z", "lang_ctor");
WEAK_ALIAS("?setString@LangIdentifier@@AAEXXZ", "lang_setString");
ALIAS("?setInitialState@ECIstate@@QAEJPAVSynthThread@@W4ECILanguageDialect@@@Z",
      "es_setInitialState");
ALIAS("?setCurrentState@ECIstate@@QAEHPAVEngineWrapper@@H@Z",
      "es_setCurrentState");
ALIAS("?setParam@ECIstate@@QAEJJJJPAVSynthThread@@J@Z", "es_setParam");
ALIAS("?paramToEngine@ECIstate@@QAEHJPAVEngineWrapper@@@Z", "es_paramToEngine");
ALIAS("?paramFromEngine@ECIstate@@QAEXJJ@Z", "es_paramFromEngine");
ALIAS("?getParam@ECIstate@@QAEJJJPAJ@Z", "es_getParam");
ALIAS("?setStandardVoice@ECIstate@@QAEJJPAVSynthThread@@@Z",
      "es_setStandardVoice");
ALIAS("?sendAnnotation@ECIstate@@AAEJPBDJPAVSynthThread@@@Z",
      "es_sendAnnotationInt");
ALIAS("?sendAnnotation@ECIstate@@AAEJPBDPADPAVSynthThread@@@Z",
      "es_sendAnnotationStr");
ALIAS("?iniToIntTimes10@ECIstate@@AAEHPAD@Z", "es_iniToIntTimes10");
