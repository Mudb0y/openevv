/* Sixteen-bit characters written out as bytes.
 *
 * The engine holds text as UCS-2 and hands it out as UTF-8, which for values
 * that fit in sixteen bits is one, two or three bytes: below a hundred and
 * twenty-eight the character is its own byte; below two thousand and
 * forty-eight it is two; anything else is three.
 *
 * The caller says how much room there is by filling in the length before the
 * call, and reads back how much was used after it. If the next character
 * would not fit, the walk stops there rather than half-writing it, the answer
 * is minus three, and the length still says truthfully how many whole bytes
 * were written. Surrogate pairs are not treated as pairs; each half is
 * encoded on its own, which is what the original does.
 */

#include <stdint.h>
#include "eci_synththread.h"
#include "evv_abi.h"

#define UTF8_NO_ROOM  (-3)

int32_t u8_convertUCS2toUTF8(const uint16_t *src, int32_t count,
                             uint8_t *dst, uint32_t *length)
{
    const uint16_t *end   = src + count;
    uint8_t        *out   = dst;
    const uint8_t  *limit = dst + *length;
    int32_t         rc    = 0;

    while (src < end) {
        uint16_t ch = *src++;
        uint16_t bytes;

        if (ch < 0x80)
            bytes = 1;
        else if (ch < 0x800)
            bytes = 2;
        else
            bytes = 3;

        if (out + bytes > limit) {
            src--;
            rc = UTF8_NO_ROOM;
            break;
        }

        switch (bytes) {
        case 3:
            out[0] = (uint8_t)((ch >> 12) + 0xe0);
            out[1] = (uint8_t)(((ch & 0xfc0) >> 6) + 0x80);
            out[2] = (uint8_t)((ch & 0x3f) + 0x80);
            break;
        case 2:
            out[0] = (uint8_t)(((ch & 0x7c0) >> 6) + 0xc0);
            out[1] = (uint8_t)((ch & 0x3f) + 0x80);
            break;
        case 1:
            out[0] = (uint8_t)ch;
            break;
        }

        out += bytes;
    }

    *length = (uint32_t)(out - dst);
    return rc;
}

ALIAS("?ConvertUCS2toUTF8@@YAHPBGHPAEAAK@Z", "u8_convertUCS2toUTF8");
