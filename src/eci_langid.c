/* A Windows locale turned into an IBM language number.
 *
 * This class is not the LanguageId the SSML reader uses; it is the smaller
 * one beside it, and the only thing in IBM's whole object set that calls it
 * is the engine registry, which reads the machine's locale and wants to
 * know which voice that means. It is transcribed with the rest of the block
 * because it is two hundred bytes and completes it, not because anything
 * here reaches it.
 *
 * Two bytes hold the language and the dialect, and three little buffers
 * after them hold whichever of the three ways of saying that was last asked
 * for -- the answer points into the object, so a second question overwrites
 * the first answer.
 *
 * The locale table has a wrinkle of the original's that is kept. Twelve of
 * the fourteen entries are full Windows locale identifiers, with the
 * country in the top half; Finnish and Korean are bare primary language
 * identifiers, 0x0b and 0x12, so the locales that actually name those two
 * countries -- 0x40b and 0x412 -- fall through to nothing.
 */

#include <stdint.h>
#include <stdio.h>
#include "evv_abi.h"

/* The three ways of saying it, each with its own room inside the object. */
typedef struct {
    int8_t language;      /* +0x00 */
    int8_t dialect;       /* +0x01 */
    char   langText[5];   /* +0x02 */
    char   dialectText[5];/* +0x07 */
    char   bothText[8];   /* +0x0c */
} LanguageID;

THIS void lid_initLocale(LanguageID *l, uint16_t locale)
{
    l->language = 0;
    l->dialect  = 0;

    switch (locale) {
    case 0x409: l->language = 1; l->dialect = 0; break;
    case 0x809: l->language = 1; l->dialect = 1; break;
    case 0x40a: l->language = 2; l->dialect = 0; break;
    case 0x80a: l->language = 2; l->dialect = 1; break;
    case 0x40c: l->language = 3; l->dialect = 0; break;
    case 0xc0c: l->language = 3; l->dialect = 1; break;
    case 0x407: l->language = 4; l->dialect = 0; break;
    case 0x410: l->language = 5; l->dialect = 0; break;
    case 0x804: l->language = 6; l->dialect = 0; break;
    case 0x404: l->language = 6; l->dialect = 1; break;
    case 0x416: l->language = 7; l->dialect = 0; break;
    case 0x411: l->language = 8; l->dialect = 0; break;
    case 0x00b: l->language = 9; l->dialect = 0; break;
    case 0x012: l->language = 10; l->dialect = 0; break;
    default: break;
    }
}

THIS void lid_initBytes(LanguageID *l, char language, char dialect)
{
    l->language = language;
    l->dialect  = dialect;
}

/* The packed word this takes is the SSML side's own: language in the third
   byte, dialect in the first. The code set in between is dropped. */
THIS void lid_initPacked(LanguageID *l, int32_t packed)
{
    l->language = (int8_t)((packed & 0xff0000) >> 16);
    l->dialect  = (int8_t)(packed & 0xff);
}

THIS int32_t lid_getPackedInt(LanguageID *l)
{
    return ((int32_t)l->language << 16) | (int32_t)l->dialect;
}

THIS const char *lid_getLanguageString(LanguageID *l)
{
    sprintf(l->langText, "%d", l->language);
    return l->langText;
}

THIS const char *lid_getDialectString(LanguageID *l)
{
    sprintf(l->dialectText, "%d", l->dialect);
    return l->dialectText;
}

THIS const char *lid_getLanguageDialectString(LanguageID *l)
{
    sprintf(l->bothText, "%d.%d", l->language, l->dialect);
    return l->bothText;
}

ALIAS("??0LanguageID@@QAE@G@Z", "lid_initLocale");
ALIAS("??0LanguageID@@QAE@CC@Z", "lid_initBytes");
ALIAS("??0LanguageID@@QAE@J@Z", "lid_initPacked");
ALIAS("?getPackedInt@LanguageID@@QAEJXZ", "lid_getPackedInt");
ALIAS("?getLanguageString@LanguageID@@QAEPBDXZ", "lid_getLanguageString");
ALIAS("?getDialectString@LanguageID@@QAEPBDXZ", "lid_getDialectString");
ALIAS("?getLanguageDialectString@LanguageID@@QAEPBDXZ",
      "lid_getLanguageDialectString");
