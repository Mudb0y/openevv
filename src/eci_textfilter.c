/* What happens to text on the way in.

   Everything a caller hands to eciAddText comes through here before Delta
   sees a character of it. The job is to find the annotations — a backtick
   and a short code — pull each one out, turn it into a parameter change,
   and hand the words between them to the synthesis thread.

   It is done in two passes because a language change has to be handled
   before anything else. The first pass looks only for that, and hands each
   run of text between language changes to the global filter chain, which
   may rewrite it. The second pass walks what comes back and deals with
   every other annotation.

   An annotation is blanked out with spaces rather than removed, so that
   nothing after it moves; the reader was going to skip spaces anyway. A
   backtick with a backslash in front of it is a literal backtick and is
   left alone.

   Transcribed rather than rewritten. Everything here decides which
   characters reach Delta, and the grammar is the language's own. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "evv_abi.h"
#include "evv_arena.h"


typedef struct { int32_t packed; char text[12]; char pad_10[4]; }
    LangIdentifier;

typedef struct {
    void *thread;     /* +0x00 */
    void *state;      /* +0x04 */
} TextFilter;

/* What an annotation turns out to be. The numbers are the parameter each
   one sets, which is why they are not in any order of their own. */
#define ANN_NONE      0
#define ANN_PITCH     0x07
#define ANN_FLUCT     0x08
#define ANN_SPEED     0x0b
#define ANN_VOLUME    0x0c
#define ANN_PAUSE     0x0d
#define ANN_VOICE     0x10
#define ANN_MODE      0x13
#define ANN_LANGUAGE  0x14
#define ANN_SPEED_S   0x1e
#define ANN_PITCH_S   0x1f
#define ANN_FLUCT_S   0x20

#define ANNOTATION '`'
#define ESCAPE     '\\'

extern void *cpp_new(uint32_t n) MANGLED("??2@YAPAXI@Z");
extern void  cpp_delete(void *p) MANGLED("??3@YAXPAX@Z");

extern THIS int32_t es_getParam(void *s, int32_t k, int32_t p, int32_t *out)
    MANGLED("?getParam@ECIstate@@QAEJJJPAJ@Z");
extern THIS int32_t es_setParam(void *s, int32_t k, int32_t p, int32_t v,
                                void *t, int32_t extra)
    MANGLED("?setParam@ECIstate@@QAEJJJJPAVSynthThread@@J@Z");
extern THIS int32_t st_addText(void *t, char *s, uint32_t n, int32_t flag)
    MANGLED("?addText@SynthThread@@QAEJPADKH@Z");
extern THIS char *stw_filterText(void *t, const char *s, int32_t n)
    MANGLED("?filterText@SynthThread@@QAEPADPBDJ@Z");
extern THIS int32_t st_insertStringIndex(void *t, char *s)
    MANGLED("?insertIndex@SynthThread@@QAEJPAD@Z");
extern THIS int32_t st_insertAudioIndex(void *t, char *s)
    MANGLED("?insertAudioIndex@SynthThread@@QAEJPAD@Z");
extern THIS void lang_setString(LangIdentifier *l)
    MANGLED("?setString@LangIdentifier@@AAEXXZ");

/* ---- which languages are written two bytes to a character ------------- */

/* The ranges are the original's own list, and there is no rule behind them
   to shorten it with. */
static int isMultiByte(int32_t lang)
{
    if (lang > 0x80800) {
        if (lang < 0xb0000)
            return 0;
        if (lang <= 0xb0001)
            return 1;
        if (lang <= 0xb07ff)
            return 0;
        return lang <= 0xb0801;
    }
    if (lang == 0x80800)
        return 1;
    if (lang < 0x60000)
        return 0;
    if (lang <= 0x60001)
        return 1;
    if (lang <= 0x607ff)
        return 0;
    if (lang <= 0x60801)
        return 1;
    return lang == 0x80000;
}

static int32_t currentLanguage(TextFilter *f)
{
    int32_t lang = 0;

    es_getParam(f->state, 0, 2, &lang);
    return lang;
}

/* ---- reading one annotation ------------------------------------------- */

/* A string-valued annotation: `vs"..."`, and the two like it. What it
   answers is a copy the caller owns. */
static char *readQuoted(const char *from, char *out)
{
    while (*from != 0 && *from != '"')
        *out++ = *from++;
    *out = 0;
    return out;
}

/* The one a caller uses to be told when a point in the text is spoken:
   a backtick, u, i, and the mark in quotes. */
static THIS int32_t tf_stringCallback(TextFilter *f, char *text, int32_t *skip,
                                      char **out)
{
    char *p = text;
    int32_t rc = -1;

    (void)f;
    *skip = 0;
    *out = 0;
    p++;
    if (*p == 0 || *p != 'u')
        return rc;

    p++;
    if (*p != 0) {
        if (*p == 'i') {
            size_t room = strlen(text) + 1;

            *out = cpp_new((uint32_t)room);
            if (*out == 0)
                return rc;
            memset(*out, 0, room);
        }
        p++;
        if (*p != 0) {
            char opened = *p;

            p++;
            /* Only a quoted mark is a mark. Anything else and there is
               nowhere to put it, which in the original means writing
               through nothing; here it simply stops. */
            if (opened == '"' && *out != 0) {
                readQuoted(p, *out);
                while (*p != 0 && *p != '"')
                    p++;
                if (*p == '"') {
                    rc = 0;
                    p++;
                }
            }
        }
    }
    *skip = (int32_t)(p - text);
    return rc;
}

/* The same again for a mark that is reported when the audio reaches it
   rather than when the text does: a backtick, a, u, d, and a quoted mark. */
static THIS int32_t tf_audioCallback(TextFilter *f, char *text, int32_t *skip,
                                     char **out)
{
    char *p = text;
    int32_t rc = -1;

    (void)f;
    *skip = 0;
    *out = 0;
    p++;
    if (*p == 0 || *p != 'a')
        return rc;

    p++;
    if (*p != 0 && *p == 'u') {
        p++;
        if (*p != 0) {
            if (*p == 'd') {
                size_t room = strlen(text) + 1;

                *out = cpp_new((uint32_t)room);
                if (*out == 0)
                    return rc;
                memset(*out, 0, room);
            }
            p++;
            if (*p != 0) {
                char opened = *p;

                p++;
                if (opened == '"' && *out != 0) {
                    readQuoted(p, *out);
                    while (*p != 0 && *p != '"')
                        p++;
                    if (*p == '"') {
                        rc = 0;
                        p++;
                    }
                }
            }
        }
    }
    *skip = (int32_t)(p - text);
    return rc;
}

/* A language named as a number with a dot in it, as in `l1.2. */
THIS LangIdentifier *tf_languageAnnotation(TextFilter *f, char *text,
                                           int32_t *skip)
{
    static LangIdentifier want;
    char *p = text;
    int32_t major = 0, minor = 0;

    (void)f;
    *skip = 0;
    p++;
    if (*p == 0 || *p != 'l')
        return 0;

    p++;
    if (*p == 0 || *p < '0' || *p > '9')
        return 0;
    sscanf(p, "%d", &major);
    while (*p >= '0' && *p <= '9')
        p++;

    if (*p == '.' ) {
        char *after = p + 1;

        if (*after >= '0' && *after <= '9') {
            sscanf(after, "%d", &minor);
            p = after;
            while (*p >= '0' && *p <= '9')
                p++;
        }
    }

    want.packed = (major << 16) | minor;
    lang_setString(&want);
    *skip = (int32_t)(p - text);
    return &want;
}

/* Everything else. Answers nought and fills in what it found, or leaves the
   kind at nothing when the text after the backtick means none of them. */
static THIS int32_t tf_annotations(TextFilter *f, char *text, int32_t *skip,
                                   uint32_t *kind, uint32_t *value,
                                   uint32_t *extra)
{
    char *p = text;
    int32_t rc = 0;
    int32_t n = 0;

    (void)f;
    *kind = 0;
    *value = 0;
    *extra = 0;
    *skip = 0;

    p++;
    if (*p == 0)
        return rc;

    /* A digit on its own is a pause, and a doubled nought is one too. */
    if (*p >= '0' && *p <= '4') {
        if (*p == '0' && p[1] == '0')
            p++;
        p++;
        *kind = ANN_PAUSE;
        *skip = (int32_t)(p - text);
        return rc;
    }

    if (*p == 'g' || *p == 'f') {
        /* The language and the reading mode share a shape: a letter saying
           whether the change is for good or for this sentence only, and a
           number. */
        int lang = (*p == 'g');
        char how = 0;
        int32_t v = 0;

        p++;
        if (*p == 0) {
            rc = -1;
        } else if (sscanf(p, lang ? "f%c%d%n" : "%c%d%n", &how, &v, &n) == 2) {
            p += 2;
            while (*p >= '0' && *p <= '9')
                p++;
            if (how != 'a' && how != 'd') {
                rc = -1;
            } else {
                *kind = lang ? ANN_LANGUAGE : ANN_MODE;
                *value = (uint32_t)v;
                *extra = (how == 'd') ? 0 : 1;
            }
        } else if (sscanf(p, lang ? "f%c%n" : "%c%n", &how, &n) == 1) {
            if (how != 'a' && how != 'd') {
                rc = -1;
            } else {
                *kind = lang ? ANN_LANGUAGE : ANN_MODE;
                *value = 0;
                *extra = (how == 'd') ? 0 : 1;
                p += 2;
            }
        } else {
            rc = -1;
        }
        *skip = (int32_t)(p - text);
        return rc;
    }

    if (*p != 'v') {
        *skip = (int32_t)(p - text);
        return rc;
    }

    /* The voice family. A number on its own picks a standard voice; a
       letter first says which part of the voice, and what follows is
       either a number or a name. */
    p++;
    if (*p == 0)
        return rc;

    if (*p >= '0' && *p <= '9') {
        int32_t v = 0;

        if (sscanf(p, "%d%n", &v, &n) == 1) {
            while (*p >= '0' && *p <= '9')
                p++;
            *kind = ANN_VOICE;
            *value = (uint32_t)v;
        } else {
            rc = -1;
        }
    } else {
        uint32_t as_number = 0, as_name = 0;
        char letter = *p;

        switch (letter) {
        case 'v': as_number = ANN_VOLUME; break;
        case 's': as_number = ANN_SPEED;  as_name = ANN_SPEED_S; break;
        case 'b': as_number = ANN_PITCH;  as_name = ANN_PITCH_S; break;
        case 'f': as_number = ANN_FLUCT;  as_name = ANN_FLUCT_S; break;
        default:  break;
        }
        if (as_number != 0) {
            int32_t v = 0;

            p++;
            if (sscanf(p, "%d%n", &v, &n) == 1) {
                while (*p >= '0' && *p <= '9')
                    p++;
                *kind = as_number;
                *value = (uint32_t)v;
            } else if (as_name != 0) {
                char name[100];

                memset(name, 0, sizeof name);
                sscanf(p, "%s%n", name, &n);
                *kind = as_name;
                *value = (uint32_t)(size_t)strdup(name);
            } else {
                rc = -1;
            }
        }
    }
    *skip = (int32_t)(p - text);
    return rc;
}

/* ---- the second pass -------------------------------------------------- */

/* Hand everything since the last annotation to the thread. */
static int32_t flushWords(TextFilter *f, char **last, char *p, int32_t flag,
                          int *stop, int32_t *rc)
{
    int32_t err;

    if (p == *last)
        return 0;
    err = st_addText(f->thread, *last, (uint32_t)(p - *last), flag);
    if (err != 0) {
        *stop = 1;
        *rc = err;
    }
    *last = p;
    return err;
}

/* Blank the annotation out and carry on from the end of it. */
static void blankOut(char *last, int32_t skip, char **p)
{
    memset(last, ' ', (size_t)skip);
    *p = last + skip - 1;
}

static THIS int32_t tf_processText(TextFilter *f, char *text, uint32_t len,
                                   int32_t flag)
{
    int32_t rc = 0;
    int stop = 0;
    char *p = text;
    char *last = text;
    int mbcs = isMultiByte(currentLanguage(f));

    while (!stop && *p != 0 && (uint32_t)(p - text) < len) {
        if (flag != 0 && *p == ANNOTATION
            && (p == text || p[-1] != ESCAPE)) {
            int32_t skip = 0;
            uint32_t kind = 0, value = 0, extra = 0;
            int32_t set = 0;

            tf_annotations(f, p, &skip, &kind, &value, &extra);
            switch (kind) {
            case ANN_MODE:
            case ANN_LANGUAGE:
                flushWords(f, &last, p, flag, &stop, &rc);
                rc = es_setParam(f->state, 0, (int32_t)kind, (int32_t)value,
                                 f->thread, (int32_t)extra);
                if (rc == 0)
                    blankOut(last, skip, &p);
                set = 1;
                break;

            case ANN_VOICE:
                flushWords(f, &last, p, flag, &stop, &rc);
                rc = es_setParam(f->state, 0, ANN_VOICE, (int32_t)value,
                                 f->thread, 0);
                set = 1;
                break;

            case ANN_VOLUME:
                flushWords(f, &last, p, flag, &stop, &rc);
                rc = es_setParam(f->state, 1, ANN_VOLUME, (int32_t)value,
                                 f->thread, 0);
                set = 1;
                break;

            case ANN_SPEED:
            case ANN_PITCH:
            case ANN_FLUCT:
            case ANN_SPEED_S:
            case ANN_PITCH_S:
            case ANN_FLUCT_S:
                flushWords(f, &last, p, flag, &stop, &rc);
                rc = es_setParam(f->state, 1, (int32_t)kind, (int32_t)value,
                                 f->thread, (int32_t)extra);
                set = 1;
                break;

            default:
                break;
            }

            if (!set) {
                /* Not a parameter. It may still be one of the two marks. */
                char *mark = 0;
                int32_t got;

                got = tf_stringCallback(f, p, &skip, &mark);
                if (got == 0 && skip > 0 && mark != 0) {
                    flushWords(f, &last, p, flag, &stop, &rc);
                    st_insertStringIndex(f->thread, mark);
                    cpp_delete(mark);
                    blankOut(last, skip, &p);
                } else {
                    if (mark != 0)
                        cpp_delete(mark);
                    mark = 0;
                    got = tf_audioCallback(f, p, &skip, &mark);
                    if (got == 0 && skip > 0 && mark != 0) {
                        flushWords(f, &last, p, flag, &stop, &rc);
                        st_insertAudioIndex(f->thread, mark);
                        cpp_delete(mark);
                        blankOut(last, skip, &p);
                    } else if (mark != 0) {
                        cpp_delete(mark);
                    }
                }
            }
        }

        /* A lead byte and its trailer count as one character. */
        if (mbcs && (unsigned char)*p > 0x80)
            p++;
        if (*p != 0)
            p++;
    }

    if ((*p == 0 || (uint32_t)(p - text) >= len) && p - last > 0) {
        int32_t err = st_addText(f->thread, last, (uint32_t)(p - last), flag);

        if (err != 0)
            rc = err;
    }
    return rc;
}

/* ---- the first pass --------------------------------------------------- */

/* Hand one run of text to the filter chain and then to the second pass.
   The chain is given a copy, because it is allowed to keep what it is
   given for as long as it likes. */
static int32_t runThroughFilters(TextFilter *f, char *from, char *to,
                                 int32_t flag)
{
    size_t room = (size_t)(to - from) + 1;
    char *copy = cpp_new((uint32_t)room);
    char *filtered;
    uint32_t n;
    int32_t err;

    if (copy != 0) {
        memset(copy, 0, room);
        strncpy(copy, from, room - 1);
    }
    filtered = stw_filterText(f->thread, copy, 0);
    n = filtered ? (uint32_t)strlen(filtered) : 0;
    err = tf_processText(f, filtered, n, flag);
    if (copy != 0)
        cpp_delete(copy);
    return err;
}

static THIS int32_t tf_globalFilters(TextFilter *f, char *text, int32_t len,
                                     int32_t flag)
{
    int32_t rc = 0;
    int32_t err = 0;
    int stop = 0;
    char *p = text;
    char *last = text;
    int mbcs = isMultiByte(currentLanguage(f));

    while (!stop && *p != 0 && (p - text) < len) {
        if (flag != 0 && *p == ANNOTATION
            && (p == text || p[-1] != ESCAPE)) {
            int32_t skip = 0;
            uint32_t kind = 0, value = 0, extra = 0;

            tf_annotations(f, p, &skip, &kind, &value, &extra);
            if (kind == ANN_LANGUAGE) {
                if (p != last) {
                    err = runThroughFilters(f, last, p, flag);
                    if (err != 0) {
                        stop = 1;
                        rc = err;
                    }
                    last = p;
                }
                rc = es_setParam(f->state, 0, ANN_LANGUAGE, (int32_t)value,
                                 f->thread, (int32_t)extra);
                if (rc == 0)
                    blankOut(last, skip, &p);
            } else if (kind >= ANN_SPEED_S && kind <= ANN_FLUCT_S) {
                /* The second pass will read this one again; the name it
                   copied out is not wanted twice. */
                free((void *)(size_t)value);
            }
        }

        if (mbcs && (unsigned char)*p > 0x80)
            p++;
        if (*p != 0)
            p++;
    }

    if ((*p == 0 || (p - text) >= len) && p - last > 0) {
        err = runThroughFilters(f, last, p, flag);
        if (err != 0)
            rc = err;
    }
    return err;
}

/* ---- what the instance calls ------------------------------------------ */

THIS TextFilter *tf_ctor(TextFilter *f)
{
    return f;
}

THIS void tf_dtor(TextFilter *f)
{
    (void)f;
}

/* Only one text type is understood; anything else is refused rather than
   guessed at. */
#define ECI_WRONG_TYPE (-8)

THIS int32_t tf_addText(TextFilter *f, void *text, int32_t len, int32_t unused,
                        int32_t flag, int32_t type, void *state, void *thread)
{
    (void)unused;
    f->state = state;
    f->thread = thread;
    if (type != 0)
        return ECI_WRONG_TYPE;
    return tf_globalFilters(f, (char *)text, len, flag);
}


ALIAS("??0TextFilter@@QAE@XZ", "tf_ctor");
ALIAS("??1TextFilter@@QAE@XZ", "tf_dtor");
ALIAS("?addText@TextFilter@@QAEJPAXJJJJPAVECIstate@@PAVSynthThread@@@Z",
      "tf_addText");
ALIAS("?processGlobalFilters@TextFilter@@AAEJPADJJ@Z", "tf_globalFilters");
ALIAS("?processText@TextFilter@@AAEJPADKJ@Z", "tf_processText");
ALIAS("?processAnnotations@TextFilter@@AAEJPADPAHPAK22@Z", "tf_annotations");
ALIAS("?processLanguageAnnotation@TextFilter@@AAEPAVLangIdentifier@@PADPAH@Z",
      "tf_languageAnnotation");
ALIAS("?processAudioCallbackAnnotation@TextFilter@@AAEJPADPAHPAPAD@Z",
      "tf_audioCallback");
ALIAS("?processStringCallbackAnnotation@TextFilter@@AAEJPADPAHPAPAD@Z",
      "tf_stringCallback");
