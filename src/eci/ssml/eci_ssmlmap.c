/* SSML's values, turned into the engine's own.
 *
 * A document says `rate="slow"' or `pitch="+2st"' or `volume="+10%"'. The
 * engine takes ` `vs%-25 ' and ` `vbst+2 ' and ` `vv100 '. Everything in
 * this file is one of those translations, and between them they are the
 * whole of what SSML's attribute values mean here.
 *
 * Three kinds. The named values -- x-low through x-high, silent through
 * x-loud, none through x-strong -- are a table each. The measured ones are
 * a number with a unit after it, and each parameter takes different units:
 * pitch and range take hertz, semitones or a percentage, rate takes words
 * a minute or a percentage, volume takes a plain number or a percentage,
 * and a break takes milliseconds or seconds. And a few are not values at
 * all but formats: a date's field order, a Roman numeral, the VoiceXML
 * date, a language name.
 *
 * A percentage is relative and a plain number is not, which is why volume
 * is the one that has to be told what the engine is set to: `+10%' of
 * ninety-two is a hundred and one, which is then clipped to a hundred.
 * Everything else says its relative change to the engine and lets the
 * engine work it out.
 *
 * Two of IBM's mappings are worth knowing about, and both are kept.
 * `getVoiceGender' has the eight voices' genders written into it as a
 * table, and that table disagrees with the settings blob for three of them
 * -- voices three, seven and eight -- so a document asking for a female
 * voice gets a different answer from the one a caller reading the presets
 * would expect. And `interpret-as="number"' is routed to the ordinal
 * annotation rather than the cardinal one, so `123' comes out as `hundred
 * and twenty-third'; the reader in src/eci/ssml/eci_ssmlprocessor.c is where that
 * happens and it is said again there.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "evv_abi.h"
#include "eci_ssml.h"
#include "eci_ssmlmap.h"

extern int ralStrIcmp(int n, const char *a, const char *b);
extern void *cpp_new(uint32_t n) MANGLED("??2@YAPAXI@Z");
extern void  cpp_delete(void *p) MANGLED("??3@YAXPAX@Z");
extern int realWorld2eci(int32_t realWorld, int32_t which, int32_t target,
                         int32_t lo, int32_t hi);

/* The bounds each measured parameter is clipped to. Pitch and rate have
   them and range does not, which is the original's. */
#define PITCH_MAX      0x1a6      /* hertz */
#define PITCH_MIN      0x28
#define SPEED_MAX      0x511      /* words a minute */
#define SPEED_MIN      0x46
#define VOLUME_MAX     100
#define VOLUME_MIN     0
#define BREAK_MAX      300000     /* milliseconds */
#define BREAK_MIN      0

/* Which voice parameter the rate is, and the range the conversion from
   words a minute searches over. */
#define ECI_SPEED_PARAM 6
#define SPEED_ECI_LOW   0
#define SPEED_ECI_HIGH  250

/* ---- odds and ends --------------------------------------------------- */

int32_t tolowerstr(char *from, char *to, int32_t length)
{
    int32_t i;

    for (i = 0; i < length; i++) {
        if (isupper((unsigned char)from[i]))
            to[i] = (char)tolower((unsigned char)from[i]);
        else
            to[i] = from[i];
    }
    to[length] = 0;
    return 0;
}

/* What one Roman numeral letter is worth, or minus one for anything else. */
int32_t romanval(char c)
{
    switch (toupper((unsigned char)c)) {
    case 'I': return 1;
    case 'V': return 5;
    case 'X': return 10;
    case 'L': return 50;
    case 'C': return 100;
    case 'D': return 500;
    case 'M': return 1000;
    default:  return -1;
    }
}

/* A pronunciation given as phonemes, in the annotation the engine takes. */
void mapToIBMph(const char *ph, char *out)
{
    if (ph == 0 || out == 0)
        return;
    sprintf(out, " `[%s]", ph);
}

/* ---- a number and its unit ------------------------------------------- */

/* Read a signed number off the front of a string. What comes back is the
   number, with the sign handed back separately and the length of what was
   read -- so the caller can look at what follows and decide what unit it
   is. A fraction is read and rounded, and the fraction's digits count
   towards the length even though they do not count towards the value.
 *
 * Answers minus one when there is no number there at all. The value itself
 * is never negative, because the sign came out separately. */
int32_t getNumber(const char *s, char *sign, int32_t *length)
{
    int8_t      roundUp = 0;
    int32_t     value;
    const char *digits;
    const char *afterInt;
    char       *copy;

    if (s[0] == '+' || s[0] == '-') {
        *sign = s[0];
        s++;
    } else {
        *sign = 0;
    }

    digits = s;
    while (*s != 0 && isdigit((unsigned char)*s))
        s++;
    afterInt = s;

    if (s == digits && *s != '.')
        return -1;

    if (*s == '.') {
        if (!isdigit((unsigned char)s[1]) && s == digits)
            return -1;
        if (isdigit((unsigned char)s[1]) && s[1] >= '5')
            roundUp = 1;
        s++;
        while (*s != 0 && isdigit((unsigned char)*s))
            s++;
    }

    copy = cpp_new((uint32_t)(afterInt - digits) + 1);
    if (copy == 0)
        return -1;
    strncpy(copy, digits, (size_t)(afterInt - digits));
    copy[afterInt - digits] = 0;
    value = atoi(copy);

    *length = (int32_t)(s - digits) + (*sign != 0 ? 1 : 0);

    if (roundUp)
        value++;

    cpp_delete(copy);
    return value;
}

static int32_t clamp(int32_t v, int32_t low, int32_t high)
{
    if (v > high)
        return high;
    if (v < low)
        return low;
    return v;
}

/* ---- the four prosody parameters ------------------------------------- */

/* Pitch. Named, or a number in hertz, semitones or per cent. Hertz is the
   only one clipped, because the other two are relative and the engine
   clips them itself. */
int32_t mapToIBMetiPitch(const char *value, char *out)
{
    char        sign;
    int32_t     length = 0;
    int32_t     n;
    const char *unit;

    if (ralStrIcmp(0, value, "x-high") == 0) {
        sprintf(out, " `vbst+12 ");
        return 0;
    }
    if (ralStrIcmp(0, value, "high") == 0) {
        sprintf(out, " `vbst+6 ");
        return 0;
    }
    if (ralStrIcmp(0, value, "medium") == 0) {
        sprintf(out, " `vbmed ");
        return 0;
    }
    if (ralStrIcmp(0, value, "low") == 0) {
        sprintf(out, " `vbst-6 ");
        return 0;
    }
    if (ralStrIcmp(0, value, "x-low") == 0) {
        sprintf(out, " `vbst-12 ");
        return 0;
    }
    if (ralStrIcmp(0, value, "default") == 0) {
        sprintf(out, " `vbmed ");
        return 0;
    }

    n = getNumber(value, &sign, &length);
    if (n == -1)
        return -1;
    unit = value + length;

    if (sign == 0 && strcmp(unit, "Hz") == 0) {
        n = clamp(n, PITCH_MIN, PITCH_MAX);
        sprintf(out, " `vbhz%d ", n);
        return 0;
    }
    if ((sign == '+' || sign == '-') && strcmp(unit, "Hz") == 0) {
        sprintf(out, " `vbhz%c%d ", sign, n);
        return 0;
    }
    if ((sign == '+' || sign == '-') && strcmp(unit, "st") == 0) {
        sprintf(out, " `vbst%c%d ", sign, n);
        return 0;
    }
    if (ralStrIcmp(0, unit, "%") == 0) {
        if (sign == 0)
            sign = '+';
        sprintf(out, " `vb%%%c%d ", sign, n);
        return 0;
    }

    return -1;
}

/* Range, which is the same shape with nothing clipped. */
int32_t mapToIBMetiRange(const char *value, char *out)
{
    char        sign;
    int32_t     length = 0;
    int32_t     n;
    const char *unit;

    if (ralStrIcmp(0, value, "x-high") == 0) {
        sprintf(out, " `vfst+12 ");
        return 0;
    }
    if (ralStrIcmp(0, value, "high") == 0) {
        sprintf(out, " `vfst+6 ");
        return 0;
    }
    if (ralStrIcmp(0, value, "medium") == 0) {
        sprintf(out, " `vfmed ");
        return 0;
    }
    if (ralStrIcmp(0, value, "low") == 0) {
        sprintf(out, " `vfst-6 ");
        return 0;
    }
    if (ralStrIcmp(0, value, "x-low") == 0) {
        sprintf(out, " `vfst-12 ");
        return 0;
    }
    if (ralStrIcmp(0, value, "default") == 0) {
        sprintf(out, " `vfmed ");
        return 0;
    }

    n = getNumber(value, &sign, &length);
    if (n == -1)
        return -1;
    unit = value + length;

    if (sign == 0 && strcmp(unit, "Hz") == 0) {
        sprintf(out, " `vfhz%d ", n);
        return 0;
    }
    if ((sign == '+' || sign == '-') && strcmp(unit, "Hz") == 0) {
        sprintf(out, " `vfhz%c%d ", sign, n);
        return 0;
    }
    if ((sign == '+' || sign == '-') && strcmp(unit, "st") == 0) {
        sprintf(out, " `vfst%c%d ", sign, n);
        return 0;
    }
    if (ralStrIcmp(0, unit, "%") == 0) {
        if (sign == 0)
            sign = '+';
        sprintf(out, " `vf%%%c%d ", sign, n);
        return 0;
    }

    return -1;
}

/* Rate. A plain number is words a minute and has to be converted into the
   engine's own scale, which is a search rather than a formula: the engine
   only knows how to go the other way, so realWorld2eci walks the scale
   until it finds the setting that produces the rate asked for. */
int32_t mapToIBMetiSpeed(const char *value, char *out)
{
    char        sign;
    int32_t     length = 0;
    int32_t     n;
    const char *unit;

    if (ralStrIcmp(0, value, "x-fast") == 0) {
        sprintf(out, " `vs%%+50 ");
        return 0;
    }
    if (ralStrIcmp(0, value, "fast") == 0) {
        sprintf(out, " `vs%%+25 ");
        return 0;
    }
    if (ralStrIcmp(0, value, "slow") == 0) {
        sprintf(out, " `vs%%-25 ");
        return 0;
    }
    if (ralStrIcmp(0, value, "x-slow") == 0) {
        sprintf(out, " `vs%%-50 ");
        return 0;
    }
    if (ralStrIcmp(0, value, "medium") == 0) {
        sprintf(out, " `vsmed ");
        return 0;
    }
    if (ralStrIcmp(0, value, "default") == 0) {
        sprintf(out, " `vsmed ");
        return 0;
    }

    n = getNumber(value, &sign, &length);
    if (n == -1)
        return -1;
    unit = value + length;

    if (sign == 0 && unit[0] == 0) {
        n = clamp(n, SPEED_MIN, SPEED_MAX);
        n = realWorld2eci(1, ECI_SPEED_PARAM, n, SPEED_ECI_LOW,
                          SPEED_ECI_HIGH);
        sprintf(out, " `vs%d ", n);
        return 0;
    }
    if ((sign == '+' || sign == '-') && unit[0] == 0) {
        sprintf(out, " `vswpm%c%d ", sign, n);
        return 0;
    }
    if (ralStrIcmp(0, unit, "%") == 0) {
        if (sign == 0)
            sign = '+';
        sprintf(out, " `vs%%%c%d ", sign, n);
        return 0;
    }

    return -1;
}

/* Volume, which is the one that needs to know what the engine is set to,
   because a percentage is a percentage of that. What comes back is the
   volume that was written, so the reader can push it on its stack. */
int32_t mapToIBMetiVolume(const char *value, char *out, int32_t current)
{
    char        sign;
    int32_t     length = 0;
    int32_t     n;
    const char *unit;

    if (ralStrIcmp(0, value, "x-loud") == 0) {
        sprintf(out, " `vv100 ");
        return 100;
    }
    if (ralStrIcmp(0, value, "loud") == 0) {
        sprintf(out, " `vv90 ");
        return 90;
    }
    if (ralStrIcmp(0, value, "medium") == 0) {
        sprintf(out, " `vv80 ");
        return 80;
    }
    if (ralStrIcmp(0, value, "soft") == 0) {
        sprintf(out, " `vv50 ");
        return 50;
    }
    if (ralStrIcmp(0, value, "x-soft") == 0) {
        sprintf(out, " `vv30 ");
        return 30;
    }
    if (ralStrIcmp(0, value, "silent") == 0) {
        sprintf(out, " `vv0 ");
        return 0;
    }
    if (ralStrIcmp(0, value, "default") == 0) {
        sprintf(out, " `vv92 ");
        return 92;
    }

    n = getNumber(value, &sign, &length);
    if (n == -1)
        return -1;
    unit = value + length;

    if (sign == 0 && unit[0] == 0) {
        n = clamp(n, VOLUME_MIN, VOLUME_MAX);
        sprintf(out, " `vv%d ", n);
        return n;
    }
    if (sign == '+' && unit[0] == 0) {
        current += n;
        if (current > VOLUME_MAX)
            current = VOLUME_MAX;
        sprintf(out, " `vv%d ", current);
        return current;
    }
    if (sign == '-' && unit[0] == 0) {
        current -= n;
        if (current < VOLUME_MIN)
            current = VOLUME_MIN;
        sprintf(out, " `vv%d ", current);
        return current;
    }
    if (ralStrIcmp(0, unit, "%") == 0) {
        if (sign == 0 || sign == '+')
            current = current + current * n / 100;
        else
            current = current - current * n / 100;
        current = clamp(current, VOLUME_MIN, VOLUME_MAX);
        sprintf(out, " `vv%d ", current);
        return current;
    }

    return -1;
}

/* ---- a break --------------------------------------------------------- */

/* The two ways of asking for one: a strength by name or a length in
   milliseconds or seconds. The annotation carries no trailing space, so
   the word after a break runs straight on from it. */
int32_t mapToIBMtime(char *value, char *out)
{
    char        sign;
    int32_t     length = 0;
    int32_t     n;
    const char *unit;

    if (ralStrIcmp(0, value, "none") == 0) {
        strcpy(out, " `p0");
        return 0;
    }
    if (ralStrIcmp(0, value, "X-small") == 0
        || ralStrIcmp(0, value, "x-weak") == 0) {
        strcpy(out, " `p1");
        return 0;
    }
    if (ralStrIcmp(0, value, "Small") == 0
        || ralStrIcmp(0, value, "weak") == 0) {
        strcpy(out, " `p250");
        return 0;
    }
    if (ralStrIcmp(0, value, "Medium") == 0) {
        strcpy(out, " `p500");
        return 0;
    }
    if (ralStrIcmp(0, value, "Large") == 0
        || ralStrIcmp(0, value, "strong") == 0) {
        strcpy(out, " `p1000");
        return 0;
    }
    if (ralStrIcmp(0, value, "X-Large") == 0
        || ralStrIcmp(0, value, "x-strong") == 0) {
        strcpy(out, " `p1500");
        return 0;
    }

    n = getNumber(value, &sign, &length);
    if (n == -1) {
        strcpy(out, "");
        return -1;
    }
    unit = value + length;

    if (sign == 0 && ralStrIcmp(0, unit, "ms") == 0) {
        n = clamp(n, BREAK_MIN, BREAK_MAX);
        sprintf(out, " `p%d", n);
        return 0;
    }
    if (sign == 0 && ralStrIcmp(0, unit, "s") == 0) {
        n = n * 1000;
        n = clamp(n, BREAK_MIN, BREAK_MAX);
        sprintf(out, " `p%d", n);
        return 0;
    }

    strcpy(out, "");
    return -1;
}

/* ---- emphasis -------------------------------------------------------- */

/* Emphasis is a pause and a slowing rather than a parameter of its own,
   which is what makes `strong' two annotations. */
int32_t mapToIBMlevel(const char *value, char *out)
{
    if (ralStrIcmp(0, value, "strong") == 0) {
        sprintf(out, " `p1 `vs%s ", "%-20");
        return 0;
    }
    if (ralStrIcmp(0, value, "moderate") == 0) {
        sprintf(out, " `p1 ");
        return 0;
    }
    if (ralStrIcmp(0, value, "none") == 0) {
        sprintf(out, " `0 ");
        return 0;
    }
    if (ralStrIcmp(0, value, "reduced") == 0) {
        sprintf(out, " `00 ");
        return 0;
    }
    return -1;
}

/* ---- the voice ------------------------------------------------------- */

int32_t mapToIBMgender(const char *value)
{
    if (ralStrIcmp(0, value, "male") == 0)
        return 0;
    if (ralStrIcmp(0, value, "female") == 0)
        return 1;
    return -1;
}

/* What gender each of the eight voices is. This is IBM's own table and it
   disagrees with the voice presets in the settings blob for voices three,
   seven and eight; the table is what the reader goes by. */
int32_t getVoiceGender(int32_t voice)
{
    switch (voice) {
    case 1: return 0;
    case 2: return 1;
    case 3: return 0;
    case 4: return 0;
    case 5: return 0;
    case 6: return 1;
    case 7: return 0;
    case 8: return 1;
    default: return -1;
    }
}

/* Which voice a gender means, holding the age band the voice already has:
   an adult voice becomes voice one or two, an elderly one seven or eight,
   and the child is the child whichever gender is asked for. */
int32_t mapGenderToVoice(int32_t voice, int32_t gender)
{
    if (gender == 0) {
        switch (voice) {
        case 1: case 2: case 4: case 5: case 6:
            return 1;
        case 7: case 8:
            return 8;
        case 3:
            return 3;
        default:
            return -1;
        }
    }
    if (gender == 1) {
        switch (voice) {
        case 1: case 2: case 4: case 5: case 6:
            return 2;
        case 7: case 8:
            return 7;
        case 3:
            return 3;
        default:
            return -1;
        }
    }
    return voice;
}

/* An age in years, and the gender it is wanted in, to a voice: under
   fifteen is the child, under sixty an adult, and anything older is
   elderly. */
int32_t mapToIBMage(const char *value, int32_t gender)
{
    size_t  length = strlen(value);
    size_t  i;
    int32_t years;

    for (i = 0; i < length; i++)
        if (!isdigit((unsigned char)value[i]))
            return -1;

    years = atoi(value);

    if (gender == 0) {
        if (years <= 14)
            return 3;
        if (years > 14 && years < 60)
            return 1;
        return 8;
    }

    if (years <= 14)
        return 3;
    if (years > 14 && years < 60)
        return 2;
    return 7;
}

/* A variant number, which asks for another voice of the same kind. What
   each one means depends on which voice is in force, and the table is
   IBM's. */
int32_t mapToIBMvariant(const char *value, int32_t voice)
{
    size_t  length = strlen(value);
    size_t  i;
    int32_t want;

    for (i = 0; i < length; i++)
        if (!isdigit((unsigned char)value[i]))
            return voice;

    want = atoi(value);

    switch (voice) {
    case 1:
        if (want == 1) return 4;
        if (want == 2) return 5;
        return voice;
    case 2:
        if (want == 1) return 6;
        return voice;
    case 4:
        if (want == 1) return 1;
        if (want == 2) return 5;
        return voice;
    case 5:
        if (want == 1) return 1;
        if (want == 2) return 4;
        return voice;
    case 6:
        if (want == 1) return 2;
        return voice;
    default:
        return voice;
    }
}

/* ---- the formats ----------------------------------------------------- */

/* A date's field order. What comes out is the head of the annotation; the
   reader puts the text and the closing bracket after it. */
int32_t mapToIBMdate(const char *format, char *out)
{
    static const struct { const char *name; const char *head; } ORDER[] = {
        { "mdy", " `datemdy[" },
        { "ymd", " `dateymd[" },
        { "dmy", " `datedmy[" },
        { "ydm", " `dateydm[" },
        { "my",  " `datemy["  },
        { "md",  " `datemd["  },
        { "dm",  " `datedm["  },
        { "ym",  " `dateym["  }
    };
    size_t i;

    for (i = 0; i < sizeof ORDER / sizeof ORDER[0]; i++) {
        if (ralStrIcmp(0, format, ORDER[i].name) == 0) {
            sprintf(out, "%s", ORDER[i].head);
            return 0;
        }
    }
    return -1;
}

/* A Roman numeral, turned into the number it is. Digits are passed through
   as they are, and anything that is neither is handed back unchanged with
   a refusal. */
int32_t mapToIBMroman(const char *value, char *out, int32_t length)
{
    int32_t previous = 0xffff;
    int32_t total = 0;
    int32_t room = length;
    char   *trimmed;
    char   *copy;
    int32_t i;

    if (length == 0) {
        out[0] = 0;
        return 0;
    }

    trimmed = stripspaces((char *)value, &room);

    copy = malloc((size_t)room + 1);
    if (copy == 0)
        return -1;
    strncpy(copy, trimmed, (size_t)room);
    copy[room] = 0;

    /* A number written as a number stays one. */
    if (isdigit((unsigned char)copy[0])) {
        for (i = 0; i < room && isdigit((unsigned char)copy[i]); i++)
            out[i] = copy[i];

        if (i != room) {
            strncpy(out, value, (size_t)length);
            out[length] = 0;
            free(copy);
            return -1;
        }

        out[i] = 0;
        free(copy);
        return 1;
    }

    for (i = 0; i < room; i++) {
        int32_t v = romanval(copy[i]);

        if (v < 0) {
            strncpy(out, value, (size_t)length);
            out[length] = 0;
            free(copy);
            return -1;
        }

        if (v <= previous)
            total += v;
        else
            total += v - 2 * previous;
        previous = v;
    }

    sprintf(out, "%i", total);
    free(copy);
    return 0;
}

/* The VoiceXML date, which is eight characters of yyyymmdd with a question
   mark for whatever is not known. What comes out is the annotation naming
   whichever fields are present, in the order month, day, year -- so a
   whole date reads as month, day and year whatever the document's own
   locale would have said.
 *
 * Answers one when only one field is known, since then the annotation is
 * not wanted and the field itself is the answer; two when none is; and
 * nought when the annotation was written. */
int32_t mapToIBMvxmldate(const char *value, char *out, int32_t length)
{
    const char *year = 0;
    const char *month = 0;
    const char *day = 0;
    const char *p;
    int32_t     i;
    int32_t     at;

    if (length < 8 || length > 12)
        return -1;

    p = value;
    while (*p == ' ' && (p - value) < length)
        p++;

    if (length - (int32_t)(p - value) < 8)
        return -1;

    for (i = 0; i < 4 && isdigit((unsigned char)p[i]); i++)
        ;
    if (i == 4)
        year = p;

    for (i = 4; i < 6 && isdigit((unsigned char)p[i]); i++)
        ;
    if (i == 6)
        month = p + 4;

    for (i = 6; i < 8 && isdigit((unsigned char)p[i]); i++)
        ;
    if (i == 8)
        day = p + 6;

    if (month == 0)
        return -1;

    /* Two of the three missing means there is nothing to order. */
    if (year == month || year == day || month == day) {
        if (year != 0) {
            strncpy(out, year, 4);
            out[4] = 0;
            return 1;
        }
        if (month != 0) {
            strncpy(out, month, 2);
            out[2] = 0;
            return 1;
        }
        if (day != 0) {
            strncpy(out, day, 2);
            out[2] = 0;
            return 1;
        }
        strncpy(out, value, (size_t)length);
        out[length] = 0;
        return 2;
    }

    at = 0;
    at += sprintf(out + at, " `date");
    if (month != 0)
        out[at++] = 'm';
    if (day != 0)
        out[at++] = 'd';
    if (year != 0)
        out[at++] = 'y';
    out[at++] = '[';

    if (month != 0) {
        strncpy(out + at, month, 2);
        at += 2;
    }
    if (month != 0 && day != 0)
        out[at++] = '/';
    if (day != 0) {
        strncpy(out + at, day, 2);
        at += 2;
    }
    if ((day != 0 && year != 0) || (month != 0 && year != 0))
        out[at++] = '/';
    if (year != 0) {
        strncpy(out + at, year, 4);
        at += 4;
    }
    out[at++] = ']';
    out[at++] = 0;

    return 0;
}

/* A language written the way a document writes one -- `en-GB', `fr_CA' --
   turned into the number the engine knows it by. The country only decides
   anything where the language has more than one dialect, and Hong Kong
   Chinese comes out as Cantonese, which is the same crossing as in
   src/eci/lang/eci_languageid.c. */
/* A document's `xml:lang' as a number.

   Fifteen languages, and the fifteenth is the one thing here that is not
   IBM's. Its own entry at family seventeen is Thai, which it never shipped
   a name for, a voice for or a rule of -- `LanguageId' in
   src/eci/lang/eci_languageid.c stops at fifteen and has no seventeen at all, so a
   Thai document became a number nothing else in the engine could say
   anything about. Polish is family seventeen in this tree and is real, so
   the slot is Polish's. That is a deliberate divergence and
   `docs/status.md' lists it; a Thai document now reads as Polish, and
   there is no Thai in this SDK for that to cost anything.

   The country only decides the dialect where the language has more than
   one, and which countries mean British English is the original's own
   list. */
int32_t mapToIBMlang(const char *value, int32_t length)
{
    static const struct { const char *name; int32_t packed; } MAJOR[] = {
        { "EN", 0x10000 }, { "ES", 0x20000 }, { "FR", 0x30000 },
        { "DE", 0x40000 }, { "IT", 0x50000 }, { "ZH", 0x60000 },
        { "PT", 0x70000 }, { "JA", 0x80000 }, { "FI", 0x90000 },
        { "KO", 0xa0000 }, { "NL", 0xc0000 }, { "NO", 0xd0000 },
        { "SV", 0xe0000 }, { "DA", 0xf0000 }, { "PL", 0x110000 }
    };
    static const char *BRITISH[] = {
        "GB", "UK", "VG", "IO", "IE", "ZA", "IN", "AU", "CA"
    };
    char   *upper;
    char   *major;
    char   *minor;
    int32_t packed = 0;
    int32_t i;

    upper = malloc((size_t)length + 1);
    if (upper == 0)
        return 0;

    for (i = 0; i < length; i++)
        upper[i] = (char)toupper((unsigned char)value[i]);
    upper[length] = 0;

    major = strtok(upper, "-_");
    for (i = 0; i < (int32_t)(sizeof MAJOR / sizeof MAJOR[0]); i++) {
        if (strcmp(major, MAJOR[i].name) == 0) {
            packed = MAJOR[i].packed;
            break;
        }
    }

    minor = strtok(0, "-_");
    if (minor != 0) {
        if (packed == 0x10000) {
            for (i = 0; i < (int32_t)(sizeof BRITISH / sizeof BRITISH[0]);
                 i++) {
                if (strcmp(minor, BRITISH[i]) == 0) {
                    packed = 0x10001;
                    break;
                }
            }
        } else if (packed == 0x20000 && strcmp(minor, "MX") == 0) {
            packed = 0x20001;
        } else if (packed == 0x30000 && strcmp(minor, "CA") == 0) {
            packed = 0x30001;
        } else if (packed == 0x60000 && strcmp(minor, "TW") == 0) {
            packed = 0x60001;
        } else if (packed == 0x60000 && strcmp(minor, "HK") == 0) {
            packed = 0xb0001;
        }
    }

    free(upper);
    return packed;
}

ALIAS("?tolowerstr@@YAHPAD0H@Z", "tolowerstr");
ALIAS("?romanval@@YAHD@Z", "romanval");
ALIAS("?mapToIBMph@@YAXPBDPAD@Z", "mapToIBMph");
ALIAS("?getNumber@@YAHPBDPADPAH@Z", "getNumber");
ALIAS("?mapToIBMetiPitch@@YAHPBDPAD@Z", "mapToIBMetiPitch");
ALIAS("?mapToIBMetiRange@@YAHPBDPAD@Z", "mapToIBMetiRange");
ALIAS("?mapToIBMetiSpeed@@YAHPBDPAD@Z", "mapToIBMetiSpeed");
ALIAS("?mapToIBMetiVolume@@YAHPBDPADH@Z", "mapToIBMetiVolume");
ALIAS("?mapToIBMtime@@YAHPAD0@Z", "mapToIBMtime");
ALIAS("?mapToIBMlevel@@YAHPBDPAD@Z", "mapToIBMlevel");
ALIAS("?mapToIBMgender@@YAHPBD@Z", "mapToIBMgender");
ALIAS("?getVoiceGender@@YAHH@Z", "getVoiceGender");
ALIAS("?mapGenderToVoice@@YAHHH@Z", "mapGenderToVoice");
ALIAS("?mapToIBMage@@YAHPBDH@Z", "mapToIBMage");
ALIAS("?mapToIBMvariant@@YAHPBDH@Z", "mapToIBMvariant");
ALIAS("?mapToIBMdate@@YAHPBDPAD@Z", "mapToIBMdate");
ALIAS("?mapToIBMroman@@YAHPBDPADH@Z", "mapToIBMroman");
ALIAS("?mapToIBMvxmldate@@YAHPBDPADH@Z", "mapToIBMvxmldate");
ALIAS("?mapToIBMlang@@YA?AW4ECILanguageDialect@@PBDH@Z", "mapToIBMlang");
