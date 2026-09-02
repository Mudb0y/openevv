/* A language, said four ways at once.
 *
 * One word holds the language in its top half, the code set in the third
 * byte and the dialect in the bottom one, which is exactly the number the
 * published interface calls an ECILanguageDialect. Beside it the same thing
 * as text three times over: the two numbers with a dot between them, and
 * each half as the name a document writes -- "En" and "US", "Fr" and "CA".
 *
 * The reader wants all four. A document says xml:lang="en-GB", which has to
 * become a number before anything can be asked about it; a voice element
 * says what language it wants by name; and the answer the reader gives back
 * to the engine is the number again.
 *
 * Three things in here are the original's and are kept. Its own table
 * spells Finnish "Fn" going out and accepts "Fi" coming in, so a language
 * put in by name does not come back out under the name it went in by.
 * Hong Kong Chinese arrives as Chinese and leaves as Cantonese dialect one,
 * and Cantonese dialect one leaves as Chinese in Hong Kong -- the pair is
 * deliberately crossed. And `equals' ignores the code set while
 * `codeSetEquals' is the one that does not, which is the opposite of what
 * the two names suggest: the second compares the whole word.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "evv_abi.h"
#include "eci_ssml.h"

/* Where the two halves sit in the word. */
#define LI_LANGUAGE(p) ((uint8_t)((p) >> 16))
#define LI_CODESET(p)  ((uint8_t)(((p) & 0xff00) >> 8))
#define LI_DIALECT(p)  ((uint8_t)((p) & 0xff))

/* How long the text of one may be, which is what bounds the copy a
   language given as text gets. */
#define LI_TEXT_ROOM 12

/* ---- reading the word ------------------------------------------------- */

THIS uint8_t li_getLanguage(const LanguageId *l)
{
    return LI_LANGUAGE(l->packed);
}

THIS uint8_t li_getDialect(const LanguageId *l)
{
    return LI_DIALECT(l->packed);
}

THIS uint8_t li_getCodeSet(const LanguageId *l)
{
    return LI_CODESET(l->packed);
}

THIS int32_t li_getPackedInt(const LanguageId *l)
{
    return l->packed;
}

THIS const char *li_getString(const LanguageId *l)
{
    return l->full;
}

THIS const char *li_getMajorString(const LanguageId *l)
{
    return l->major;
}

THIS const char *li_getMinorString(const LanguageId *l)
{
    return l->minor;
}

THIS int32_t li_getIsLanguageAvailable(const LanguageId *l)
{
    return l->available;
}

THIS void li_setIsLanguageAvailable(LanguageId *l, int32_t yes)
{
    l->available = yes;
}

/* Two languages are the same when the language and the dialect agree; the
   code set is masked out. */
THIS int32_t li_equals(LanguageId *l, LanguageId *other)
{
    if (other == 0)
        return 0;
    return (other->packed & 0xffff00ff) == (l->packed & 0xffff00ff);
}

/* And this one is the whole word, code set included. */
THIS int32_t li_codeSetEquals(LanguageId *l, LanguageId *other)
{
    if (other == 0)
        return 0;
    return other->packed == l->packed;
}

THIS int32_t li_compareLanguage(const LanguageId *l, int32_t packed)
{
    return packed == l->packed;
}

/* ---- writing the text out of the word --------------------------------- */

/* The two names for each language, in the order the language number
   indexes them. Fifteen languages, numbered from one. */
THIS void li_setString(LanguageId *l)
{
    uint8_t lang    = li_getLanguage(l);
    uint8_t dialect = li_getDialect(l);

    sprintf(l->full, "%u.%u", lang, dialect);

    switch (lang) {
    case 1:
        strcpy(l->major, "En");
        if (dialect == 0)
            strcpy(l->minor, "US");
        else if (dialect == 1)
            strcpy(l->minor, "UK");
        else
            strcpy(l->minor, "");
        break;
    case 2:
        strcpy(l->major, "Es");
        if (dialect == 0)
            strcpy(l->minor, "ES");
        else if (dialect == 1)
            strcpy(l->minor, "MX");
        else
            strcpy(l->minor, "");
        break;
    case 3:
        strcpy(l->major, "Fr");
        if (dialect == 0)
            strcpy(l->minor, "FR");
        else if (dialect == 1)
            strcpy(l->minor, "CA");
        else
            strcpy(l->minor, "");
        break;
    case 4:
        strcpy(l->major, "Gr");
        strcpy(l->minor, "GR");
        break;
    case 5:
        strcpy(l->major, "It");
        strcpy(l->minor, "IT");
        break;
    case 6:
        strcpy(l->major, "Zh");
        if (dialect == 0)
            strcpy(l->minor, "CN");
        else if (dialect == 1)
            strcpy(l->minor, "TW");
        else
            strcpy(l->minor, "");
        break;
    case 7:
        strcpy(l->major, "Pt");
        strcpy(l->minor, "BR");
        break;
    case 8:
        strcpy(l->major, "Ja");
        strcpy(l->minor, "JP");
        break;
    /* Coming in it is "Fi"; going out the original writes "Fn". */
    case 9:
        strcpy(l->major, "Fn");
        strcpy(l->minor, "FN");
        break;
    case 10:
        strcpy(l->major, "Ko");
        strcpy(l->minor, "KR");
        break;
    /* Cantonese, whose second dialect is Chinese in Hong Kong. */
    case 11:
        if (dialect == 0) {
            strcpy(l->major, "Ct");
            strcpy(l->minor, "CN");
        } else if (dialect == 1) {
            strcpy(l->major, "Zh");
            strcpy(l->minor, "HK");
        } else {
            strcpy(l->major, "");
            strcpy(l->minor, "");
        }
        break;
    case 12:
        strcpy(l->major, "Nl");
        strcpy(l->minor, "BE");
        break;
    case 13:
        strcpy(l->major, "No");
        strcpy(l->minor, "NO");
        break;
    case 14:
        strcpy(l->major, "Sv");
        strcpy(l->minor, "SE");
        break;
    case 15:
        strcpy(l->major, "Da");
        strcpy(l->minor, "DK");
        break;
    default:
        strcpy(l->major, "");
        strcpy(l->minor, "");
        break;
    }
}

/* ---- and the word out of the text ------------------------------------- */

/* "1.0" back into a number. Both halves accumulate in a single byte, so
   anything above two hundred and fifty-five wraps; that is the original's
   arithmetic and no language is numbered that high. Note that the code set
   is not part of the text and comes out nought. */
THIS void li_setPackedInt(LanguageId *l)
{
    const char *s = l->full;
    uint8_t major = 0;
    uint8_t minor = 0;

    while (*s >= '0' && *s <= '9') {
        major = (uint8_t)(major * 10 + (*s - '0'));
        s++;
    }

    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') {
            minor = (uint8_t)(minor * 10 + (*s - '0'));
            s++;
        }
    }

    l->packed = ((int32_t)major << 16) | minor;
}

/* ---- setting one ------------------------------------------------------ */

THIS void li_setLanguageBytes(LanguageId *l, uint8_t lang, uint8_t dialect,
                              uint8_t codeset)
{
    l->packed = ((int32_t)lang << 16) | ((int32_t)codeset << 8) | dialect;
    li_setString(l);
}

THIS void li_setLanguagePacked(LanguageId *l, int32_t packed)
{
    l->packed = packed;
    li_setString(l);
}

/* A language given as its own text keeps that text and works the number out
   of it, so the two names are left as whatever they were. */
THIS void li_setLanguageString(LanguageId *l, const char *s)
{
    if (s == 0)
        return;

    l->packed = 0;
    strncpy(l->full, s, LI_TEXT_ROOM);
    l->full[LI_TEXT_ROOM] = 0;
    li_setPackedInt(l);
}

/* ---- and making one --------------------------------------------------- */

THIS void li_init(LanguageId *l)
{
    l->packed = 0;
    l->available = 0;
    li_setLanguageBytes(l, 0, 0, 0);
}

THIS void li_initPacked(LanguageId *l, int32_t packed)
{
    l->packed = packed;
    l->available = 0;
    li_setLanguagePacked(l, packed);
}

THIS void li_initBytes(LanguageId *l, uint8_t lang, uint8_t dialect,
                       uint8_t codeset)
{
    l->packed = 0;
    l->available = 0;
    li_setLanguageBytes(l, lang, dialect, codeset);
}

THIS void li_initString(LanguageId *l, const char *s)
{
    l->packed = 0;
    l->available = 0;
    li_setLanguageString(l, s);
}

/* The one a document reaches: xml:lang="en-GB" arrives here as its two
   halves and has to become a number.
 *
 * The country decides the dialect only where the language has more than
 * one, and the list of countries that mean British English is the
 * original's own: the two names for the United Kingdom, the two territories
 * that use its spelling, Ireland, South Africa, India and Australia, with
 * Canada on the end. Anything else is dialect nought. */
THIS void li_initNames(LanguageId *l, const char *major, const char *minor)
{
    int32_t packed = 0;

    l->packed = 0;
    l->available = 0;

    if (major == 0 || minor == 0)
        return;

    if (strcmp(major, "En") == 0)
        packed = 0x10000;
    else if (strcmp(major, "Es") == 0)
        packed = 0x20000;
    else if (strcmp(major, "Fr") == 0)
        packed = 0x30000;
    else if (strcmp(major, "De") == 0 || strcmp(major, "Gr") == 0)
        packed = 0x40000;
    else if (strcmp(major, "It") == 0)
        packed = 0x50000;
    else if (strcmp(major, "Zh") == 0)
        packed = 0x60000;
    else if (strcmp(major, "Pt") == 0)
        packed = 0x70000;
    else if (strcmp(major, "Ja") == 0)
        packed = 0x80000;
    else if (strcmp(major, "Fi") == 0)
        packed = 0x90000;
    else if (strcmp(major, "Ko") == 0)
        packed = 0xa0000;
    else if (strcmp(major, "Nl") == 0)
        packed = 0xc0000;
    else if (strcmp(major, "No") == 0)
        packed = 0xd0000;
    else if (strcmp(major, "Sv") == 0)
        packed = 0xe0000;
    else if (strcmp(major, "Da") == 0)
        packed = 0xf0000;
    else if (strcmp(major, "Ct") == 0)
        packed = 0xb0000;

    if (packed == 0x10000
        && (strcmp(minor, "GB") == 0 || strcmp(minor, "UK") == 0
            || strcmp(minor, "VG") == 0 || strcmp(minor, "IO") == 0
            || strcmp(minor, "IE") == 0 || strcmp(minor, "ZA") == 0
            || strcmp(minor, "IN") == 0 || strcmp(minor, "AU") == 0
            || strcmp(minor, "CA") == 0))
        packed = 0x10001;
    else if (packed == 0x20000 && strcmp(minor, "MX") == 0)
        packed = 0x20001;
    else if (packed == 0x30000 && strcmp(minor, "CA") == 0)
        packed = 0x30001;
    else if (packed == 0x60000 && strcmp(minor, "TW") == 0)
        packed = 0x60001;
    else if (packed == 0x60000 && strcmp(minor, "HK") == 0)
        packed = 0xb0001;

    li_setLanguagePacked(l, packed);
}

ALIAS("??0LanguageId@@QAE@XZ", "li_init");
ALIAS("??0LanguageId@@QAE@W4ECILanguageDialect@@@Z", "li_initPacked");
ALIAS("??0LanguageId@@QAE@EEE@Z", "li_initBytes");
ALIAS("??0LanguageId@@QAE@PBD@Z", "li_initString");
ALIAS("??0LanguageId@@QAE@PBD0@Z", "li_initNames");
ALIAS("?equals@LanguageId@@QAEHPAV1@@Z", "li_equals");
ALIAS("?codeSetEquals@LanguageId@@QAEHPAV1@@Z", "li_codeSetEquals");
ALIAS("?getLanguage@LanguageId@@QBEEXZ", "li_getLanguage");
ALIAS("?getDialect@LanguageId@@QBEEXZ", "li_getDialect");
ALIAS("?getCodeSet@LanguageId@@QBEEXZ", "li_getCodeSet");
ALIAS("?getPackedInt@LanguageId@@QBE?AW4ECILanguageDialect@@XZ",
      "li_getPackedInt");
ALIAS("?getString@LanguageId@@QBEPBDXZ", "li_getString");
ALIAS("?getMajorString@LanguageId@@QBEPBDXZ", "li_getMajorString");
ALIAS("?getMinorString@LanguageId@@QBEPBDXZ", "li_getMinorString");
ALIAS("?getIsLanguageAvailable@LanguageId@@QBEHXZ",
      "li_getIsLanguageAvailable");
ALIAS("?setIsLanguageAvailable@LanguageId@@QAEXH@Z",
      "li_setIsLanguageAvailable");
ALIAS("?setLanguage@LanguageId@@QAEXEEE@Z", "li_setLanguageBytes");
ALIAS("?setLanguage@LanguageId@@QAEXW4ECILanguageDialect@@@Z",
      "li_setLanguagePacked");
ALIAS("?setLanguage@LanguageId@@QAEXPBD@Z", "li_setLanguageString");
ALIAS("?compareLanguage@LanguageId@@QBEHW4ECILanguageDialect@@@Z",
      "li_compareLanguage");
ALIAS("?setString@LanguageId@@AAEXXZ", "li_setString");
ALIAS("?setPackedInt@LanguageId@@AAEXXZ", "li_setPackedInt");
