/* Which voices the engine has, read out of its own settings.
 *
 * `<voice name="...">' asks for a voice by name, and nothing else in the
 * engine can answer that: the eight voice presets a language carries have
 * numbers and no names. What has names is the concatenative engine's voice
 * datasets, which the settings blob lists as `Voice1Dataset1.0.11025' and
 * the like, and a name beside each under the same key with `.mrcp' on the
 * end.
 *
 * So this builds a table out of the settings -- every language the engine
 * has, every voice number from one to seven, every sample rate it might
 * have a dataset at -- and answers questions about it. Asking is a
 * narrowing: a mask says which of the six fields to match on, and the
 * answer is filled in only when exactly one voice survives.
 *
 * On this extraction the table comes out with no names in it. The
 * datasets are the Torrent engine's, which is not here and whose data the
 * SDK never shipped, and no module's settings carry a `.mrcp' key -- so
 * every record's name is the empty string and `<voice name>' matches
 * nothing, whatever the document asks for. That is what IBM's own binary
 * does with the same settings, which was checked rather than assumed.
 *
 * One thing is an interface met rather than code transcribed, on the same
 * footing as the managers in src/eci/synth/eci_managers.c. IBM finishes building the
 * table by calling `GetScansoftVoices', which reads a `[Scansoft]' section
 * to find voices belonging to another vendor's engine installed beside
 * this one. No module in this tree has that section, nothing in this tree
 * is that engine, and `docs/remaining.md' declined the whole object on
 * those grounds before the SSML reader reached it. It answers with no
 * voices.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "evv_abi.h"
#include "eci_engine.h"
#include "eci_ssml.h"
#include "eci_vcinfo.h"

extern void *cpp_new(uint32_t n) MANGLED("??2@YAPAXI@Z");
extern void  cpp_delete(void *p) MANGLED("??3@YAXPAX@Z");

extern THIS IniFileReader *ini_ctor(IniFileReader *r);
extern THIS void  ini_dtor(IniFileReader *r);
extern THIS char *ini_getString(IniFileReader *r, const char *section,
                                const char *key);
extern int32_t lg_eciGetAvailableLanguages2(uint32_t *languages,
                                            int32_t *count);
extern int ralStrIcmp(int n, const char *a, const char *b);

/* What the language walk answers when it cannot. */
#define ECI_PARAMETER_ERROR 0x80

/* The three rates a dataset might be recorded at. */
static const int32_t VOICE_RATES[] = { 8000, 11025, 22050 };
#define VOICE_RATE_COUNT ((int32_t)(sizeof VOICE_RATES / sizeof VOICE_RATES[0]))

/* The voice numbers a language declares, one to seven. The loop tests for
   an eighth in two places and can never reach it, which is the original's
   and is left alone. */
#define VOICE_FIRST 1
#define VOICE_LAST  8

/* Chinese is the one language whose first dataset is listed under a
   different key, and 7938 is the corpus number that key carries. */
#define LANGUAGE_CHINESE 6

/* What a question answers. */
#define VOICE_NO_CRITERIA (-12)
#define VOICE_NOT_ONE     (-17)

/* Which fields a mask may ask about. */
#define BY_NAME      0x01
#define BY_TAG       0x02
#define BY_LANGUAGE  0x04
#define BY_NUMBER    0x08
#define BY_DATASET   0x10
#define BY_RATE      0x20

/* ---- making one ------------------------------------------------------ */

THIS void cvi_ctor(CVoicesInfo *self)
{
    self->count = 0;
    self->nlangs = 0;
}

THIS void cvi_dtor(CVoicesInfo *self)
{
    int32_t i;

    for (i = 0; i < self->count; i++) {
        if (self->voices[i]->owned1 != 0)
            free(self->voices[i]->owned1);
        if (self->voices[i]->owned2 != 0)
            free(self->voices[i]->owned2);
        free(self->voices[i]);
    }
}

THIS void cvi_delete(CVoicesInfo *self)
{
    if (self == 0)
        return;
    cvi_dtor(self);
    cpp_delete(self);
}

/* ---- reading the settings -------------------------------------------- */

/* Which age band a voice number belongs to, and which variant of that band
   it is. The eight presets are one adult male, one adult female, a child,
   two elderly and three more adults, and these two tables are how IBM
   sorts them; they are numbers in its code with no names beside them. */
static int32_t cvi_ageBand(int32_t voice)
{
    switch (voice) {
    case 1: case 2: case 4: case 5: case 6:
        return 1;
    case 3:
        return 0;
    case 7: case 8:
        return 2;
    default:
        return 0;
    }
}

static int32_t cvi_variant(int32_t voice)
{
    switch (voice) {
    case 1: case 2: case 3: case 7: case 8:
        return 0;
    case 4: case 6:
        return 1;
    case 5:
        return 2;
    default:
        return 0;
    }
}

/* The `[Scansoft]' section, which no module has. See the head of this
   file. */
THIS void cvi_getScansoftVoices(CVoicesInfo *self)
{
    (void)self;
}

THIS int32_t cvi_isSsftVoicesAvailables(CVoicesInfo *self)
{
    (void)self;
    return 0;
}

THIS void cvi_initVoicesInfo(CVoicesInfo *self)
{
    IniFileReader ini;
    int32_t which;

    ini_ctor(&ini);

    self->nlangs = CVI_LANGS;
    if (lg_eciGetAvailableLanguages2(self->langs, &self->nlangs)
        == ECI_PARAMETER_ERROR)
        self->nlangs = 0;

    for (which = 0; which < self->nlangs; which++) {
        int32_t language = (int32_t)((self->langs[which] & 0xff0000) >> 16);
        int32_t dialect  = (int32_t)(self->langs[which] & 0xff);
        char    section[32];
        int32_t voice;

        sprintf(section, "%d.%d", language, dialect);

        for (voice = VOICE_FIRST; voice < VOICE_LAST; voice++) {
            int32_t rate;

            for (rate = 0; rate < VOICE_RATE_COUNT; rate++) {
                char       key[0x400];
                char      *value;
                char       copy[0x400];
                VOICE_INFO *info;
                int32_t     dataset = 0;

                sprintf(key, "Voice%dDataset%s.%d", voice, section,
                        VOICE_RATES[rate]);
                value = ini_getString(&ini, section, key);

                /* Chinese lists its first one under a corpus number
                   instead. */
                if (value == 0 && language == LANGUAGE_CHINESE && rate == 0) {
                    sprintf(key, "Voice%dCorpus%s.7938", voice, section);
                    value = ini_getString(&ini, section, key);
                }

                if (value == 0)
                    continue;

                strcpy(copy, value);
                sscanf(copy, "%d", &dataset);

                info = malloc(sizeof *info);
                self->voices[self->count] = info;
                if (info == 0) {
                    cpp_delete(value);
                    ini_dtor(&ini);
                    return;
                }

                info->language = (int32_t)self->langs[which];
                info->voice    = voice;
                info->rate     = VOICE_RATES[rate];
                info->dataset  = dataset;
                info->ageBand  = cvi_ageBand(voice);
                info->variant  = cvi_variant(voice);
                info->owned1   = 0;
                info->owned2   = 0;
                info->unused   = 0;
                info->tag      = 0;

                cpp_delete(value);

                /* And the name beside it, if the settings carry one. */
                strcat(key, ".mrcp");
                value = ini_getString(&ini, section, key);
                if (value != 0) {
                    strcpy(copy, value);
                    if (sscanf(copy, "%s", info->name) != 1)
                        strcpy(info->name, "");
                    cpp_delete(value);
                } else {
                    strcpy(info->name, "");
                }

                self->count++;
            }
        }
    }

    cvi_getScansoftVoices(self);
    ini_dtor(&ini);
}

/* ---- asking about one ------------------------------------------------ */

/* The mask says which fields to match on, and each one narrows the list of
   candidates. An answer is only given when exactly one survives, so asking
   for a voice by a name two of them share is refused rather than guessed
   at. Each filter is skipped once nothing is left. */
THIS int32_t cvi_getVoiceInfo(CVoicesInfo *self, VOICE_INFO *info,
                              int32_t mask)
{
    int32_t candidates[CVI_VOICES];
    int32_t left;
    int32_t kept;
    int32_t i;

    if (mask == 0)
        return VOICE_NO_CRITERIA;

    if (self->count == 0) {
        cvi_initVoicesInfo(self);
        if (self->count == 0)
            return VOICE_NOT_ONE;
    }

    left = self->count;
    kept = self->count;
    for (i = 0; i < self->count; i++)
        candidates[i] = i;

    if (left != 0 && (mask & BY_NAME)) {
        if (strlen(info->name) == 0)
            return VOICE_NO_CRITERIA;
        kept = 0;
        for (i = 0; i < left; i++)
            if (ralStrIcmp(0, info->name,
                           self->voices[candidates[i]]->name) == 0)
                candidates[kept++] = candidates[i];
        left = kept;
    }

    if (left != 0 && (mask & BY_TAG)) {
        kept = 0;
        for (i = 0; i < left; i++)
            if (self->voices[candidates[i]]->tag == info->tag)
                candidates[kept++] = candidates[i];
        left = kept;
    }

    if (left != 0 && (mask & BY_LANGUAGE)) {
        kept = 0;
        for (i = 0; i < left; i++)
            if (self->voices[candidates[i]]->language == info->language)
                candidates[kept++] = candidates[i];
        left = kept;
    }

    if (left != 0 && (mask & BY_NUMBER)) {
        kept = 0;
        for (i = 0; i < left; i++)
            if (self->voices[candidates[i]]->voice == info->voice)
                candidates[kept++] = candidates[i];
        left = kept;
    }

    if (left != 0 && (mask & BY_DATASET)) {
        kept = 0;
        for (i = 0; i < left; i++)
            if (self->voices[candidates[i]]->dataset == info->dataset)
                candidates[kept++] = candidates[i];
        left = kept;
    }

    if (left != 0 && (mask & BY_RATE)) {
        kept = 0;
        for (i = 0; i < left; i++)
            if (self->voices[candidates[i]]->rate == info->rate)
                candidates[kept++] = candidates[i];
        left = kept;
    }

    if (kept != 1)
        return VOICE_NOT_ONE;

    {
        VOICE_INFO *found = self->voices[candidates[0]];

        strcpy(info->name, found->name);
        info->voice    = found->voice;
        info->dataset  = found->dataset;
        info->rate     = found->rate;
        info->language = found->language;
        info->ageBand  = found->ageBand;
        info->variant  = found->variant;
        info->owned1   = found->owned1;
        info->owned2   = found->owned2;
    }

    return 0;
}

ALIAS("??0CVoicesInfo@@QAE@XZ", "cvi_ctor");
ALIAS("??1CVoicesInfo@@UAE@XZ", "cvi_dtor");
ALIAS("?InitVoicesInfo@CVoicesInfo@@QAEXXZ", "cvi_initVoicesInfo");
ALIAS("?GetVoiceInfo@CVoicesInfo@@QAEHPAUVOICE_INFO@@H@Z",
      "cvi_getVoiceInfo");
ALIAS("?GetScansoftVoices@CVoicesInfo@@QAEXXZ", "cvi_getScansoftVoices");
ALIAS("?IsSsftVoicesAvailables@CVoicesInfo@@QAEHXZ",
      "cvi_isSsftVoicesAvailables");
