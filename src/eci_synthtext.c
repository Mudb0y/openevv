/* Text on its way from the caller into the engine.

   This is one function, addTextRun, and it is the longest in the object
   because it is where five separate concerns meet: telling the caller
   whether the voice went concatenative, wiring up the callbacks the first
   time anything is said after a reset, letting a star command through
   untouched, recoding the text if the SSML filter is in the way, and finally
   handing what comes out to the romanizer a sentence at a time.

   The recoding is the bulk of it. The caller's text arrives as UTF-8 and the
   engine wants single bytes, so there are two roads: with a romanizer
   instance for this engine the text goes to UTF-16 and the romanizer maps it
   down, and without one it is decoded here and the handful of code points
   that have a place in the Windows Western byte set are put there. Text that
   turns out not to be UTF-8 at all is copied across untouched rather than
   mangled.

   Two things in here are the original's and are left as they are because
   they are what it does: a local for a UTF-16 result that is never assigned
   and so never used, and a flag set in every arm of the mapping table and
   never read. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "eci_synththread.h"
#include "evv_abi.h"
#include "delta_lang.h"
#include "evv_arena.h"

#define APP_CONCATENATIVE  0xc
#define ROM_CONCATENATIVE  0x3e8

/* Which callback a registration is for, on the concatenative side. */
#define CAT_CB_WORD_START  1
#define CAT_CB_WORD_MARK   2
#define CAT_CB_PHONEME     3
#define CAT_CB_USER_INDEX  4

/* The engine slot that takes a line of its own command language. */
#define ENG_COMMAND        0x18

/* How many filters can be asked about at once, and how long a description
   is allowed to be. Both are the original's own choice of room. */
#define MAX_FILTERS  0x14
#define DESC_ROOM    0x104

/* The romanizer is told which engine to map for, with a bit set to say the
   text it is being handed is wide. */
#define WIDE_TEXT    0x800

typedef void (*IndexCallback)(int32_t, void *);
typedef void (*UserCallback)(void *);
typedef void (*SynthCallback)(int32_t, int32_t *, void *);

#define ENGCALL __attribute__((stdcall))
typedef ENGCALL int32_t (*EngCommand)(void *engine, const char *line);

#define ENG_CALL(t, off) \
    (((void **)*(void ***)ST_ENGINE(t))[(off) / 4])

extern THIS void stb_postRomanizerError(SynthThread *t, int32_t which)
    MANGLED("?postRomanizerError@SynthThread@@AAEXH@Z");
extern THIS void stb_postEngineError(SynthThread *t)
    MANGLED("?postEngineError@SynthThread@@AAEXXZ");
extern THIS int32_t stw_isOldEngine(SynthThread *t)
    MANGLED("?isOldEngine@SynthThread@@QAEHXZ");
extern THIS char *stw_filterText(SynthThread *t, const char *text, int32_t engine)
    MANGLED("?filterText@SynthThread@@QAEPADPBDJ@Z");
extern THIS void stw_addTextToEngine(SynthThread *t, char *text, int32_t len)
    MANGLED("?addTextToEngine@SynthThread@@QAEXPADH@Z");

extern void stb_staticSynthCallback(int32_t a, int32_t *b, void *param)
    MANGLED("?staticSynthCallback@SynthThread@@CAXHPAJPAX@Z");
extern void stb_staticTorrentPhonemeCallback(int32_t index, void *param)
    MANGLED("?staticTorrentPhonemeCallback@SynthThread@@CAXHPAX@Z");
extern void stb_staticUserIndexCallback(void *param)
    MANGLED("?staticUserIndexCallback@SynthThread@@CAXPAX@Z");
extern void stb_staticWordCallback(int32_t index, void *param)
    MANGLED("?staticWordCallback@SynthThread@@CAXHPAX@Z");

extern THIS int32_t cm_usingConcatenativeEngine(void *c)
    MANGLED("?usingConcatenativeEngine@ConcatenationManager@@QAEHXZ");
extern THIS void cm_processStarCommand(void *c, char *line)
    MANGLED("?processStarCommand@ConcatenationManager@@QAEXPAD@Z");
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
extern THIS void *rz_getRom(void *r, uint32_t engine)
    MANGLED("?getRom@RomanizerManager@@QAEPAVRomInstance@@K@Z");
extern THIS int32_t rz_UnicodeToMBCS(void *r, uint32_t engine,
                                      const uint16_t *wide, char **out,
                                      int32_t flag)
    MANGLED("?UnicodeToMBCS@RomanizerManager@@QAEHKPBGPAPADH@Z");
extern THIS int32_t rz_addText(void *r, const char *text, int32_t len,
                                int32_t last)
    MANGLED("?addText@RomanizerManager@@QAEHPBDHH@Z");
extern THIS int32_t rz_processSentence(void *r, char **out, int32_t flag)
    MANGLED("?processSentence@RomanizerManager@@QAEHPAPADH@Z");

extern THIS int32_t aq_postUser(void *a, int32_t what, int32_t value)
    MANGLED("?postUser@ETIappMessageQueue@@QAEHJJ@Z");

extern THIS void fm_getAvailableFilters(void *m, int32_t engine,
                                        uint32_t *ids, uint32_t *count)
    MANGLED("?getAvailableFilters@FilterManager@@QAEXJPAI0@Z");
extern THIS void fm_getFilterDescription(void *m, int32_t engine, uint32_t id,
                                         char *out)
    MANGLED("?getFilterDescription@FilterManager@@QAEXJIPAD@Z");
extern THIS int32_t fm_isActiveById(void *m, uint32_t id)
    MANGLED("?isFilterActive@FilterManager@@QAEHI@Z");

/* The two lines of engine command language this sends. The first turns on
   the mode the concatenative side needs; the second is what a plain engine
   is put back to. */
static const char CMD_CONCATENATIVE[] = "`esp2";
static const char CMD_PLAIN[] = "`espr0 `esp1";

/* The one filter that changes what the bytes mean rather than merely how
   they are said, so it is the one this has to recode for. */
static const char SSML_FILTER[] = "IBM SSML Filter";

/* The code points that have somewhere to go in the Windows Western byte set
   above 0x7f. Everything else keeps its own low byte, which is what the
   original does whether or not that means anything. */
static const struct { uint32_t cp; uint8_t byte; } WESTERN[] = {
    { 0x20ac, 0x80 }, { 0x201a, 0x82 }, { 0x0192, 0x83 }, { 0x201e, 0x84 },
    { 0x2026, 0x85 }, { 0x2020, 0x86 }, { 0x2021, 0x87 }, { 0x02c6, 0x88 },
    { 0x2030, 0x89 }, { 0x0160, 0x8a }, { 0x2039, 0x8b }, { 0x0152, 0x8c },
    { 0x017d, 0x8e }, { 0x2018, 0x91 }, { 0x2019, 0x92 }, { 0x201c, 0x93 },
    { 0x201d, 0x94 }, { 0x2022, 0x95 }, { 0x2013, 0x96 }, { 0x2014, 0x97 },
    { 0x02dc, 0x98 }, { 0x2122, 0x99 }, { 0x0161, 0x9a }, { 0x203a, 0x9b },
    { 0x0153, 0x9c }, { 0x017e, 0x9e }, { 0x0178, 0x9f },
};

/* Which language this thread is speaking, or nothing if it is not saying yet.
   Not delta_lang_now: the text path runs before a language is in force and
   that accessor refuses then, as it should. */
static const delta_language *langOf(SynthThread *t)
{
    if (t == 0 || ST_ENGINE_ID(t) == 0)
        return 0;
    return delta_lang_by_id((int32_t)ST_ENGINE_ID(t));
}

/* Say whether the voice in play is concatenative, but only when the answer
   has changed since the last time anyone was told. */
static void reportConcatenative(SynthThread *t)
{
    if (ST_CONCAT(t) && cm_usingConcatenativeEngine(ST_CONCAT(t))) {
        if (!ST_TOLD_CAT(t)) {
            if (APP_LISTENING(ST_APP(t)))
                aq_postUser(ST_APP(t), APP_CONCATENATIVE, 1);
            ST_TOLD_CAT(t) = 1;
        }
    } else if (ST_TOLD_CAT(t) == 1) {
        if (APP_LISTENING(ST_APP(t)))
            aq_postUser(ST_APP(t), APP_CONCATENATIVE, 0);
        ST_TOLD_CAT(t) = 0;
        rz_setParam(ST_ROMAN(t), ROM_CONCATENATIVE, 0);
    }
}

/* The first thing said after a reset has to set the engine up, because
   everything the last run told it has been forgotten. */
static void openTheEngine(SynthThread *t)
{
    EngCommand command = (EngCommand)ENG_CALL(t, ENG_COMMAND);

    if (ST_CONCAT(t) && cm_usingConcatenativeEngine(ST_CONCAT(t))) {
        cm_registerCallbackC(ST_CONCAT(t), stb_staticSynthCallback, t);
        cm_registerCallbackA(ST_CONCAT(t), CAT_CB_PHONEME,
                                  stb_staticTorrentPhonemeCallback, t);

        if (ST_FLAGS(t) & STF_ROMANIZING)
            cm_registerCallbackB(ST_CONCAT(t), CAT_CB_USER_INDEX,
                                     stb_staticUserIndexCallback, t);
        else if (ST_FLAGS(t) & STF_WORD_STARTS)
            cm_registerCallbackA(ST_CONCAT(t), CAT_CB_WORD_START,
                                      stb_staticWordCallback, t);

        /* Word marks are registered as nothing at all here. Whatever wants
           them puts itself in later; this only makes the slot exist. */
        if (ST_FLAGS(t) & STF_WORD_MARKS)
            cm_registerCallbackA(ST_CONCAT(t), CAT_CB_WORD_MARK, 0, t);

        /* The phoneme callback goes in twice. It is the same callback under
           the same number both times, so the second is spent effort, but it
           is what the original does. */
        cm_registerCallbackA(ST_CONCAT(t), CAT_CB_PHONEME,
                                  stb_staticTorrentPhonemeCallback, t);

        ST_DIRECT(t) = 1;
        if (command(ST_ENGINE(t), CMD_CONCATENATIVE))
            stb_postEngineError(t);
        ST_DIRECT(t) = 0;
    } else if (ST_CONCAT(t) && !cm_usingConcatenativeEngine(ST_CONCAT(t))) {
        if (command(ST_ENGINE(t), CMD_PLAIN))
            stb_postEngineError(t);
    }
    ST_FRESH(t) = 0;
}

/* Only the SSML filter asks for the text to be recoded, so it is the one
   thing worth looking for among the filters that are switched on. */

/* UTF-8 to UTF-16, for handing to the romanizer.

   The two-byte case is written the way the original writes it, which is not
   the obvious way: the leading byte's five payload bits are split across the
   two halves of the result rather than shifted into place in one go. It
   comes to the same thing. Its bound is off by one at the top, so a leading
   0xdf is taken for a three-byte sequence; that is the original's and is
   left alone. */
static void utf8ToWide(const char *text, uint32_t len, uint16_t *out)
{
    uint32_t i;
    uint16_t *w = out;

    for (i = 0; i < len; i++) {
        uint8_t c = (uint8_t)text[i];

        if (c < 0x80) {
            *w++ = c;
        } else if (c >= 0xc0 && c < 0xdf) {
            uint8_t hi = (uint8_t)((c & 0x1c) >> 2);
            uint8_t lo = (uint8_t)(((c & 0x03) << 6)
                                   | ((uint8_t)text[i + 1] & 0x3f));

            *w = (uint16_t)((((uint32_t)*w | hi) << 8) | lo);
            w++;
            i++;
        } else {
            uint8_t a = (uint8_t)(text[i] & 0x0f);
            uint8_t b = (uint8_t)((uint8_t)text[i + 1] & 0x3f);
            uint8_t c3 = (uint8_t)((uint8_t)text[i + 2] & 0x3f);

            *w++ = (uint16_t)((a << 12) | (b << 6) | c3);
            i += 2;
        }
    }
}

/* UTF-8 to single bytes, for when there is no romanizer to do it.

   Answers whether the text really was UTF-8. It it was not, the caller's own
   bytes are put in the buffer untouched instead, because text that is not
   what it claimed to be is better said wrongly than not at all. */
static int utf8ToWestern(const char *text, uint32_t len, char *out,
                         const delta_language *l)
{
    uint32_t i;
    char *o = out;

    for (i = 0; i < len; i++) {
        signed char c;
        int32_t want, cp;
        int32_t k;
        uint32_t m;

        if ((uint8_t)text[i] < 0x80) {
            *o++ = text[i];
            continue;
        }

        c = (signed char)text[i];
        want = 1;
        cp = 0;
        if (!(c & 0x80)) {
            want = 1;
            cp = c & 0x7f;
        } else if ((c & 0xe0) == 0xc0) {
            want = 2;
            cp = c & 0x1f;
        } else if ((c & 0xf0) == 0xe0) {
            want = 3;
            cp = c & 0x0f;
        } else if ((c & 0xf8) == 0xf0) {
            want = 4;
            cp = c & 0x07;
        } else {
            want = -1;
        }

        for (k = 1; k < want; k++) {
            signed char cc = (signed char)text[i + k];

            if ((cc & 0xc0) != 0x80) {
                want = -1;
                break;
            }
            cp = (cp << 6) | (cc & 0x3f);
        }

        if (want <= 0) {
            memset(out, 0, len + 1);
            for (m = 0; m < len; m++)
                out[m] = text[m];
            return 0;
        }
        i += (uint32_t)want - 1;

        /* The language in force first, since a character it names is its
           own business and its answer is the one its alphabet agrees with.
           Then the Western set, and then the low byte, which is what the
           original does and is nothing for a letter in neither. */
        {
            int done = 0;

            if (l != 0 && l->codepoints != 0) {
                for (m = 0; m < (uint32_t)l->codepoints_n; m++) {
                    if (l->codepoints[m].cp == (uint32_t)cp) {
                        cp = l->codepoints[m].byte;
                        done = 1;
                        break;
                    }
                }
            }
            if (!done) {
                for (m = 0; m < sizeof WESTERN / sizeof WESTERN[0]; m++) {
                    if (WESTERN[m].cp == (uint32_t)cp) {
                        cp = WESTERN[m].byte;
                        break;
                    }
                }
            }
        }
        *o++ = (char)cp;
    }
    return 1;
}

/* Recode the text if the SSML filter is in the way, and answer whichever
   buffer the caller should go on with. Either of the two out parameters may
   come back holding something that has to be given back afterwards. */
static void recodeForSSML(SynthThread *t, const char *text, uint32_t len,
                          char **mbcs_out, char **mapped_out)
{
    uint32_t ids[MAX_FILTERS];
    uint32_t count = MAX_FILTERS;
    uint32_t i;

    if (!ST_FILTERS(t))
        return;
    fm_getAvailableFilters(ST_FILTERS(t), 0, ids, &count);

    /* The scan runs to the end rather than stopping at the first match. Two
       filters cannot really describe themselves the same way, so this is the
       original being thorough rather than there being a second one to
       find. */
    for (i = 0; i < count; i++) {
        char desc[DESC_ROOM];
        uint32_t engine;

        fm_getFilterDescription(ST_FILTERS(t), 0, ids[i], desc);
        if (strcmp(desc, SSML_FILTER) != 0)
            continue;
        if (!fm_isActiveById(ST_FILTERS(t), ids[i]))
            continue;

        engine = ST_ENGINE_ID(t);
        if (rz_getRom(ST_ROMAN(t), engine)) {
            uint16_t *wide = (uint16_t *)cpp_new(2 * len + 2);

            if (wide) {
                memset(wide, 0, 2 * len + 2);
                utf8ToWide(text, len, wide);
                rz_UnicodeToMBCS(ST_ROMAN(t), engine | WIDE_TEXT, wide,
                                  mapped_out, 1);
                cpp_delete(wide);
            }
        } else {
            char *out = (char *)cpp_new(len + 1);

            if (!out)
                continue;
            *mbcs_out = out;
            memset(out, 0, len + 1);
            utf8ToWestern(text, len, out, langOf(t));
        }
    }
}

/* Everything a caller ever says out loud comes through here. */
THIS void addTextRun(SynthThread *t, char *text, uint32_t len, int32_t seq,
                     int32_t last)
{
    /* The original keeps a third buffer for a wide result and never puts
       anything in it, so it is always the empty one of the three. It is
       written out because the choice below is a three-way one in the
       original and reads wrongly as a two-way one. */
    char *wide_out = 0;
    char *mbcs_out = 0;
    char *mapped_out = 0;
    const char *source;
    char *filtered;
    uint32_t asked = len;
    void *lock;

    (void)seq;

    reportConcatenative(t);
    if (ST_FRESH(t))
        openTheEngine(t);

    /* A star command is the concatenative side's own language and is handed
       over without being looked at. An engine too old to have that side goes
       without, and the text is dropped. */
    if (text[0] == '`' && text[1] == '*') {
        if ((ST_CONCAT(t) && cm_usingConcatenativeEngine(ST_CONCAT(t))) || !stw_isOldEngine(t))
            cm_processStarCommand(ST_CONCAT(t), text + 1);
        goto counted;
    }

    recodeForSSML(t, text, len, &mbcs_out, &mapped_out);

    /* And the way in for a language whose letters are not in the byte set
       this engine was built around.
     *
     * IBM's takes the caller's bytes as characters on this path: the code set
     * only ever mattered under the SSML filter, which recodes above, and for
     * the languages with a romanizer, which convert their own. Every letter of
     * the nine it shipped is in the Western byte set, so nothing else needed
     * saying. A language of ours can have letters that are not, and then a
     * caller writing UTF-8 -- which is every caller now -- would hand over two
     * bytes the machine reads as two characters.
     *
     * So a language that declares characters of its own gets its text
     * converted here, and one that declares none is left exactly as it was.
     * The nine IBM shipped declare none, which is what makes this safe rather
     * than merely careful: their behaviour cannot change, and the suite says
     * so. Text that is not UTF-8 after all is left alone by the converter
     * itself. */
    {
        const delta_language *l = langOf(t);

        if (mbcs_out == 0 && mapped_out == 0
            && l != 0 && l->codepoints_n > 0) {
            char *out = (char *)cpp_new(len + 1);

            if (out != 0) {
                memset(out, 0, len + 1);
                if (utf8ToWestern(text, len, out, l)) {
                    /* Only the buffer changes hands. What is outstanding is
                       counted in characters further down, out of whatever
                       the filters answer, and the count it was given was
                       taken before any of this. */
                    mbcs_out = out;
                } else {
                    cpp_delete(out);
                }
            }
        }
    }

    if (wide_out)
        source = wide_out;
    else if (mbcs_out)
        source = mbcs_out;
    else if (mapped_out)
        source = mapped_out;
    else
        source = text;

    /* The filters proper, which may hand back something longer or shorter
       than they were given. What is outstanding has to follow it, because
       the count is in characters and the characters have just changed. */
    filtered = stw_filterText(t, source, (int32_t)ST_ENGINE_ID(t));
    len = filtered ? (uint32_t)strlen(filtered) : 0;
    if (len < asked)
        ST_PENDING(t) -= (int32_t)(asked - len);
    else if (len > asked)
        ST_PENDING(t) += (int32_t)(len - asked);

    if (rz_addText(ST_ROMAN(t), filtered, (int32_t)len, last) == -1) {
        stb_postRomanizerError(t, 0);
        return;
    }

    /* And out the other side a sentence at a time, until there is not a
       whole one left. */
    for (;;) {
        char *sentence;
        int32_t n = rz_processSentence(ST_ROMAN(t), &sentence, 0);

        if (n == -1) {
            stb_postRomanizerError(t, 0);
            return;
        }
        if (n == 0)
            break;
        stw_addTextToEngine(t, sentence, n);
    }

    if (wide_out)
        free(wide_out);
    if (mbcs_out)
        cpp_delete(mbcs_out);

counted:
    lock = ST_LOCK(t);
    sy_mutexWait(lock, -1);
    ST_PENDING(t) -= (int32_t)len;
    sy_mutexRelease(lock);
}

ALIAS("?addTextRun@SynthThread@@AAEXPADKJH@Z", "addTextRun");
