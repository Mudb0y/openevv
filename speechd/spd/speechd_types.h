#ifndef OPENEVV_SPEECHD_TYPES_H
#define OPENEVV_SPEECHD_TYPES_H

/* The types of Speech Dispatcher's that the module protocol beside this file
 * and speechd/openevv.c use, under the name spd_module_main.h includes them
 * by, so that the module builds with no Speech Dispatcher package installed.
 * Ours, and declared here rather than taken from Speech Dispatcher's own
 * headers, which are LGPL. The numbers are Speech Dispatcher 0.12's: the
 * protocol carries names rather than these values, so nothing outside the
 * module ever sees one, but the module counts on the voice types being one
 * to eight in this order. */

typedef enum {
    SPD_PUNCT_ALL = 0,
    SPD_PUNCT_NONE = 1,
    SPD_PUNCT_SOME = 2,
    SPD_PUNCT_MOST = 3
} SPDPunctuation;

typedef enum {
    SPD_CAP_NONE = 0,
    SPD_CAP_SPELL = 1,
    SPD_CAP_ICON = 2
} SPDCapitalLetters;

typedef enum {
    SPD_SPELL_OFF = 0,
    SPD_SPELL_ON = 1
} SPDSpelling;

typedef enum {
    SPD_MALE1 = 1,
    SPD_MALE2 = 2,
    SPD_MALE3 = 3,
    SPD_FEMALE1 = 4,
    SPD_FEMALE2 = 5,
    SPD_FEMALE3 = 6,
    SPD_CHILD_MALE = 7,
    SPD_CHILD_FEMALE = 8,
    SPD_UNSPECIFIED = -1
} SPDVoiceType;

/* One voice as LIST VOICES reports it: a name, a locale and a variant. */
typedef struct {
    char *name;
    char *language;
    char *variant;
} SPDVoice;

typedef enum {
    SPD_MSGTYPE_TEXT = 0,
    SPD_MSGTYPE_SOUND_ICON = 1,
    SPD_MSGTYPE_CHAR = 2,
    SPD_MSGTYPE_KEY = 3,
    SPD_MSGTYPE_SPELL = 99
} SPDMessageType;

#endif
