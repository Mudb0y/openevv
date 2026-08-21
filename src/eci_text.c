/* Text on its way in, and the annotations buried in it.

   A caller hands over a string. Two things then happen to it. If the caller
   asked for the queued kind of working, the string is copied into a queue
   element along with the settings in force at the moment it arrived, and
   nothing is sent down until synthesis is asked for; that way a program can
   set a voice, add some words, set another voice, add some more, and have
   each stretch spoken in the voice that was current when it was written. If
   the caller did not ask for that, the string goes straight down.

   Either way it is read for annotations first. An annotation is a backtick
   followed by a letter and usually a number, and it says the same thing a
   call to set a parameter would say. The engine acts on them itself further
   down, so this pass does not remove them; it mirrors them into the records
   kept here, so that those records still describe what is being spoken.

   The one thing it does rewrite is a number given in real-world units. The
   engine only understands its own, so the number is converted and written
   back over the original in place. It has to fit in the room the original
   took, which is why there is a digit count and a fallback to a row of
   nines.

   Names are prefixed and the aliases at the foot carry the real ones. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "eci_synththread.h"
#include "evv_abi.h"
#include "evv_arena.h"
#include "eci_old.h"

/* Which of the eighteen settings say how text is to be handled. */
#define ENV_SYNTHMODE   0
#define ENV_INPUTTYPE   1
#define ENV_RATE        5
#define ENV_LANGUAGE    9
#define ENV_VOICENO     17

/* One thing waiting on the queue, with the settings it arrived under. */
typedef struct QueueElement {
    int32_t  kind;              /* +0x00, nought text, one an index mark */
    char    *text;              /* +0x04, or the index number */
    int32_t  env[0x12];         /* +0x08 */
    int32_t  voice[0x14];       /* +0x50 */
    struct QueueElement *next;  /* +0xa0 */
} QueueElement;

/* The offsets above are the original's, and are what an element can be
   checked against; they are not what one costs here. IBM's element is 0xa4
   bytes because a pointer was four of them, and on a wider host this struct
   is twelve bytes longer. Allocating the original's count wrote the tail of
   the voice array and the whole of the link past the end of the block. */

/* The tables of voices, laid out by family, dialect, rate and voice. */
#define CV_FAMILY_BYTES  0x1e18
#define CV_DIALECT_BYTES 0x0f0c
#define CV_RATE_BYTES    0x0504
#define CV_VOICE_BYTES   0x0050
#define CV_PRESENT       0x44
#define SV_FAMILY_BYTES  0x0a08
#define SV_DIALECT_BYTES 0x0504
#define SV_FIRST         4

/* Where a voice keeps the eight things an annotation may move. */
#define VOICE_PARAM(v, i) (*(int32_t *)((char *)(v) + 0x20 + (i) * 4))

extern int32_t STDCALL api_add_text(void *h2, const char *s, int32_t len,
                                     int32_t a, int32_t annotate, int32_t b)
    MANGLED("_eciAddText2@24");
extern int32_t STDCALL api_insert_index(void *h2, int32_t n)
    MANGLED("_eciInsertIndex2@8");
extern int32_t STDCALL api_block(void *h2) MANGLED("_eciBlock2@4");
extern int32_t STDCALL api_unblock(void *h2) MANGLED("_eciUnblock2@4");
extern int32_t STDCALL api_synthesize(void *h2)
    MANGLED("_eciSynthesize2@4");
extern int lg_splitLanguageString(char *s, uint8_t *family, uint8_t *dialect,
                               uint8_t *extra)
    MANGLED("?splitLanguageString@@YAHPADPAE11@Z");
extern int realWorld2eci(int32_t realWorld, int32_t which, int32_t value,
                         int32_t lo, int32_t hi)
    MANGLED("?realWorld2eci@@YAHHW4ECIVoiceParam@@HHH@Z");
extern char standardVoices[] MANGLED("_standardVoices");
extern int CheckUnicodeHeaderTag(const char *text)
    MANGLED("_CheckUnicodeHeaderTag");
extern int CheckSSMLFilterActive(void *filterMgr)
    MANGLED("_CheckSSMLFilterActive");
extern unsigned UniStrlen(const uint16_t *s) MANGLED("?UniStrlen@@YAIPBG@Z");
extern int u8_convertUCS2toUTF8(const uint16_t *in, int n, uint8_t *out,
                             uint32_t *made)
    MANGLED("?ConvertUCS2toUTF8@@YAHPBGHPAEAAK@Z");
extern int UnicodeConverter(OldInst *h, const char *text, char **out,
                            int32_t want) MANGLED("_UnicodeConverter");
extern int enableFilter(OldInst *h, int32_t lang, char *text, char **out)
    MANGLED("_enableFilter");

typedef struct Environment { int32_t w[0x12]; } Environment;
typedef struct ECIVoice { int32_t w[0x14]; } ECIVoice;

extern int32_t setECIerror(int32_t rc, OldInst *h);
extern int ev_sendParameters(OldInst *h);
extern int ev_sendChangedEnvironment(OldInst *h, Environment env,
                                     int32_t force);
extern int ev_sendChangedActiveVoice(OldInst *h, ECIVoice v, int32_t force);
extern void eo_clearManualQueue(OldInst *h);
extern const int32_t ev_paramRange[0x12][2];
extern const int32_t ev_voiceParamRange[8][2];

void et_processAnnotations(void *concat, int32_t *voice, int32_t *env,
                           int32_t *voice2, int32_t *env2, int32_t realWorld,
                           char *text);

/* ---- finding a voice in one of the two tables ----------------------- */

static char *et_concatEntry(void *concat, int family, int dialect,
                            int32_t rate, int32_t voiceno)
{
    return (char *)concat
        + (family - 1) * CV_FAMILY_BYTES
        + dialect * CV_DIALECT_BYTES
        + rate * CV_RATE_BYTES
        + (voiceno - 1) * CV_VOICE_BYTES;
}

static char *et_standardEntry(int family, int dialect, int32_t voiceno)
{
    return standardVoices
        + (family - 1) * SV_FAMILY_BYTES
        + dialect * SV_DIALECT_BYTES
        + (voiceno - 1) * CV_VOICE_BYTES;
}

static int et_present(const char *entry)
{
    return *(const int32_t *)(entry + CV_PRESENT) != 0;
}

/* ---- reading the annotations ---------------------------------------- */

/* How many characters a number takes when written out. */
static int et_digits(int32_t v)
{
    int n = 0;

    do {
        n++;
        v /= 10;
    } while (v > 0);
    return n;
}

/* The largest number that will fit in the given room, all nines. */
static int32_t et_allNines(int room)
{
    int32_t v = 0;
    int i;

    for (i = 0; i < room; i++)
        v = v * 10 + 9;
    return v;
}

/* A voice setting given in the engine's own units. */
static void et_setVoiceParam(int32_t *voice, int32_t *voice2, int code,
                             int32_t value)
{
    VOICE_PARAM(voice, code) = value;
    if (voice2)
        VOICE_PARAM(voice2, code) = value;
}

/* An environment setting, checked against what that setting will take. */
static void et_setEnvParam(int32_t *env, int32_t *env2, int idx, int32_t value)
{
    if (value < ev_paramRange[idx][0] || value > ev_paramRange[idx][1])
        return;
    env[idx] = value;
    if (env2)
        env2[idx] = value;
}

/* Walk the text, acting on every annotation in it.

   Two indices run along the same buffer: one reading and one writing. They
   move together until a real-world number turns out to be shorter written in
   the engine's units than it was in the caller's, and from then on the write
   index trails and the text is being compacted behind it. */
void et_processAnnotations(void *concat, int32_t *voice, int32_t *env,
                           int32_t *voice2, int32_t *env2, int32_t realWorld,
                           char *text)
{
    int w = 0, r = 0, n = 0;
    int32_t value = 0;
    int code = 0;
    int idx = 0;

    while (text[r] != 0) {
        text[w] = text[r];

        if (text[r] != '`') {
            r++;
            w++;
            continue;
        }

        w++;
        r++;
        if (w != r)
            text[w] = text[r];
        n = 0;

        switch (text[r]) {

        case 'l': {     /* a language, named rather than numbered */
            uint8_t family = 0, dialect = 0, extra = 0;

            w++;
            r++;
            n = lg_splitLanguageString(text + r, &family, &dialect, &extra);
            if (n <= 0)
                continue;
            if (family < 1 || family > 0x12)
                break;
            if (dialect + 1 > 2)
                break;

            /* Only if that language is actually installed. */
            if (!et_present(et_concatEntry(concat, family, dialect,
                                           env[ENV_RATE], env[ENV_VOICENO]))
                && *(const int32_t *)(standardVoices
                                      + (family - 1) * SV_FAMILY_BYTES
                                      + dialect * SV_DIALECT_BYTES) != 1)
                break;

            {
                int32_t lang = ((int32_t)family << 16)
                             | ((int32_t)extra << 8) | dialect;

                if (env[ENV_LANGUAGE] != lang) {
                    int oldFamily =
                        (int8_t)((env[ENV_LANGUAGE] & 0xff0000) >> 16);
                    int oldDialect = (int8_t)(env[ENV_LANGUAGE] & 0xff);
                    char *was = et_concatEntry(concat, oldFamily, oldDialect,
                                               env[ENV_RATE],
                                               env[ENV_VOICENO]);
                    char *now = et_concatEntry(concat, family, dialect,
                                               env[ENV_RATE],
                                               env[ENV_VOICENO]);

                    if (et_present(was) || et_present(now)) {
                        if (et_present(now))
                            memcpy(voice, now + SV_FIRST, VOICE_BYTES);
                        else
                            memcpy(voice,
                                   et_standardEntry(family, dialect,
                                                    env[ENV_VOICENO])
                                   + SV_FIRST, VOICE_BYTES);
                    }
                    env[ENV_LANGUAGE] = lang;
                    if (env2)
                        env2[ENV_LANGUAGE] = lang;
                }
            }
            break;
        }

        case 'v':       /* a voice, either one of its eight settings or a
                           whole preset by number */
            r++;
            w++;
            text[w] = text[r];

            if (text[r] == 'g') {
                r++;
                w++;
                if (sscanf(text + r, "%i%n", &value, &n) != 1)
                    continue;
                if (value >= ev_voiceParamRange[0][0]
                    && value <= ev_voiceParamRange[0][1])
                    et_setVoiceParam(voice, voice2, 0, value);
                break;
            }

            if (text[r] == 'h' || text[r] == 'f' || text[r] == 'r'
                || text[r] == 'y') {
                code = text[r] == 'h' ? 1
                     : text[r] == 'f' ? 3
                     : text[r] == 'r' ? 4 : 5;
                r++;
                w++;
                if (sscanf(text + r, "%i%n", &value, &n) != 1)
                    continue;
                if (value > ev_voiceParamRange[code][1])
                    value = ev_voiceParamRange[code][1];
                if (value >= ev_voiceParamRange[code][0])
                    et_setVoiceParam(voice, voice2, code, value);
                break;
            }

            if (text[r] == 'b' || text[r] == 's' || text[r] == 'v') {
                code = text[r] == 'b' ? 2 : text[r] == 's' ? 6 : 7;
                r++;
                w++;
                if (sscanf(text + r, "%i%n", &value, &n) != 1)
                    continue;

                if (value < 0) {
                    /* Left as it stands, and copied across unchanged. */
                    int i;

                    for (i = 0; i < n; i++)
                        text[w + i] = text[r + i];
                    w += n;
                    r += n;
                    continue;
                }

                {
                    int32_t v = realWorld2eci(realWorld, code, value, 0, 250);
                    int room;
                    char after;

                    if (v > ev_voiceParamRange[code][1])
                        v = ev_voiceParamRange[code][1];

                    /* It has to be written back over the room the caller's
                       own number took. If it will not fit, the largest
                       number that will is used instead. */
                    room = et_digits(v);
                    if (room > n) {
                        room = n;
                        v = et_allNines(n);
                    }
                    et_setVoiceParam(voice, voice2, code, v);

                    after = text[w + room];
                    sprintf(text + w, "%i", v);
                    text[w + room] = after;
                    w += room;
                    r += n;
                    continue;
                }
            }

            /* Anything else is one of the eight voices, by number. */
            if (sscanf(text + r, "%i%n", &value, &n) != 1)
                continue;
            {
                int family = (int8_t)((env[ENV_LANGUAGE] & 0xff0000) >> 16);
                int dialect = (int8_t)(env[ENV_LANGUAGE] & 0xff);
                char *wanted, *current, *standard;

                if (value <= 0 || value > 8)
                    break;
                wanted = et_concatEntry(concat, family, dialect,
                                        env[ENV_RATE], value);
                standard = et_standardEntry(family, dialect, value);
                if (!et_present(wanted) && !et_present(standard))
                    break;

                current = et_concatEntry(concat, family, dialect,
                                         env[ENV_RATE], env[ENV_VOICENO]);
                if (et_present(wanted) || et_present(current)) {
                    if (et_present(wanted))
                        memcpy(voice, wanted + SV_FIRST, VOICE_BYTES);
                    else
                        memcpy(voice, standard + SV_FIRST, VOICE_BYTES);
                }
                if (voice2)
                    memcpy(voice2, voice, VOICE_BYTES);
                env[ENV_VOICENO] = value;
                if (env2)
                    env2[ENV_VOICENO] = value;
            }
            break;

        case 't':       /* how the text itself is to be read */
            r++;
            w++;
            text[w] = text[r];
            if (text[r] != 's' && text[r] != 'y')
                continue;
            idx = text[r] == 's' ? 2 : 10;
            r++;
            w++;
            if (sscanf(text + r, "%i%n", &value, &n) != 1)
                continue;
            et_setEnvParam(env, env2, idx, value);
            break;

        case 'd':       /* the dictionary */
            r++;
            w++;
            text[w] = text[r];
            if (text[r] != 'a')
                continue;
            idx = 3;
            r++;
            w++;
            if (sscanf(text + r, "%i%n", &value, &n) != 1)
                continue;
            et_setEnvParam(env, env2, idx, value);
            break;

        case 'p':       /* phrase prediction */
            r++;
            w++;
            text[w] = text[r];
            if (text[r] != 'p')
                continue;
            idx = 11;
            r++;
            w++;
            if (sscanf(text + r, "%i%n", &value, &n) != 1)
                continue;
            et_setEnvParam(env, env2, idx, value);
            break;

        default:
            continue;
        }

        /* While nothing has been squeezed out of the text, the number that
           was just read is stepped over on both sides at once. */
        if (w == r) {
            w += n;
            r += n;
        }
    }

    text[w] = text[r];
}

/* ---- the queue the caller may fill ---------------------------------- */

/* A stretch of text, with the settings that were in force when it arrived. */
QueueElement *et_createTextElement(OldInst *h, const char *text, int32_t len,
                                   int32_t annotate)
{
    QueueElement *e = calloc(1, sizeof *e);

    if (!e)
        return 0;

    e->kind = 0;
    e->text = calloc(1, len + 1);
    if (!e->text) {
        free(e);
        return 0;
    }
    strncpy(e->text, text, len);
    e->text[len] = 0;

    memcpy(e->env, OI_ENV(h), sizeof e->env);
    memcpy(e->voice, OI_VOICE(h), sizeof e->voice);

    if (annotate == 1)
        et_processAnnotations(OI_CONCAT(h), (int32_t *)OI_VOICE(h), OI_ENV(h),
                              0, 0, e->env[8], e->text);
    return e;
}

/* An index mark, likewise. */
QueueElement *et_createIndexElement(OldInst *h, int32_t n)
{
    QueueElement *e = calloc(1, sizeof *e);

    if (!e)
        return 0;

    e->kind = 1;
    e->text = (char *)(size_t)n;
    memcpy(e->env, OI_ENV(h), sizeof e->env);
    memcpy(e->voice, OI_VOICE(h), sizeof e->voice);
    return e;
}

void et_addToManualQueue(OldInst *h, QueueElement *e)
{
    if (OI_QHEAD(h) == 0)
        OI_QHEAD(h) = e;
    else
        OI_QTAIL(h)->next = e;
    OI_QTAIL(h) = e;
}

/* Empty the queue into the engine, one element at a time, each under the
   settings it was written with.

   The engine is blocked for the whole run so that nothing is spoken until
   the queue has been read out, and unblocked at the end whatever happened.
   A failure anywhere stops the run and throws away what is left. */
int et_processManualQueue(OldInst *h)
{
    QueueElement *e, *done;
    int blocked = 0;
    int ok = 1;
    int failed;

    if (OI_QHEAD(h) == 0)
        goto unblock;

    failed = 0;
    e = OI_QHEAD(h);
    if (e) {
        failed = 1;
        if (!setECIerror(api_block(OI_NEW(h)), h)) {
            blocked = 1;
            failed = 0;
        }
    }

    while (e && !failed) {
        if (OI_READY(h)) {
            if (ev_sendChangedEnvironment(h, *(Environment *)e->env, 1)
                && ev_sendChangedActiveVoice(h, *(ECIVoice *)e->voice, 1))
                OI_READY(h) = 0;
            else
                failed = 1;
        } else {
            if (!ev_sendChangedEnvironment(h, *(Environment *)e->env, 0)
                || !ev_sendChangedActiveVoice(h, *(ECIVoice *)e->voice, 0))
                failed = 1;
        }

        if (!failed) {
            if (e->kind == 0) {
                int annotate = (e->env[ENV_INPUTTYPE] == 1);
                int32_t rc;

                if (annotate)
                    et_processAnnotations(OI_CONCAT(h),
                                          (int32_t *)OI_VOICE_SAVED(h),
                                          OI_ENV_SAVED(h), 0, 0, 0, e->text);

                rc = setECIerror(api_add_text(OI_NEW(h), e->text,
                                             strlen(e->text), 0, annotate,
                                             0), h);
                /* Text the engine simply had no room for is not a failure. */
                if (rc != 0 && rc != -14)
                    failed = 1;
                free(e->text);
            } else if (e->kind == 1) {
                if (setECIerror(api_insert_index(OI_NEW(h),
                                                (int32_t)(size_t)e->text), h))
                    failed = 1;
            }
        }

        done = e;
        e = e->next;
        free(done);
    }

    if (failed) {
        OI_QHEAD(h) = e;
        eo_clearManualQueue(h);
        ok = 0;
    }
    OI_QHEAD(h) = 0;
    OI_QTAIL(h) = 0;

unblock:
    if (blocked)
        setECIerror(api_unblock(OI_NEW(h)), h);
    return ok;
}

/* ---- the entry points ----------------------------------------------- */

/* Refuse a call that arrived while another was still running. */
static int et_reentered(OldInst *h, uint32_t bit)
{
    if (!h || !OI_BUSY(h))
        return 0;
    OI_REFUSED(h) = bit;
    OI_REFUSEDALL(h) |= bit;
    return 1;
}

int STDCALL et_insertIndex(OldInst *h, int32_t n)
{
    OldInst *inst;

    if (et_reentered(h, 0x800))
        return 0;

    inst = h;
    if (!inst)
        return 1;

    if (OI_ENV(inst)[ENV_SYNTHMODE] == 1) {
        QueueElement *e = et_createIndexElement(inst, n);

        if (!e) {
            OI_REFUSED(inst) = 2;
            OI_REFUSEDALL(inst) |= 2;
            return 0;
        }
        et_addToManualQueue(inst, e);
        return 1;
    }

    if (!ev_sendParameters(inst))
        return 0;
    if (setECIerror(api_insert_index(OI_NEW(inst), n), inst))
        return 0;
    return 1;
}

int STDCALL et_synthesize(OldInst *h)
{
    OldInst *inst;

    if (et_reentered(h, 0x800))
        return 0;

    inst = h;
    if (!inst) {
        OI_REFUSED(h) = 0x80;
        OI_REFUSEDALL(h) |= 0x80;
        return 0;
    }

    if (!et_processManualQueue(inst))
        return 0;
    return setECIerror(api_synthesize(OI_NEW(inst)), inst) == 0;
}

/* Everything the entry point below may have allocated on its way through. */
static void et_letGo(char **filtered, int mine, char **s)
{
    if (*filtered) {
        cpp_delete(*filtered);
        *filtered = 0;
    }
    if (mine && *s) {
        free(*s);
        *s = 0;
    }
}

/* Take a stretch of text from the caller.

   Most of the work is deciding what the string actually is. It may be wide
   characters, in which case it is turned into the engine's own encoding. It
   may carry a byte order mark, which is stepped over. It may be claimed by a
   filter, which hands back a different string altogether. Only then is there
   something to queue or to send down.

   Note that the annotated path sends the string the filter and the byte
   order mark left, while the plain path sends the one that came in. That is
   what the original does. */
int STDCALL et_addText(OldInst *h, const char *text)
{
    OldInst *inst;
    char *s;
    char *bom = 0;
    char *filtered = 0;
    char *body;
    int32_t lang;
    int32_t len;
    int32_t rc;
    int mine = 0;
    int wide = 0;
    int annotate;

    if (et_reentered(h, 0x800))
        return 0;

    inst = h;
    s = (char *)text;
    lang = OI_LANG(inst);

    /* Three of the language families are written in wide characters. */
    if ((lang & 0xff00) == 0x800 || (lang & 0xff00) == 0x900
        || (lang & 0xff00) == 0xa00)
        wide = 1;

    if (wide && (CheckUnicodeHeaderTag(text)
                 || CheckSSMLFilterActive(OI_FILTERMGR(inst)))) {
        const uint16_t *u = (const uint16_t *)text;
        unsigned n = UniStrlen(u);
        uint32_t made;

        s = malloc(n * 2 + 1);
        if (!s)
            return 0;
        mine = 1;
        memset(s, 0, n * 2);
        made = n * 2;
        u8_convertUCS2toUTF8(u, n, (uint8_t *)s, &made);
        s[made] = 0;
    } else if (UnicodeConverter(h, text, &s, 1)) {
        OI_REFUSED(inst) = 0x1000;
        OI_REFUSEDALL(inst) |= 0x1000;
        return 0;
    }

    if (!s || s[0] == 0)
        return 1;

    if (strlen(s) > 2 && (uint8_t)s[0] == 0xef && (uint8_t)s[1] == 0xbb
        && (uint8_t)s[2] == 0xbf)
        bom = s + 3;

    len = strlen(bom ? bom : s);

    if (!inst || !s) {
        OI_REFUSED(inst) = 0x80;
        OI_REFUSEDALL(inst) |= 0x80;
        return 0;
    }

    annotate = (OI_ENV(inst)[ENV_INPUTTYPE] == 1);
    lang = OI_LANG(inst);

    /* A filter that claims the text turns annotations on whether or not the
       caller asked for them, because what it hands back is full of them. */
    if (enableFilter(inst, 0, bom ? bom : s, &filtered) == 1) {
        OI_ENV(inst)[ENV_INPUTTYPE] = 1;
        annotate = 1;
    }
    if (enableFilter(inst, lang, bom ? bom : s, &filtered) == 1) {
        OI_ENV(inst)[ENV_INPUTTYPE] = 1;
        annotate = 1;
    }

    body = filtered ? filtered : (bom ? bom : s);
    len = strlen(body);

    if (len == 0) {
        et_letGo(&filtered, mine, &s);
        return 1;
    }

    if (OI_ENV(inst)[ENV_SYNTHMODE] == 1) {
        QueueElement *e = et_createTextElement(inst, body, len,
                                               OI_ENV(inst)[ENV_INPUTTYPE]);

        if (!e) {
            OI_REFUSED(inst) = 2;
            OI_REFUSEDALL(inst) |= 2;
            et_letGo(&filtered, mine, &s);
            return 0;
        }
        et_addToManualQueue(inst, e);
        et_letGo(&filtered, mine, &s);
        return 1;
    }

    if (!et_processManualQueue(inst)) {
        et_letGo(&filtered, mine, &s);
        return 0;
    }
    if (!ev_sendParameters(inst)) {
        et_letGo(&filtered, mine, &s);
        return 0;
    }

    if (annotate) {
        char *copy = calloc(1, len + 1);

        if (!copy) {
            OI_REFUSED(inst) = 2;
            OI_REFUSEDALL(inst) |= 2;
            et_letGo(&filtered, mine, &s);
            return 0;
        }
        strncpy(copy, body, len);
        copy[len] = 0;

        et_processAnnotations(OI_CONCAT(inst), (int32_t *)OI_VOICE(inst),
                              OI_ENV(inst), (int32_t *)OI_VOICE_SAVED(inst),
                              OI_ENV_SAVED(inst), OI_ENV(inst)[8], copy);
        rc = setECIerror(api_add_text(OI_NEW(inst), copy, strlen(copy), 0,
                                     annotate, 0), inst);
        free(copy);
    } else {
        rc = setECIerror(api_add_text(OI_NEW(inst), s, strlen(s), 0, annotate,
                                     0), inst);
    }

    et_letGo(&filtered, mine, &s);
    /* Text the engine had no room for is not counted as a failure. */
    return (rc == 0 || rc == -14) ? 1 : 0;
}

ALIAS("?processAnnotations@@YAXPAVStandardConcatenativeVoices@@PAUECIVoice@@"
      "PAUEnvironment@@12HPAD@Z", "et_processAnnotations");
ALIAS("?createTextElement@@YAPAUQueueElement@@PAUoldECIInstData@@PBDHH@Z",
      "et_createTextElement");
ALIAS("?createIndexElement@@YAPAUQueueElement@@PAUoldECIInstData@@H@Z",
      "et_createIndexElement");
ALIAS("?addToManualQueue@@YAXPAUoldECIInstData@@PAUQueueElement@@@Z",
      "et_addToManualQueue");
ALIAS("?processManualQueue@@YAHPAUoldECIInstData@@@Z",
      "et_processManualQueue");

ALIAS_N("_eciInsertIndex@8", "et_insertIndex", 8);
ALIAS_N("_eciSynthesize@4", "et_synthesize", 4);
ALIAS_N("_eciAddText@8", "et_addText", 8);
