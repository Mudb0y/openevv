/* A language written as numbers.
 *
 * A language identifier carries the language packed into one word and, just
 * after it, the same thing as text: two numbers with a dot between them. The
 * packed form is the first number in the top half of the word and the second
 * in the bottom, and packing is done by reading the text back.
 *
 * Both numbers are accumulated in a single byte, so anything above two
 * hundred and fifty-five wraps. That is the original's arithmetic and it is
 * kept, because a language is never numbered that high.
 *
 * Names carry no prefix where the object uses none.
 */

#include <stdint.h>
#include <stdio.h>
#include "eci_synththread.h"
#include "evv_abi.h"

/* Where the text sits inside the identifier. */
#define LANG_TEXT(l)    ((const char *)(l) + 4)
#define LANG_PACKED(l)  (*(int32_t *)(l))

/* Whether two code sets have anything in common. */
int32_t isUnicodeCodeSet(int32_t a, int32_t b)
{
    return (a & b) > 0;
}

THIS void li_setPackedInt(void *l)
{
    const char *s = LANG_TEXT(l);
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

    LANG_PACKED(l) = ((int32_t)major << 16) | minor;
}

/* The other direction: write the packed word back out as text, which is
   where the text beside it comes from in the first place. Both halves are
   taken as bytes, so a language numbered above two hundred and fifty-five
   comes back wrong -- the same limit the packing has. */
THIS void li_setString(void *l)
{
    int32_t packed = LANG_PACKED(l);

    sprintf((char *)l + 4, "%u.%u",
            (unsigned)((packed >> 16) & 0xff),
            (unsigned)(packed & 0xff));
}

ALIAS("?setPackedInt@LangIdentifier@@AAEXXZ", "li_setPackedInt");
ALIAS("?setString@LangIdentifier@@AAEXXZ", "li_setString");
