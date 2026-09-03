/* The eight voices every language offers, read out of the settings.
 *
 * One block per language and dialect, each holding sixteen voice records of
 * which the first eight are filled in. A record is a name and eight
 * settings; three of those settings are also kept in a person's units and
 * are worked out here rather than stored.
 *
 * The settings file names each voice as a line of eight numbers under a key
 * of its own, so a section reads as one language and a run of Voice1 to
 * Voice8. The name is not in the file: the eight are always the same eight,
 * in the same order, and the original writes them in from a list.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "evv_abi.h"
#include "eci_engine.h"

/* The concatenative table is the same again with a rate between the
   dialect and the voice, and it belongs to an instance rather than to the
   library. */
#define CV_FAMILY_BYTES  0x1e18
#define CV_DIALECT_BYTES 0x0f0c
#define CV_RATE_BYTES    0x0504
#define CV_TABLE_BYTES   (FAMILIES * CV_FAMILY_BYTES)
#define RATES            3

/* One language and dialect, and one voice inside it. */
#define SV_FAMILY_BYTES  0x0a08
#define SV_DIALECT_BYTES 0x0504
#define SV_FIRST         4
#define VOICE_BYTES      0x50
#define VOICE_PARAMS     0x20
#define VOICE_PRESENT    0x44

/* How many of each there is room for. */
#define FAMILIES  18
#define VOICE_TABLE_BYTES (FAMILIES * SV_FAMILY_BYTES)
#define VOICES     8
#define PARAMS     8

/* How long a line of settings and a section name may run to. */
#define LINE_ROOM    0x100
#define SECTION_ROOM 0x20

/* Asking for all three of the settings kept in both units at once. */
#define ALL_PARAMS (-1)

/* Embedded here by value, so its size has to be right; it is the same
   block eci_iniread.c works in. */

extern THIS IniFileReader *ini_ctor(IniFileReader *r)
    MANGLED("??0IniFileReader@@QAE@XZ");
extern THIS void ini_dtor(IniFileReader *r) MANGLED("??1IniFileReader@@QAE@XZ");
extern THIS const char *ini_getFirstSection(IniFileReader *r)
    MANGLED("?getFirstSection@IniFileReader@@QAEPBDXZ");
extern THIS const char *ini_getNextSection(IniFileReader *r)
    MANGLED("?getNextSection@IniFileReader@@QAEPBDXZ");
extern THIS char *ini_getString(IniFileReader *r, const char *section,
                                const char *key)
    MANGLED("?getString@IniFileReader@@QAEPBDPBD0@Z");

extern int32_t lg_splitLanguageString(char *s, uint8_t *first, uint8_t *second,
                                      uint8_t *third);
extern void setRealWorldParamsFromECIParams(char *voice, int32_t which);
extern void cpp_delete(void *p) MANGLED("??3@YAXPAX@Z");

static void loadStandardVoice(char *table, char *section,
                              IniFileReader *ini);

/* Sixteen records a dialect, eighteen families, two dialects each. The
   block itself is in eci_voicebuf.c, which the differential build leaves
   out: there it is still the original's, so that the original's own
   readers and this loader are working on the same one. */
extern char standardVoices[];

/* Always these eight, always in this order. */
static const char *const voice_names[VOICES] = {
    "Adult Male 1",
    "Adult Female 1",
    "Child 1",
    "Adult Male 2",
    "Adult Male 3",
    "Adult Female 2",
    "Elderly Female 1",
    "Elderly Male 1"
};

/* The block one language and dialect keeps its voices in. */
static char *voice_block(char *table, uint8_t family, uint8_t dialect)
{
    return table
         + (family - 1) * SV_FAMILY_BYTES
         + dialect * SV_DIALECT_BYTES;
}

/* One section of the settings: a language, then its voices. A section that
   does not read as a language is left alone, and so is a voice the file
   says nothing about. */
static void loadStandardVoice(char *table, char *section,
                              IniFileReader *ini)
{
    char    line[LINE_ROOM];
    uint8_t family, dialect, third;
    int32_t major, minor;
    int32_t voice;

    line[0] = 0;
    memset(line + 1, 0, sizeof line - 1);

    if (sscanf(section, "%d . %d", &major, &minor) != 2)
        return;

    lg_splitLanguageString(section, &family, &dialect, &third);

    *(int32_t *)voice_block(table, family, dialect) = 1;

    for (voice = 1; voice <= VOICES; voice++) {
        char    key[SECTION_ROOM];
        char   *entry, *record, *value, *tok;
        int32_t i;

        sprintf(key, "Voice%u", (unsigned)voice);
        value = ini_getString(ini, section, key);
        if (value == 0)
            continue;

        strcpy(line, value);
        cpp_delete(value);

        entry  = voice_block(table, family, dialect)
               + (voice - 1) * VOICE_BYTES;
        record = entry + SV_FIRST;

        tok = strtok(line, " ");
        sscanf(tok, "%d", (int32_t *)(record + VOICE_PARAMS));
        for (i = 1; i < PARAMS; i++) {
            tok = strtok(NULL, " ");
            sscanf(tok, "%d", (int32_t *)(record + VOICE_PARAMS + i * 4));
        }

        setRealWorldParamsFromECIParams(record, ALL_PARAMS);

        if (voice >= 1 && voice <= VOICES)
            strcpy(record, voice_names[voice - 1]);
        else
            strcpy(record, "");

        *(int32_t *)(entry + VOICE_PRESENT) = 1;
    }
}

/* The same eight voices again, for each of the three rates the
   concatenative synthesiser offers, under a key that names all four of
   the things that pick a dataset out.

   Every one of them is marked absent whether or not it was found, which
   is what the original does; the table is read only where something else
   has since said a voice is there. */
static void loadStandardConcatenativeVoice(char *table, char *section,
                                           IniFileReader *ini)
{
    static const int32_t rates[RATES] = { 8000, 11025, 22050 };

    char    line[LINE_ROOM];
    uint8_t family, dialect, third;
    int32_t major, minor;
    int32_t voice, rate;

    line[0] = 0;
    memset(line + 1, 0, sizeof line - 1);

    if (sscanf(section, "%d . %d", &major, &minor) != 2)
        return;

    lg_splitLanguageString(section, &family, &dialect, &third);

    for (voice = 1; voice <= VOICES; voice++)
        for (rate = 0; rate < RATES; rate++) {
            char    key[SECTION_ROOM];
            char   *entry, *record, *value, *tok;
            int32_t i;

            sprintf(key, "Voice%uDataset%u.%u.%u", (unsigned)voice,
                    (unsigned)family, (unsigned)dialect,
                    (unsigned)rates[rate]);

            entry = table
                  + (family - 1) * CV_FAMILY_BYTES
                  + dialect * CV_DIALECT_BYTES
                  + rate * CV_RATE_BYTES
                  + (voice - 1) * VOICE_BYTES;
            record = entry + SV_FIRST;

            value = ini_getString(ini, section, key);
            if (value == 0) {
                *(int32_t *)(entry + VOICE_PRESENT) = 0;
                continue;
            }

            strcpy(line, value);
            cpp_delete(value);

            tok = strtok(line, " ");
            sscanf(tok, "%d", (int32_t *)(record + VOICE_PARAMS));
            for (i = 1; i < PARAMS; i++) {
                tok = strtok(NULL, " ");
                sscanf(tok, "%d", (int32_t *)(record + VOICE_PARAMS + i * 4));
            }

            setRealWorldParamsFromECIParams(record, ALL_PARAMS);

            if (voice >= 1 && voice <= VOICES)
                strcpy(record, voice_names[voice - 1]);
            else
                strcpy(record, "");

            *(int32_t *)(entry + VOICE_PRESENT) = 0;
        }
}

/* Walking the sections is the same walk twice over, so it is written
   once. Whichever loader is handed in is called with the name out of its
   brackets. */
static void walk_sections(char *table, IniFileReader *ini,
                          void (*load)(char *, char *, IniFileReader *))
{
    char        section[SECTION_ROOM];
    const char *name;

    name = ini_getFirstSection(ini);
    if (name == 0)
        return;

    strncpy(section, name + 1, strlen(name) - 2);
    section[strlen(name) - 2] = 0;
    load(table, section, ini);

    for (;;) {
        name = ini_getNextSection(ini);
        if (name == 0)
            return;
        if (strcmp(name, "[LanguageIndependent]") == 0)
            continue;

        strncpy(section, name + 1, strlen(name) - 2);
        section[strlen(name) - 2] = 0;
        load(table, section, ini);
    }
}

THIS void *scv_ctor(char *table)
{
    IniFileReader ini;

    ini_ctor(&ini);
    memset(table, 0, CV_TABLE_BYTES);
    walk_sections(table, &ini, loadStandardConcatenativeVoice);
    ini_dtor(&ini);
    return table;
}

/* Every section of the settings but the one that belongs to no language.
   The names arrive in their brackets and are copied out of them. */
THIS void *isv_ctor(void *self)
{
    IniFileReader ini;

    ini_ctor(&ini);
    memset(standardVoices, 0, VOICE_TABLE_BYTES);
    walk_sections(standardVoices, &ini, loadStandardVoice);
    ini_dtor(&ini);
    return self;
}

ALIAS("??0InitializeStandardVoices@@QAE@XZ", "isv_ctor");
ALIAS("??0StandardConcatenativeVoices@@QAE@XZ", "scv_ctor");
