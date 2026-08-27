/* Shift-JIS to UCS-2 and back.
 *
 * Japanese text reaches the engine as Shift-JIS or as UCS-2, and the analyser
 * behind this works in Shift-JIS, so a caller who sent UCS-2 has it converted
 * here first. The tables are IBM's, lifted whole; nothing here decides
 * anything about a character, it only looks it up.
 *
 * The converter keeps one buffer of each kind and grows it when a longer
 * string arrives, so what it answers is a pointer into itself and is good only
 * until the next call. The byte buffer grows in kilobytes rather than to the
 * length asked for, which is the original's arithmetic and not a rounding of
 * mine.
 *
 * Three things about the original are written down here rather than smoothed
 * over. Its getMBCS2UCSTableIdx is private and nothing calls it, so it is left
 * out. A byte of 0x80, 0xfe or 0xff makes its MBCSToUCS2 walk forever. And a
 * pair beginning 0xfd is looked up past the end of the table it is looked up
 * in. Those last two are the only places this file does not do what IBM's
 * does, and each says why where it happens.
 *
 * test/romprims.sh is what found the second of them and is what holds the rest
 * of this file to IBM's answer: every single byte, every two-byte pair, and all
 * sixty-five thousand code points in the other direction.
 */

#include <string.h>
#include "jprom.h"

/* Where each table starts. The lead-byte tables are indexed by the whole
   two-byte character with its lead byte brought down to nought, so each lead
   byte owns 256 entries whether Shift-JIS uses them or not; the kana table is
   indexed by the single byte, and the wide-character tables by the code point
   itself. */
#define LEAD1_FIRST 0x81    /* 0x81 to 0x9f */
#define LEAD1_LAST  0x9f
/* The second lead-byte table holds 29 lead bytes, 0xe0 to 0xfc, and the
   original's own test lets 0xfd through it as well -- so for any pair
   beginning 0xfd it reads past the end of the table, into whichever object
   the linker happened to put next. That is not an answer to reproduce: it is
   not the same answer twice if anything about the link changes, and 0xfd is
   not a Shift-JIS lead byte in the first place. The bound here is the one the
   table can actually answer for, and a pair beginning 0xfd is dropped the way
   any other unknown byte is. */
#define LEAD2_FIRST 0xe0    /* 0xe0 to 0xfc */
#define LEAD2_LAST  0xfc
#define KANA_FIRST  0xa0    /* 0xa0 to 0xdf, one byte each */
#define KANA_LAST   0xdf

/* Above this a code point is not in the table that holds the low ones, and
   above the second it is in the private-use range the R table covers. */
#define WIDE_FIRST  0xa000
#define R_FIRST     0xe000

/* What a converter answers. Nought is well, and these are the two errors it
   can report. */
#define OK          0
#define ERR_MEMORY  ROM_ERR_MEMORY
#define ERR_UNICODE ROM_ERR_UNICODE

static uint16_t at16(const uint8_t *table, uint32_t i)
{
    return ((const uint16_t *)table)[i];
}

UnicodeConverter *uc_ctor(UnicodeConverter *c, RomInstParam *param)
{
    c->mbcs = 0;
    c->mbcsRoom = 0;
    c->ucs = 0;
    c->ucsRoom = 0;
    c->param = param;
    return c;
}

void uc_dtor(UnicodeConverter *c)
{
    if (c->mbcs != 0)
        cpp_delete(c->mbcs);
    if (c->ucs != 0)
        cpp_delete(c->ucs);
}

/* The original loads no table: they are in the program. It answers one and
   looks at neither of its arguments. */
int32_t uc_initTable(UnicodeConverter *c, const char *path, int32_t lang)
{
    (void)c;
    (void)path;
    (void)lang;
    return 1;
}

uint32_t ucs2len(const uint16_t *s)
{
    uint32_t n = 0;

    while (*s++ != 0)
        n++;
    return n;
}

/* Room for `want' characters in the wide buffer, keeping what is there if it
   is already big enough. Answers nought when there is no room to be had. */
static int32_t wideRoom(UnicodeConverter *c, uint32_t want)
{
    if (c->ucs != 0 && c->ucsRoom >= want)
        return 1;
    if (c->ucs != 0)
        cpp_delete(c->ucs);
    c->ucsRoom = want;
    c->ucs = (uint16_t *)cpp_new(c->ucsRoom * 2);
    if (c->ucs == 0) {
        rp_setError(c->param, ERR_MEMORY);
        return 0;
    }
    return 1;
}

int32_t uc_MBCSToUCS2(UnicodeConverter *c, const char *in, uint16_t **out)
{
    const uint8_t *p;
    uint16_t      *q;
    size_t         n;

    if (in == 0) {
        *out = 0;
        return OK;
    }

    n = strlen(in);
    if (!wideRoom(c, (uint32_t)n + 1))
        return ERR_MEMORY;
    if (c->ucs == 0)
        return ERR_UNICODE;

    memset(c->ucs, 0, n * 2 + 2);

    p = (const uint8_t *)in;
    q = c->ucs;
    while (*p != 0) {
        if (*p < 0x80) {
            *q++ = *p++;
        } else if (*p >= LEAD1_FIRST && *p <= LEAD1_LAST) {
            *q++ = at16(jajp_m_pLeadByteTable1,
                        (uint32_t)((*p - LEAD1_FIRST) << 8) + p[1]);
            p += 2;
        } else if (*p >= LEAD2_FIRST && *p <= LEAD2_LAST) {
            *q++ = at16(jajp_m_pLeadByteTable2,
                        (uint32_t)((*p - LEAD2_FIRST) << 8) + p[1]);
            p += 2;
        } else if (*p >= KANA_FIRST && *p <= KANA_LAST) {
            *q++ = at16(jajp_m_pKanaTable, (uint32_t)(*p - KANA_FIRST));
            p++;
        } else {
            /* 0x80, 0xfe and 0xff reach here, and the original does not
               advance: it walks the same byte for ever and the synthesis
               thread never comes back. That is a fault rather than a
               behaviour to reproduce -- an engine that hangs on a byte says
               nothing at all, so there are no samples of IBM's to differ
               from -- and the byte is dropped instead. */
            p++;
        }
    }

    *out = c->ucs;
    return OK;
}

/* Room for `want' bytes in the byte buffer. The original rounds up to a whole
   kilobyte, and compares the rounded figure against what it already has. */
static int32_t byteRoom(UnicodeConverter *c, uint32_t want)
{
    uint32_t room = (want + 0x3ff) >> 10 << 10;

    if (c->mbcs != 0 && c->mbcsRoom >= room)
        return 1;
    if (c->mbcs != 0)
        cpp_delete(c->mbcs);
    c->mbcsRoom = room;
    c->mbcs = (char *)cpp_new(c->mbcsRoom);
    if (c->mbcs == 0) {
        rp_setError(c->param, ERR_MEMORY);
        return 0;
    }
    return 1;
}

/* Each character becomes the two bytes the table holds for it, appended with
   strncat so that a table entry whose high byte is nought contributes one
   byte and not two. That is how a one-byte character comes out of a table of
   pairs, and why the walk asks strlen where it has got to rather than
   counting.
 *
 * The backslash is the exception. U+005C is the yen sign in Shift-JIS, and
 * which of the two is meant is decided by the third argument: nought writes a
 * literal backslash and anything else lets the table decide as it would for
 * any other character. The manager passes its own count through to here, so
 * what arrives is a length being read as a flag; that is the original's doing
 * and it is left alone. */
int32_t uc_UCS2ToMBCS(UnicodeConverter *c, const uint16_t *in, char **out,
                      int32_t yenFlag)
{
    char *q;

    if (in == 0) {
        *out = 0;
        return OK;
    }

    if (!byteRoom(c, ucs2len(in) * 2 + 1))
        return ERR_MEMORY;
    if (c->mbcs == 0)
        return ERR_UNICODE;

    memset(c->mbcs, 0, c->mbcsRoom);

    q = c->mbcs;
    while (*in != 0) {
        if (*in < WIDE_FIRST) {
            if (*in == 0x5c && yenFlag == 0) {
                strncat(q, "\\", 2);
            } else {
                uint16_t two = at16(jajp_m_pAITable, *in);

                strncat(q, (const char *)&two, 2);
            }
            q += strlen(q);
        } else if (*in < R_FIRST) {
            *q++ = ' ';
        } else {
            uint16_t two = at16(jajp_m_pRTable, (uint32_t)*in - R_FIRST);

            strncat(q, (const char *)&two, 2);
            q += strlen(q);
        }
        in++;
    }

    *out = c->mbcs;
    return OK;
}
