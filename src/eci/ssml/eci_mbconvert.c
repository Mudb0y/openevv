/* Text with wide characters in it, made narrow so a lexer can read it.
 *
 * The XML scanner under the SSML reader is generated code with a
 * two-hundred-and-fifty-six entry character table in it, so it can only see
 * one byte at a time. A document is UTF-8. Something has to stand between
 * them, and rather than widen the scanner IBM narrowed the text: every byte
 * of a multi-byte character becomes a bell, that byte's value written out in
 * decimal, and another bell. So `ą' -- two bytes, 0xc4 and 0x85 -- arrives
 * at the scanner as seven ASCII characters, and the scanner treats the whole
 * run as ordinary text and hands it back untouched.
 *
 * `Sbcs2Mbcs' is the way back, and the two are what the reader wraps every
 * piece of character data in.
 *
 * The same pass resolves a numeric character reference. `&#261;' and
 * `&#x105;' both become the character they name, which is then put through
 * the bell form as well -- so a reference to a non-ASCII character comes out
 * the same as the character written directly. That is done here rather than
 * in the scanner because the scanner's own escape handling only knows the
 * five named entities.
 *
 * One divergence, and it is the only one. The original terminates the digits
 * by writing a nought over the semicolon in the caller's own text, which
 * means eciAddText's argument comes back modified and a caller that handed
 * over a string literal does not come back at all. The digits are copied out
 * here instead. What is written to the output is identical either way.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "evv_abi.h"
#include "eci_ssml.h"

/* The byte that brackets a narrowed one. Nothing in a document can carry it
   -- a bell is not valid XML character data -- which is why it was chosen. */
#define NARROW_MARK 7

/* How many digits a reference may have; four is enough for the largest
   character and this is more. */
#define REFERENCE_DIGITS 16

extern int32_t ConvertUCS32toUTF8(const uint32_t *src, uint32_t count,
                                  uint8_t *dst, uint32_t *length);

/* How many bytes the character starting here takes, read off the top bits of
   its first byte. A byte that can only be a continuation answers minus
   one. */
int32_t getCharByteCount(const uint8_t *s)
{
    if ((s[0] & 0x80) == 0)
        return 1;
    if ((s[0] & 0xe0) == 0xc0)
        return 2;
    if ((s[0] & 0xf0) == 0xe0)
        return 3;
    if ((s[0] & 0xf8) == 0xf0)
        return 4;
    return -1;
}

/* UTF-8 in, the narrow form out. Answers how many bytes were written, or
   what had been written when a byte that could not start a character was
   met -- which is a stop rather than an error, and is the original's own
   arrangement. */
int32_t Mbcs2Sbcs(char *in, char *out)
{
    char *p = in;
    char *q = out;

    while (p != 0 && *p != 0) {
        int32_t bytes = getCharByteCount((const uint8_t *)p);
        int32_t i;

        if (bytes == -1) {
            *q = 0;
            return (int32_t)(q - out);
        }

        if (bytes != 1) {
            for (i = 0; i < bytes; i++) {
                uint8_t b;

                *q++ = NARROW_MARK;
                b = (uint8_t)*p++;
                sprintf(q, "%d", b);
                q += strlen(q);
                *q++ = NARROW_MARK;
            }
            continue;
        }

        /* A numeric character reference, in either base. */
        if (p[1] != 0 && p[0] == '&' && p[1] == '#' && p[2] != 0) {
            int32_t  hex = (p[2] == 'x');
            char    *digits = hex ? p + 3 : p + 2;
            int32_t  length = hex ? 3 : 2;
            char    *r = digits;
            char     copy[REFERENCE_DIGITS];
            size_t   n;

            while (r != 0 && *r != 0 && *r != ';') {
                r++;
                length++;
            }

            if (*r != ';') {
                *q++ = *p++;
                continue;
            }

            n = (size_t)(r - digits);
            if (n >= sizeof copy)
                n = sizeof copy - 1;
            memcpy(copy, digits, n);
            copy[n] = 0;

            {
                uint32_t point = hex ? (uint32_t)strtol(copy, 0, 16)
                                     : (uint32_t)atoi(copy);
                uint8_t  utf8[4];
                uint32_t room = sizeof utf8;
                char     narrow[sizeof utf8 * 5 + 1];

                p = p + length + 1;

                memset(utf8, 0, sizeof utf8);
                if (ConvertUCS32toUTF8(&point, 1, utf8, &room) == 0) {
                    int32_t made = Mbcs2Sbcs((char *)utf8, narrow);

                    if (made != -1) {
                        for (i = 0; i < made; i++)
                            *q++ = narrow[i];
                    }
                }
            }
            continue;
        }

        *q++ = *p++;
    }

    *q = 0;
    return (int32_t)(q - out);
}

/* And back again: a bell, a number and a bell become the byte that number
   is. A bell with no closing bell after it is minus one, since the text
   cannot have come from the pass above. Answers the length of what it
   wrote.

   This one does write a nought into what it is given, over the closing
   bell, and that is kept: what it is ever given is the reader's own
   intermediate buffer rather than anything of the caller's. */
int32_t Sbcs2Mbcs(char *in, char *out)
{
    char *p;
    char *q;

    if (in == 0)
        return 0;

    p = in;
    q = out;

    while (p != 0 && *p != 0) {
        char *end;

        if (*p != NARROW_MARK) {
            *q++ = *p++;
            continue;
        }

        end = strchr(p + 1, NARROW_MARK);
        if (end == 0)
            return -1;

        *end = 0;
        *q++ = (char)atoi(p + 1);
        p = end + 1;
    }

    *q = 0;
    return (int32_t)strlen(out);
}

ALIAS("?Mbcs2Sbcs@@YAHPAD0@Z", "Mbcs2Sbcs");
ALIAS("?Sbcs2Mbcs@@YAHPAD0@Z", "Sbcs2Mbcs");
ALIAS("?getCharByteCount@@YAHPAE@Z", "getCharByteCount");
