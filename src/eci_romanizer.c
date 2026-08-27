/* The romanizer manager.
 *
 * What this object is. The synthesis thread hands every scrap of text to a
 * romanizer manager before the engine sees it. For a language written in
 * another script that manager loads a romanizer and converts; for the rest
 * it is a pass-through that tidies the text and hands it on. US English
 * takes the second road, so the pass-through is live and matters, and the
 * conversion is dead.
 *
 * That was my mistake earlier in this work: I assumed a thing called a
 * romanizer was only for other scripts and stubbed the whole manager out.
 * The engine went silent. It is called seventeen times for addParam and ten
 * for processRemaining on a single sentence, and it is on the text path
 * proper.
 *
 * One finding worth keeping, and a correction to it. This file used to say
 * that getRomanizerInst loads a romanizer through Win32 LoadLibrary and
 * GetProcAddress, and that the loading half was therefore platform code to be
 * stubbed rather than transcribed. That is not what it does. It takes the
 * address of getRomObject, a link-time symbol that romedll_link.obj answers
 * when the romanizer is part of the program, and the only Win32 in it is
 * GetModuleFileNameA, asked for the directory the program was loaded from. So
 * it is transcribed below: eci_rom.h says what a romanizer is and
 * eci_romedll.c stands in for the linker's answer. getLibraryName is dead
 * either way -- it returns nothing at all in IBM's own code.
 */

#include <stdint.h>
#include <string.h>
#include "eci_synththread.h"
#include "evv_abi.h"
#include "eci_engine.h"
#include "delta_lang.h"
#include "eci_rom.h"

typedef struct SynthThread SynthThread;

/* The manager's own record. The two arrays are eighteen language families
   of two dialects each: one of romanizers held open, one of the names they
   were loaded from. */
typedef struct RomanizerManager {
    uint8_t       lock[0x0c];   /* +0x000 */
    IniFileReader ini;          /* +0x00c */
    /* Eighteen language families of two dialects each: one array of the names
       the romanizers were loaded from, one of the romanizers themselves. */
    char         *names[0x12][2];   /* +0x130 */
    /* Where IBM keeps the address of getRomObject, which it fetches every
       time it is about to ask for a romanizer. Ours keeps the maker that
       answered for the family being asked about, which is the same thing
       with the family said out loud. */
    EvvRomMaker   maker;            /* +0x1c0 */
    EvvRom       *active;           /* +0x1c4 */
    int32_t       last_flag;        /* +0x1c8 */
    int32_t       stopped;          /* +0x1cc */
    /* Indexed by a one-based family number, which is why the original's own
       two users of it disagree by eight about where it starts. */
    EvvRom       *roms[0x12][2];    /* +0x1d0 */
    SynthThread  *thread;           /* +0x260 */
    int32_t       family;           /* +0x264 */
    int32_t       dialect;          /* +0x268 */
    char         *pending;          /* +0x26c */
    char         *out;              /* +0x270 */
    int32_t       pending_len;      /* +0x274 */
} RomanizerManager;

/* What a caller has to allocate for one. Only this file knows what is in it. */
const uint32_t rm_bytes = sizeof(RomanizerManager);

#define RM_LOCK(m)        ((void *)(m)->lock)
#define RM_INI(m)         ((void *)&(m)->ini)
#define RM_NAMES(m, f, d) ((m)->names[f][d])
#define RM_ACTIVE(m)      ((m)->active)
#define RM_LAST_FLAG(m)   ((m)->last_flag)
#define RM_STOPPED(m)     ((m)->stopped)
#define RM_ROMS(m, f, d)  ((m)->roms[(f) - 1][d])
#define RM_MAKER(m)       ((m)->maker)
#define RM_THREAD(m)      ((m)->thread)
#define RM_FAMILY(m)      ((m)->family)
#define RM_DIALECT(m)     ((m)->dialect)
#define RM_PENDING(m)     ((m)->pending)
#define RM_OUT(m)         ((m)->out)
#define RM_PENDING_LEN(m) ((m)->pending_len)

#define RM_FAMILIES  0x12
#define RM_DIALECTS  2

extern THIS void *sy_mutexCtor(void *m, int32_t recursive)
    MANGLED("??0Mutex@@QAE@H@Z");
extern THIS void sy_mutexDtor(void *m) MANGLED("??1Mutex@@QAE@XZ");
extern THIS int sy_mutexWait(void *m, int32_t ms) MANGLED("?wait@Mutex@@QAEHJ@Z");
extern THIS int sy_mutexRelease(void *m) MANGLED("?release@Mutex@@QAEHXZ");
extern THIS void *ini_ctor(void *r)
    MANGLED("??0IniFileReader@@QAE@XZ");
extern THIS void ini_dtor(void *r)
    MANGLED("??1IniFileReader@@QAE@XZ");
extern THIS void stw_addTextToEngine(SynthThread *t, char *text, int32_t n)
    MANGLED("?addTextToEngine@SynthThread@@QAEXPADH@Z");
extern THIS void stw_processRemaining(SynthThread *t)
    MANGLED("?processRemaining@SynthThread@@QAEXXZ");
extern int32_t fileModuleDirectory(char *out, int32_t room);

/* The table that maps one byte to another when no corpus is loaded. */

/* Every byte as it is passed on. Control characters become spaces, the
   newline is kept because the walk below decides what to do with it, the
   curly quotes become apostrophes, and the rest stands. */
const uint8_t ConversionTable[256] = {
    0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x0a,0x20,0x20,0x20,0x20,0x20,
    0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,
    0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,
    0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,
    0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x4b,0x4c,0x4d,0x4e,0x4f,
    0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x5b,0x5c,0x5d,0x5e,0x5f,
    0x60,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x6b,0x6c,0x6d,0x6e,0x6f,
    0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x7b,0x7c,0x7d,0x7e,0x20,
    0x80,0x20,0x20,0x20,0x20,0x85,0x20,0x20,0x20,0x20,0x8a,0x20,0x8c,0x20,0x20,0x20,
    0x20,0x27,0x27,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0x20,0x9c,0x20,0x20,0x20,
    0xa0,0xa1,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xab,0xac,0xad,0xae,0xaf,
    0xb0,0xb1,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xbb,0xbc,0xbd,0xbe,0xbf,
    0xc0,0xc1,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xcb,0xcc,0xcd,0xce,0xcf,
    0xd0,0xd1,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xdb,0xdc,0xdd,0xde,0xdf,
    0xe0,0xe1,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xeb,0xec,0xed,0xee,0xef,
    0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa,0xfb,0xfc,0xfd,0xfe,0xff,
};

#define ST_CORPORA_AT(t) ST_CORPORA((SynthThread *)(t))

int rz_isRomExist(int32_t family, int32_t dialect);
void rz_removeUnusedByCode(RomanizerManager *m, uint8_t f, uint8_t d);

/* ---- which languages have a romanizer at all ------------------------ */

/* Five families do, and only some of their dialects. Everything else is
   spoken as it is written. */
int rz_isRomExist(int32_t family, int32_t dialect)
{
    switch (family) {
    case 6:  return dialect == 0 || dialect == 1;
    case 8:  return dialect == 0;
    case 10: return dialect == 0;
    case 11: return dialect == 0 || dialect == 1;
    case 16: return dialect == 0;
    default: return 0;
    }
}

/* ---- making and unmaking -------------------------------------------- */

static void rz_forgetAll(RomanizerManager *m)
{
    int f, d;

    for (f = 1; f <= RM_FAMILIES; f++)
        for (d = 0; d < RM_DIALECTS; d++) {
            RM_ROMS(m, f, d) = 0;
            RM_NAMES(m, f - 1, d) = 0;
        }
    RM_ACTIVE(m) = 0;
    RM_FAMILY(m) = 0;
    RM_DIALECT(m) = 0;
    RM_PENDING_LEN(m) = 0;
    RM_LAST_FLAG(m) = 0;
}

THIS void *rz_ctor(RomanizerManager *m, SynthThread *thread)
{
    sy_mutexCtor(RM_LOCK(m), 0);
    ini_ctor(RM_INI(m));
    RM_THREAD(m) = thread;
    rz_forgetAll(m);
    RM_MAKER(m) = 0;
    RM_OUT(m) = 0;
    RM_STOPPED(m) = 0;
    return m;
}

THIS void rz_dtor(RomanizerManager *m)
{
    int f, d;

    for (f = 1; f <= RM_FAMILIES; f++)
        for (d = 0; d < RM_DIALECTS; d++) {
            EvvRom *r = RM_ROMS(m, f, d);

            if (r)
                r->ops->release(r);
        }

    if (RM_OUT(m))
        cpp_delete(RM_OUT(m));

    ini_dtor(RM_INI(m));
    sy_mutexDtor(RM_LOCK(m));
}

/* ---- what runs when there is no romanizer --------------------------- */

/* A parameter on its way down. With a romanizer it is that romanizer's
   business; without one it is text like any other and goes straight to the
   engine. */
THIS int rz_addParam(RomanizerManager *m, const char *s, int32_t n)
{
    if (RM_ACTIVE(m))
        return RM_ACTIVE(m)->ops->addParam(RM_ACTIVE(m), s, n);

    stw_addTextToEngine(RM_THREAD(m), (char *)s, n);
    return 1;
}

/* An index mark. Without a romanizer it becomes the annotation that means
   the same thing. */
THIS int rz_insertIndex(RomanizerManager *m)
{
    if (RM_ACTIVE(m))
        return RM_ACTIVE(m)->ops->insertIndex(RM_ACTIVE(m));

    stw_addTextToEngine(RM_THREAD(m), "`ui", 3);
    return 1;
}

THIS void rz_clear(RomanizerManager *m)
{
    RM_PENDING(m) = 0;
    RM_PENDING_LEN(m) = 0;
}

THIS int rz_resume(RomanizerManager *m)
{
    if (RM_ACTIVE(m))
        RM_ACTIVE(m)->ops->resume(RM_ACTIVE(m));
    RM_STOPPED(m) = 0;
    return 1;
}

THIS int rz_stop(RomanizerManager *m)
{
    RM_STOPPED(m) = 1;
    if (RM_ACTIVE(m) && !RM_ACTIVE(m)->ops->stop(RM_ACTIVE(m)))
        return 0;
    return 1;
}

THIS EvvRom *rz_getRom(RomanizerManager *m, uint32_t lang)
{
    (void)lang;
    return RM_ACTIVE(m);
}

THIS void rz_romClearErrors(RomanizerManager *m)
{
    int f, d;

    for (f = 1; f <= RM_FAMILIES; f++)
        for (d = 0; d < RM_DIALECTS; d++) {
            EvvRom *r = RM_ROMS(m, f, d);

            if (r)
                r->ops->clearErrors(r);
        }
}

THIS uint32_t rz_romProgStatus(RomanizerManager *m)
{
    if (!RM_ACTIVE(m))
        return 0;
    return RM_ACTIVE(m)->ops->progStatus(RM_ACTIVE(m));
}

THIS void rz_romErrorMessage(RomanizerManager *m, char *out)
{
    if (RM_ACTIVE(m)) {
        RM_ACTIVE(m)->ops->errorMessage(RM_ACTIVE(m), out);
        return;
    }
    strcpy(out, "No Romanizer Error");
}

/* Taking a romanizer out of use does nothing at all in this build. */
void rz_removeUnusedByCode(RomanizerManager *m, uint8_t f, uint8_t d)
{
    (void)m;
    (void)f;
    (void)d;
}

THIS void rz_removeUnused(RomanizerManager *m, int32_t *lang)
{
    rz_removeUnusedByCode(m, (uint8_t)((*lang & 0xff0000) >> 16),
                          (uint8_t)(*lang & 0xff));
}

/* ---- the one byte-for-byte conversion that is not a romanizer -------- */

/* Whether a byte is one the language in force claims as its own.

   A language IBM never shipped may have letters outside the byte set this
   table was made for, and it says which in `lang/<tag>/<tag>.codepoints'.
   The eight IBM did ship claim none, so this answers no for every one of
   them and the table decides as it always did. */
static int ownByte(SynthThread *t, uint8_t c)
{
    const delta_language *l;
    int32_t i;

    if (t == 0 || ST_ENGINE_ID(t) == 0)
        return 0;
    l = delta_lang_by_id((int32_t)ST_ENGINE_ID(t));
    if (l == 0 || l->codepoints == 0)
        return 0;
    for (i = 0; i < l->codepoints_n; i++)
        if (l->codepoints[i].byte == c)
            return 1;
    return 0;
}

/* With no corpus loaded, every byte is mapped through one table. With a
   corpus the text is left alone, because the corpus has already had it.
 *
 * A letter of a language's own is left alone as well, which the original
 * does not do because it had no such language. The table turns most of
 * 0x80 to 0x9f into a space -- undefined in the Western set it was made
 * for -- and those are the byte values a new language's letters are free to
 * take, so without this a Polish word arrives as several one-letter ones.
 * Nothing IBM shipped has a letter of its own, so nothing IBM shipped
 * reaches this. */
THIS void rz_convertText(RomanizerManager *m, uint8_t *text)
{
    SynthThread *t = RM_THREAD(m);

    if (ST_CORPORA_AT(t))
        return;

    for (; *text; text++)
        if (!ownByte(t, *text))
            *text = ConversionTable[*text];
}

/* What a character that means something to the engine is replaced with.
   Four backslashes and a space is not a typo: the engine strips one layer
   and the filter below it strips another. */
static const char backSlash[] = "\\";
static const char doubleBackSlash[] = "\\\\";
static const char quadBackSlash[] = "\\\\\\\\ ";

extern THIS int stw_isOldEngine(SynthThread *t)
    MANGLED("?isOldEngine@SynthThread@@QAEHXZ");

/* ---- rewriting the text --------------------------------------------- */

/* How much longer the text will be once the characters that mean something
   have been escaped. Counted first so that one allocation holds the
   result. */
THIS int rz_countAdditionSpace(RomanizerManager *m, uint8_t *text,
                               int32_t annotated)
{
    int extra = 0;

    for (; *text; text++) {
        if (*text == '|') {
            if (RM_THREAD(m) && stw_isOldEngine(RM_THREAD(m)))
                extra += strlen(backSlash);
            continue;
        }
        if (*text == '\\') {
            extra += strlen(quadBackSlash);
            if (text[1] == '`')
                extra += strlen(doubleBackSlash) - 1;
            text++;
            continue;
        }
        if (*text == '^') {
            extra += strlen(doubleBackSlash);
            continue;
        }
        if (*text == '`') {
            /* With annotations off every backtick is escaped; with them on
               only the three the caller is not allowed to write itself. */
            if (!annotated
                || strncmp((char *)text, "`g", 2) == 0
                || strncmp((char *)text, "`i", 2) == 0
                || strncmp((char *)text, "`ui", 3) == 0)
                extra += strlen(doubleBackSlash);
        }
    }
    return extra;
}

/* Copy one character that means something across, escaped. A backslash
   takes the character after it with it. Answers nought always: there is no
   way for this to fail. */
THIS int32_t rz_processSpecial(RomanizerManager *m, char **runStart,
                               char **pp, char *out, int32_t annotated)
{
    char *q = *runStart;

    if (*q == '|') {
        if (RM_THREAD(m) && stw_isOldEngine(RM_THREAD(m)))
            strcat(out, backSlash);
    } else if (*q == '\\') {
        strcat(out, quadBackSlash);
        if (q[1] == '`')
            strcat(out, doubleBackSlash);
        q++;
    } else if (*q == '^') {
        strcat(out, doubleBackSlash);
    } else if (*q == '`') {
        if (!annotated
            || strncmp(q, "`g", 2) == 0
            || strncmp(q, "`i", 2) == 0
            || strncmp(q, "`ui", 3) == 0)
            strcat(out, doubleBackSlash);
    }

    strncat(out, q, 1);
    q++;
    *pp = q;
    *runStart = q;
    return 0;
}

/* Walk the text and build the rewritten copy.

   Ordinary characters are not copied one at a time. The walk remembers
   where the current run of them started and only flushes it with strncat
   when it reaches something that needs handling, which is most of the text
   in one call.

   A newline becomes a space, unless the character before it was already a
   space, in which case it is dropped. Everything the engine treats as
   special is handed to the routine above. */
THIS int32_t rz_processText(RomanizerManager *m, char **io, uint32_t len,
                            int32_t annotated)
{
    uint8_t *start = (uint8_t *)*io;
    uint8_t *p = start;
    uint8_t *runStart = start;
    int stopped = 0;
    int32_t rc = 0;
    int extra;
    char *out;

    rz_convertText(m, start);
    extra = rz_countAdditionSpace(m, start, annotated);

    if (RM_OUT(m)) {
        cpp_delete(RM_OUT(m));
        RM_OUT(m) = 0;
    }
    RM_OUT(m) = cpp_new(len + extra + 1);
    if (!RM_OUT(m))
        return -2;
    out = RM_OUT(m);
    out[0] = 0;

    while (!stopped && (uint32_t)(p - start) < len) {
        /* Asked to stop part way through, nothing of this is wanted. */
        if (RM_STOPPED(m)) {
            out[0] = 0;
            return 0;
        }

        if (*p == '\n') {
            if (p == start || p[-1] != ' ') {
                *p = ' ';
            } else {
                if (p - runStart > 0)
                    strncat(out, (char *)runStart, p - runStart);
                p++;
                runStart = p;
            }
            continue;
        }

        if (*p == '^' || *p == '|'
            || (!annotated && (*p == '`' || *p == '\\'))) {
            if (p - runStart > 0) {
                strncat(out, (char *)runStart, p - runStart);
                runStart = p;
            }
            if (!stopped) {
                int32_t bad = rz_processSpecial(m, (char **)&runStart,
                                                (char **)&p, out, annotated);
                if (bad) {
                    stopped = 1;
                    rc = bad;
                }
            }
            continue;
        }

        if (annotated && (*p == '\\' || *p == '`')) {
            if (p - runStart > 0) {
                strncat(out, (char *)runStart, p - runStart);
                runStart = p;
            }
            if (!stopped) {
                if ((uint32_t)(p - start) < len) {
                    int32_t bad = rz_processSpecial(m, (char **)&runStart,
                                                    (char **)&p, out,
                                                    annotated);
                    if (bad) {
                        stopped = 1;
                        rc = bad;
                    }
                } else {
                    runStart = p;
                }
            }
            continue;
        }

        p++;
    }

    if (!stopped && p - runStart > 0)
        strncat(out, (char *)runStart, p - runStart);

    *io = out;
    return rc;
}

/* ---- text arriving and leaving -------------------------------------- */

/* Hand back whatever has been held. With a romanizer that is its business;
   without one the held text is rewritten and passed on. */
THIS int32_t rz_processSentence(RomanizerManager *m, char **out,
                                int32_t annotated)
{
    int32_t n = 0;

    *out = 0;

    if (RM_ACTIVE(m)) {
        int32_t answer = RM_ACTIVE(m)->ops->processSentence(RM_ACTIVE(m), out,
                                                            annotated);

        if (answer == 2) {
            n = strlen(*out);
            rz_convertText(m, (uint8_t *)*out);
        }
        return n;
    }

    if (RM_STOPPED(m) || !RM_PENDING_LEN(m))
        return 0;

    *out = RM_PENDING(m);
    rz_processText(m, out, strlen(*out), RM_LAST_FLAG(m));
    RM_PENDING_LEN(m) = 0;
    return strlen(*out);
}

THIS int32_t rz_processRemaining(RomanizerManager *m, char **out)
{
    return rz_processSentence(m, out, 1);
}

/* Take a stretch of text. Anything still held from last time goes to the
   engine first. */
THIS int rz_addText(RomanizerManager *m, const char *text, int32_t len,
                    int32_t flag)
{
    int rc = 1;

    if (RM_STOPPED(m))
        return rc;

    if (flag != RM_LAST_FLAG(m)) {
        char *held = 0;

        rz_processRemaining(m, &held);
        if (held)
            stw_addTextToEngine(RM_THREAD(m), held, strlen(held));
    }

    RM_PENDING(m) = (char *)text;
    RM_PENDING_LEN(m) = len;

    if (len && RM_ACTIVE(m)) {
        rc = RM_ACTIVE(m)->ops->addText(RM_ACTIVE(m), RM_PENDING(m), len,
                                        flag);
        RM_PENDING_LEN(m) = 0;
    }

    RM_LAST_FLAG(m) = flag;
    return rc;
}

/* ---- choosing a language -------------------------------------------- */

/* One romanizer per language, made the first time it is asked for and kept.
 *
 * The original makes a LangIdentifier here, fills it in from the family and
 * dialect, asks it for its string and then frees it without using either.
 * That is left out: it allocates and frees twenty bytes and nothing else
 * happens.
 *
 * What the romanizer is told is the directory the program was loaded from,
 * which is where IBM's own is to find its files. Ours has its data compiled
 * in and does not need it, and it is handed over anyway because that is what
 * the call says. */
THIS EvvRom *rz_getRomanizerInst(RomanizerManager *m, uint8_t family,
                                 uint8_t dialect)
{
    EvvRom *inst = 0;
    char    dir[0x104];

    sy_mutexWait(RM_LOCK(m), -1);

    if (RM_ROMS(m, family, dialect)) {
        EvvRom *r = RM_ROMS(m, family, dialect);

        sy_mutexRelease(RM_LOCK(m));
        return r;
    }

    if (!rz_isRomExist(family, dialect)) {
        sy_mutexRelease(RM_LOCK(m));
        return 0;
    }

    RM_MAKER(m) = evv_rom_maker(family, dialect);
    if (RM_MAKER(m)) {
        if (!fileModuleDirectory(dir, (int32_t)sizeof dir))
            dir[0] = 0;
        inst = RM_MAKER(m)(dir);
    }

    RM_ROMS(m, family, dialect) = inst;
    sy_mutexRelease(RM_LOCK(m));
    return inst;
}

THIS int rz_setActiveLanguage(RomanizerManager *m, uint8_t family,
                              uint8_t dialect, EvvRom **out)
{
    if (!rz_isRomExist(family, dialect)) {
        *out = 0;
        return 0;
    }

    rz_removeUnusedByCode(m, family, dialect);
    *out = rz_getRomanizerInst(m, family, dialect);
    return *out ? 0 : -1;
}

/* Set one of the engine's settings. A change of language is the only one
   this object does anything with itself. */
THIS int rz_setParam(RomanizerManager *m, int32_t which, int32_t value)
{
    int32_t rc = 0;

    if (RM_ACTIVE(m))
        rc = RM_ACTIVE(m)->ops->getParam(RM_ACTIVE(m), which);
    if (rc != value)
        stw_processRemaining(RM_THREAD(m));

    if (which == 2) {
        uint8_t family = (uint8_t)((value & 0xff0000) >> 16);
        uint8_t dialect = (uint8_t)(value & 0xff);
        EvvRom *found = 0;

        rc = ((RM_FAMILY(m) & 0xff) << 16) | (RM_DIALECT(m) & 0xff);
        if (rz_setActiveLanguage(m, family, dialect, &found) != 0) {
            rc = -1;
            return rc;
        }
        RM_FAMILY(m) = family;
        RM_DIALECT(m) = dialect;
        RM_ACTIVE(m) = found;
        if (RM_ACTIVE(m))
            rc = RM_ACTIVE(m)->ops->setParam(RM_ACTIVE(m), which, value);
        return rc;
    }

    if (which == 0) {
        if (RM_ACTIVE(m))
            rc = RM_ACTIVE(m)->ops->setParam(RM_ACTIVE(m), which, value);
        RM_LAST_FLAG(m) = value;
        return rc;
    }

    if (which == 3 || which == 14 || which == 15
        || (which >= 1000 && which <= 1003)) {
        if (RM_ACTIVE(m))
            rc = RM_ACTIVE(m)->ops->setParam(RM_ACTIVE(m), which, value);
    }
    return rc;
}

/* ---- the two the caller may ask for directly ------------------------ */

THIS int rz_UnicodeToMBCS(RomanizerManager *m, uint32_t lang,
                          const uint16_t *in, char **out, int32_t n)
{
    EvvRom *found = 0;

    if (rz_setActiveLanguage(m, (uint8_t)((lang & 0xff0000) >> 16),
                             (uint8_t)(lang & 0xff), &found) != 0)
        return -1;
    if (!found)
        return -1;
    if ((lang & 0xff00) != 0x800)
        return 0;
    return found->ops->UCS2ToMBCS(found, in, out, n);
}

THIS int rz_MBCSToUnicode(RomanizerManager *m, uint32_t lang,
                          const char *in, uint16_t **out)
{
    EvvRom *found = 0;

    if (rz_setActiveLanguage(m, (uint8_t)((lang & 0xff0000) >> 16),
                             (uint8_t)(lang & 0xff), &found) != 0)
        return -1;
    if (!found)
        return -1;
    if (out)
        *out = 0;
    return 0;
}

ALIAS("??0RomanizerManager@@QAE@PAVSynthThread@@@Z", "rz_ctor");
ALIAS("??1RomanizerManager@@QAE@XZ", "rz_dtor");
ALIAS("?addParam@RomanizerManager@@QAEHPBDH@Z", "rz_addParam");
ALIAS("?addText@RomanizerManager@@QAEHPBDHH@Z", "rz_addText");
ALIAS("?clear@RomanizerManager@@QAEXXZ", "rz_clear");
ALIAS("?getRom@RomanizerManager@@QAEPAVRomInstance@@K@Z", "rz_getRom");
ALIAS("?insertIndex@RomanizerManager@@QAEHXZ", "rz_insertIndex");
ALIAS("?MBCSToUnicode@RomanizerManager@@QAEHKPBDPAPAG@Z", "rz_MBCSToUnicode");
ALIAS("?processRemaining@RomanizerManager@@QAEHPAPAD@Z",
      "rz_processRemaining");
ALIAS("?processSentence@RomanizerManager@@QAEHPAPADH@Z", "rz_processSentence");
ALIAS("?removeUnusedRomanizer@RomanizerManager@@QAEXPAVLangIdentifier@@@Z",
      "rz_removeUnused");
ALIAS("?removeUnusedRomanizer@RomanizerManager@@QAEXEE@Z",
      "rz_removeUnusedByCode");
ALIAS("?resume@RomanizerManager@@QAEHXZ", "rz_resume");
ALIAS("?romClearErrors@RomanizerManager@@QAEXXZ", "rz_romClearErrors");
ALIAS("?romErrorMessage@RomanizerManager@@QAEXPAD@Z", "rz_romErrorMessage");
ALIAS("?romProgStatus@RomanizerManager@@QAEKXZ", "rz_romProgStatus");
ALIAS("?setParam@RomanizerManager@@QAEHJH@Z", "rz_setParam");
ALIAS("?stop@RomanizerManager@@QAEHXZ", "rz_stop");
ALIAS("?UnicodeToMBCS@RomanizerManager@@QAEHKPBGPAPADH@Z", "rz_UnicodeToMBCS");
ALIAS("?convertText@RomanizerManager@@AAEXPAE@Z", "rz_convertText");
ALIAS("?countAdditionSpace@RomanizerManager@@AAEHPAEH@Z",
      "rz_countAdditionSpace");
ALIAS("?processSpecial@RomanizerManager@@AAEJPAPAD0PADH@Z",
      "rz_processSpecial");
ALIAS("?processText@RomanizerManager@@AAEJPAPADKH@Z", "rz_processText");
ALIAS("?getRomanizerInst@RomanizerManager@@AAEPAVRomInstance@@EE@Z",
      "rz_getRomanizerInst");
ALIAS("?setActiveLanguage@RomanizerManager@@AAEHEEPAPAVRomInstance@@@Z",
      "rz_setActiveLanguage");
ALIAS("?isRomExist@RomanizerManager@@CAHHH@Z", "rz_isRomExist");
