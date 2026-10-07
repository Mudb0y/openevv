/* What an instance starts out believing.
 *
 * Eighteen words the caller may read and write one at a time. Six of them
 * have names the interface publishes; the rest are the environment's own
 * business and are left numbered, because nothing in the library reads
 * them by any other name either.
 *
 * Taken from the original's data rather than transcribed from code: there
 * is no code, it is a table. The differential build still reads the
 * original's copy, because the object it sits in is one we link; the two
 * were compared word for word when this was written.
 */

#include <stdint.h>

/* The rate is an index into the three the engine offers rather than a
   number of samples, and one of them is eleven thousand and twenty-five,
   which is what the raw format defaults to as well. */
#define RATE_MIDDLE 1

int32_t g_DefaultEnvironment[0x12] = {
    0,            /* synthesis mode */
    0,            /* input type */
    0,            /* text mode */
    /* The abbreviation dictionary, which IBM shipped on and which is off
       here: it says what it takes an abbreviation to stand for, "mg" as
       milligrams and "Dr." as doctor or drive, and a listener then hears a
       word that is not in the text. eciSetParam(h, eciDictionary, 0) or `da1
       asks for it; nought here is off, since this table holds the setting
       the right way up and the interface turns it over. */
    0,
    0,
    RATE_MIDDLE,  /* sample rate */
    0,
    0,            /* phonemes not wanted */
    0,
    0,            /* language, meaning whichever is first */
    1,            /* number mode */
    /* Phrase prediction, which IBM shipped on and which is off here: the
       breaks it guesses at in a stretch with no punctuation are pauses a
       listener cannot predict from the text. `pp1 or
       eciSetDefaultParam(11, 1) asks for it. */
    0,
    0,            /* romanizer off */
    10,           /* the four the caller sees as the environment proper */
    2200,
    0,
    2200,
    1
};
