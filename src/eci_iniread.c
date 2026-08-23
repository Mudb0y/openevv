/* The engine's settings file, which is not a file.
 *
 * Everything the engine wants to know about which languages and voices it
 * was built with is written in ini form -- square-bracketed sections, then
 * key equals value lines -- and compiled into the image as one blob. So the
 * reader that opens it opens nothing: it points at the blob and reads from
 * there, which is why nothing in this file needs a filing system.
 *
 * The blob is not quite an ini file. Lines end with either a newline or a
 * nought, and the whole thing ends with a byte of 0xff, which is what every
 * walk below stops on. Reading it as signed characters, which is what the
 * original does, that byte is minus one.
 *
 * The search is a plain forward scan with one twist that makes it an ini
 * search rather than a substring search: having matched the whole of what it
 * was looking for, it insists the next character be an equals sign, an end
 * of line, or nothing at all. So looking for "rate" does not find "rateMax".
 * The one exception is looking for a bare opening bracket, which is how the
 * section walk finds the next section, and there anything may follow.
 */

#include <stdint.h>
#include <string.h>
#include "eci_synththread.h"
#include "evv_abi.h"
#include "eci_engine.h"

/* Where the blob is and how long it is. Every language module carries one,
   and src/delta_lang.c joins them into the single file this reads: the
   original's own shape, a section per language, which is what the language
   walk in eci_getlangs.c answers out of. */
#include "delta_lang.h"

extern void *cpp_new(uint32_t n) MANGLED("??2@YAPAXI@Z");
extern void  cpp_delete(void *p) MANGLED("??3@YAXPAX@Z");

/* The blob ends on this, read as a signed character. */
#define INI_END  (-1)

/* Every walk below reads the blob as signed characters, because that is how
   the original reads it and it is the only way the end marker comes out as
   minus one. A machine whose plain char is unsigned would see 255. */
#define AT(r, i)  ((signed char)(r)->text[i])

THIS int32_t     ini_readFileIntoMemory(IniFileReader *r);
THIS int32_t     ini_stringSearch(IniFileReader *r, const char *want,
                                  int32_t from, int32_t limit);
THIS int32_t     ini_goEndSectionHeader(IniFileReader *r);
THIS int32_t     ini_goEndSection(IniFileReader *r);
THIS int32_t     ini_goEndKey(IniFileReader *r);
THIS int32_t     ini_goEndData(IniFileReader *r, int32_t *at);

/* ---- opening and closing -------------------------------------------- */

THIS IniFileReader *ini_ctor(IniFileReader *r)
{
    r->text      = 0;
    r->room      = 0;
    r->unused_08 = 0;
    r->at        = 0;
    r->size      = 0;
    ini_readFileIntoMemory(r);
    return r;
}

/* Nothing was opened and nothing was allocated, so nothing is given back. */
THIS void ini_dtor(IniFileReader *r)
{
    (void)r;
}

THIS int32_t ini_readFileIntoMemory(IniFileReader *r)
{
    r->text = delta_lang_ini();
    r->size = delta_lang_ini_size();
    return 1;
}

/* For the version of this that really does read a file. Kept because the
   original keeps it, and harmless here: with no room recorded there is
   nothing to double. */
THIS int32_t ini_doubleBuffer(IniFileReader *r)
{
    int32_t bigger = r->room * 2;
    char   *fresh;

    if (!r->text)
        return 0;

    fresh = (char *)cpp_new(bigger);
    if (!fresh)
        return 0;

    memcpy(fresh, r->text, r->room);
    r->text = fresh;
    r->room = bigger;
    return 1;
}

/* ---- finding something ---------------------------------------------- */

/* Look for a name from one place up to another. On a hit the answer is where
   it starts; on a miss it is wherever the scan stopped, which the callers
   use to tell the two apart by looking at what is there. */
THIS int32_t ini_stringSearch(IniFileReader *r, const char *want,
                              int32_t from, int32_t limit)
{
    int32_t wanted = (int32_t)strlen(want);
    int32_t found  = 0;
    int32_t i      = from;
    int32_t j      = 0;

    while (!found && i < limit) {
        if (AT(r, i) != want[j]) {
            /* Not this line. Run to the end of it and start the next. */
            while (AT(r, i) != 0 && AT(r, i) != '\n' && i < limit)
                i++;
            if (i < limit)
                i++;
            continue;
        }

        /* Matching. Keep going while the characters agree. */
        do {
            i++;
            j++;
        } while (want[j] == AT(r, i) && i < limit && j < wanted);

        /* All of it, and the right sort of thing after it. */
        if (j == wanted
            && (AT(r, i) == '=' || AT(r, i) == '\n' || AT(r, i) == 0
                || strcmp(want, "[") == 0)) {
            found = 1;
            break;
        }

        j = 0;
    }

    return found ? i - j : i;
}

/* ---- walking past things -------------------------------------------- */

/* Past the closing bracket of a section header, which is only a header if
   the line ends right after it. */
THIS int32_t ini_goEndSectionHeader(IniFileReader *r)
{
    for (;;) {
        signed char c = AT(r, r->at);
        signed char next;

        if (c == INI_END)
            return 0;

        if (c == ']') {
            next = AT(r, r->at + 1);
            if (next == '\n' || next == 0) {
                r->at++;
                return 1;
            }
        }
        r->at++;
    }
}

/* Past the end of a whole section: an end of line with either a blank line,
   the end of the blob, or the next section's bracket behind it. */
THIS int32_t ini_goEndSection(IniFileReader *r)
{
    for (;;) {
        signed char c = AT(r, r->at);
        signed char next;

        if (c == INI_END)
            return 0;

        if (c == '\n' || c == 0) {
            next = AT(r, r->at + 1);
            if (next == '\n' || next == INI_END || next == '[') {
                r->at++;
                return 1;
            }
        }
        r->at++;
    }
}

/* Past the equals sign, which is where the value starts. */
THIS int32_t ini_goEndKey(IniFileReader *r)
{
    for (;;) {
        signed char c = AT(r, r->at);

        if (c == INI_END)
            return 0;
        if (c == '=') {
            r->at++;
            return 1;
        }
        r->at++;
    }
}

/* To the end of a value. This one walks a place of the caller's rather than
   the reader's own, so the reader stays pointing at the value. */
THIS int32_t ini_goEndData(IniFileReader *r, int32_t *at)
{
    for (;;) {
        signed char c = AT(r, *at);

        if (c == INI_END)
            return 0;
        if (c == '\n' || c == 0)
            return 1;
        (*at)++;
    }
}

/* ---- what the callers ask for --------------------------------------- */

/* The section header is looked for whole, brackets and all, so a section
   named "a" is not found inside one named "ab". */
THIS const char *ini_getFirstSection(IniFileReader *r)
{
    int32_t start;

    if (!r->text)
        return 0;

    r->at = ini_stringSearch(r, "[", 0, r->size);
    if (AT(r, r->at) == INI_END)
        return 0;

    start = r->at;
    if (!ini_goEndSectionHeader(r))
        return 0;

    return r->text + start;
}

THIS const char *ini_getNextSection(IniFileReader *r)
{
    int32_t start;

    if (!r->text)
        return 0;

    if (!ini_goEndSection(r) || AT(r, r->at) == INI_END)
        return 0;

    r->at = ini_stringSearch(r, "[", r->at, r->size);
    if (AT(r, r->at) == INI_END)
        return 0;

    start = r->at;
    if (!ini_goEndSectionHeader(r))
        return 0;

    return r->text + start;
}

/* The value of a key in a section, on the heap, for the caller to give back.
   Nought means the section, the key, or the value was not there. */
THIS char *ini_getString(IniFileReader *r, const char *section,
                         const char *key)
{
    char   *answer = 0;
    char   *header;
    int32_t body;
    int32_t end;
    int32_t stop;
    int32_t n;

    if (!r->text)
        return 0;

    header = (char *)cpp_new((uint32_t)strlen(section) + 3);
    if (!header)
        return 0;

    header[0] = '[';
    strcpy(header + 1, section);
    strcat(header, "]");

    r->at = ini_stringSearch(r, header, 0, r->size);
    cpp_delete(header);
    header = 0;

    if (AT(r, r->at) == INI_END)
        return 0;

    /* Where the section's contents start, and then where they end. */
    body = r->at + (int32_t)strlen(section) + 2;
    if (!ini_goEndSection(r))
        return 0;
    end = r->at;

    /* Where the search stopped is what says whether the key was there: it
       answers its own limit when it fails, and steps a little past it. The
       byte at that place only tells on a failure that landed on the end of a
       line or of the blob, so a key absent from a section with another
       section behind it came back with the next section's first value. */
    r->at = ini_stringSearch(r, key, body, end);
    if (r->at >= end || AT(r, r->at) == 0 || AT(r, r->at) == '\n'
        || AT(r, r->at) == INI_END)
        return 0;

    if (!ini_goEndKey(r))
        return 0;

    /* Only to be sure the value is terminated; where it ends is not wanted,
       because the value is copied out with the blob's own terminator. */
    stop = r->at;
    if (!ini_goEndData(r, &stop))
        return 0;

    n = (int32_t)strlen(r->text + r->at);
    if (n == 0)
        return answer;

    answer = (char *)cpp_new((uint32_t)n + 1);
    if (!answer)
        return answer;

    strcpy(answer, r->text + r->at);
    return answer;
}

ALIAS("??0IniFileReader@@QAE@XZ", "ini_ctor");
ALIAS("??1IniFileReader@@QAE@XZ", "ini_dtor");
ALIAS("?readFileIntoMemory@IniFileReader@@AAEHXZ", "ini_readFileIntoMemory");
ALIAS("?doubleBuffer@IniFileReader@@AAEHXZ", "ini_doubleBuffer");
ALIAS("?stringSearch@IniFileReader@@AAEJPBDJJ@Z", "ini_stringSearch");
ALIAS("?goEndSectionHeader@IniFileReader@@AAEHXZ", "ini_goEndSectionHeader");
ALIAS("?goEndSection@IniFileReader@@AAEHXZ", "ini_goEndSection");
ALIAS("?goEndKey@IniFileReader@@AAEHXZ", "ini_goEndKey");
ALIAS("?goEndData@IniFileReader@@AAEHPAJ@Z", "ini_goEndData");
ALIAS("?getFirstSection@IniFileReader@@QAEPBDXZ", "ini_getFirstSection");
ALIAS("?getNextSection@IniFileReader@@QAEPBDXZ", "ini_getNextSection");
ALIAS("?getString@IniFileReader@@QAEPBDPBD0@Z", "ini_getString");
