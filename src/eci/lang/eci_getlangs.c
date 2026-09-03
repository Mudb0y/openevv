/* Which languages this build has.
 *
 * The answer is in the built-in ini: every language is a section, and the
 * section's name is the language written as numbers with dots between them,
 * so walking the sections and reading the numbers out of their names is the
 * whole of it.
 *
 * A caller asking with room for nought languages is asking how many there
 * are; one asking with room is asking for as many as will fit, and gets told
 * how many that was. Either way the count comes back in the same place the
 * room was given in.
 */

#include <stdint.h>
#include <string.h>
#include "eci_synththread.h"
#include "evv_abi.h"
#include "eci_engine.h"

#define ECI_PARAMETER_ERROR  0x80

/* The reader is built on the stack, so its size has to be right. */

extern THIS IniFileReader *ini_ctor(IniFileReader *r);
extern THIS void           ini_dtor(IniFileReader *r);
extern THIS const char    *ini_getFirstSection(IniFileReader *r);
extern THIS const char    *ini_getNextSection(IniFileReader *r);

/* Room for a section name. The original keeps forty-three bytes on the
   stack; nothing in the built-in ini comes near that, and a larger one
   cannot change what is read. */
#define NAME_ROOM  0x100

/* Numbers separated by dots, read out into three bytes. The answer is how
   many characters were used, so nought means it was not a number at all.
   Only the first two are cleared before reading, which is the original's
   doing: the third is written into only if a second dot turns up. */
int32_t lg_splitLanguageString(char *s, uint8_t *first, uint8_t *second,
                               uint8_t *third)
{
    int32_t used = 0;

    *first  = 0;
    *second = 0;

    while (*s && *s >= '0' && *s <= '9') {
        *first = (uint8_t)(*first * 10 + (*s - '0'));
        used++;
        s++;
    }

    if (*s == '.') {
        used++;
        s++;
        while (*s && *s >= '0' && *s <= '9') {
            *second = (uint8_t)(*second * 10 + (*s - '0'));
            used++;
            s++;
        }

        if (*s == '.') {
            used++;
            s++;
            while (*s && *s >= '0' && *s <= '9') {
                *third = (uint8_t)(*third * 10 + (*s - '0'));
                used++;
                s++;
            }
        }
    }

    return used;
}

int32_t lg_eciGetAvailableLanguages2(uint32_t *languages, int32_t *count)
{
    IniFileReader reader;
    const char   *section;
    int32_t       found = 0;
    int32_t       room;

    if (!count || *count < 0 || (!languages && *count != 0))
        return ECI_PARAMETER_ERROR;

    ini_ctor(&reader);
    room = *count;

    for (section = ini_getFirstSection(&reader); section;
         section = ini_getNextSection(&reader)) {
        char    name[NAME_ROOM];
        uint8_t major = 0;
        uint8_t minor = 0;
        uint8_t third = 0;
        size_t  n = strlen(section);

        /* The name without its brackets. */
        strncpy(name, section + 1, n - 2);
        name[n - 2] = 0;

        if (!lg_splitLanguageString(name, &major, &minor, &third))
            continue;

        found++;
        if (languages && *count != 0) {
            languages[room - *count] = ((uint32_t)major << 16) | minor;
            (*count)--;
        }
    }

    /* Asked with no room, the answer is how many there are; asked with room,
       it is how many went in. */
    if (room == 0)
        *count = found;
    else
        *count = room - *count;

    ini_dtor(&reader);
    return 0;
}

ALIAS("?splitLanguageString@@YAHPADPAE11@Z", "lg_splitLanguageString");
ALIAS("?eciGetAvailableLanguages2@@YAHPAW4ECILanguageDialect@@PAH@Z",
      "lg_eciGetAvailableLanguages2");
