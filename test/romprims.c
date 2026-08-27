/* The romanizer's converters, ours against IBM's, one call at a time.
 *
 * test/romcan.sh cannot reach these. It proves the engine below the romanizer
 * by replaying what IBM's romanizer answered, and a class the romanizer reaches
 * for itself is never called on that path at all -- the codeset conversion is
 * exactly that. So this is the same arrangement test/prims.c uses for the
 * machine's primitives: one file compiled twice, once against our romanizer and
 * once against IBM's own objects, both printing the same lines for the same
 * sweep, and test/romprims.sh diffing them.
 *
 * What is swept is every input there is. Shift-JIS to UCS-2 takes every single
 * byte and every two-byte pair the converter accepts, which is about eleven and
 * a half thousand; UCS-2 to Shift-JIS takes all sixty-five thousand code
 * points, twice, because the backslash is treated differently depending on the
 * third argument. Nothing is sampled and nothing is left to a case somebody
 * thought of.
 *
 * Four bytes are left out of the first sweep and the reason is not squeamish.
 * 0x80, 0xfe and 0xff reach the end of IBM's chain of tests without its walk
 * advancing over them, so its converter loops on the same byte for ever;
 * sweeping them would hang IBM's side rather than say anything. And a pair
 * beginning 0xfd is one IBM's converter accepts and its table does not hold,
 * so it answers with whatever the linker put after that table -- which is what
 * this sweep found, and it is not an answer that means anything. Both are
 * deliberate differences and both are written down in
 * rom/jajp/unicodeconvt.c.
 *
 * usage: romprims
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "evv_abi.h"

#ifdef EVV_ROMPRIMS_OURS
#include "jprom.h"
#endif

void evvRunStaticInitialisers(void);
#ifdef EVV_ROMPRIMS_OURS
void evv_port_start(void);
void evv_port_finish(void);
#endif

/* How the two sides are reached. Ours are C functions over structs of our own;
   IBM's are C++ methods over objects of a size only its own code knows, so
   the room is stated here and the constructors are called by their mangled
   names. */
#ifdef EVV_ROMPRIMS_OURS

typedef RomInstParam Param;
typedef UnicodeConverter Conv;

static Param param_room;
static Conv  conv_room;

static Param *makeParam(const char *path)
{
    return rp_ctor(&param_room, path);
}

static Conv *makeConv(Param *p)
{
    return uc_ctor(&conv_room, p);
}

#define TO_UCS2(c, in, out)      uc_MBCSToUCS2((c), (in), (out))
#define TO_MBCS(c, in, out, f)   uc_UCS2ToMBCS((c), (in), (out), (f))

#else

typedef struct Param Param;
typedef struct Conv Conv;

/* What IBM's two objects take up: 0x3c bytes for the parameter block and
   0x14 for the converter, which is what their own constructors are handed. */
static char param_room[0x3c];
static char conv_room[0x14];

extern THIS Param *ibm_paramCtor(void *self, const char *path)
    MANGLED("??0RomInstParam@@QAE@PBD@Z");
extern THIS Conv *ibm_convCtor(void *self, Param *p)
    MANGLED("??0UnicodeConverter@@QAE@PAVRomInstParam@@@Z");
extern THIS int32_t ibm_toUcs2(Conv *c, const char *in, uint16_t **out)
    MANGLED("?MBCSToUCS2@UnicodeConverter@@QAE?AW4RomError@@PBDPAPAG@Z");
extern THIS int32_t ibm_toMbcs(Conv *c, const uint16_t *in, char **out,
                               int32_t flag)
    MANGLED("?UCS2ToMBCS@UnicodeConverter@@QAE?AW4RomError@@PBGPAPADH@Z");

static Param *makeParam(const char *path)
{
    return ibm_paramCtor(param_room, path);
}

static Conv *makeConv(Param *p)
{
    return ibm_convCtor(conv_room, p);
}

#define TO_UCS2(c, in, out)      ibm_toUcs2((c), (in), (out))
#define TO_MBCS(c, in, out, f)   ibm_toMbcs((c), (in), (out), (f))

#endif

/* ---- printing ------------------------------------------------------- */

static void putBytes(const char *s)
{
    if (s == NULL) {
        fputs("-", stdout);
        return;
    }
    if (*s == 0) {
        fputs(".", stdout);
        return;
    }
    for (; *s; s++)
        printf("%02x", (unsigned char)*s);
}

static void putWide(const uint16_t *s)
{
    if (s == NULL) {
        fputs("-", stdout);
        return;
    }
    if (*s == 0) {
        fputs(".", stdout);
        return;
    }
    for (; *s; s++)
        printf("%04x", (unsigned)*s);
}

/* ---- the sweeps ----------------------------------------------------- */

/* Which lead bytes take a second byte, and which single bytes stand alone.
   The trailing byte range is the whole of what Shift-JIS uses, and the
   converter is offered all of it whether the pair means anything or not:
   what a table holds for a pair nobody writes still has to be the same on
   both sides. */
static void sweepToUcs2(Conv *c)
{
    static char in[3];
    uint16_t *out;
    int lead, trail, one;

    for (one = 0x01; one <= 0x7f; one++) {
        in[0] = (char)one;
        in[1] = 0;
        out = NULL;
        printf("MBCS %02x -> %d ", one, (int)TO_UCS2(c, in, &out));
        putWide(out);
        putchar('\n');
    }

    for (one = 0xa0; one <= 0xdf; one++) {
        in[0] = (char)one;
        in[1] = 0;
        out = NULL;
        printf("MBCS %02x -> %d ", one, (int)TO_UCS2(c, in, &out));
        putWide(out);
        putchar('\n');
    }

    for (lead = 0x81; lead <= 0xfc; lead++) {
        if (lead > 0x9f && lead < 0xe0)
            continue;
        for (trail = 0x40; trail <= 0xfc; trail++) {
            in[0] = (char)lead;
            in[1] = (char)trail;
            in[2] = 0;
            out = NULL;
            printf("MBCS %02x%02x -> %d ", lead, trail,
                   (int)TO_UCS2(c, in, &out));
            putWide(out);
            putchar('\n');
        }
    }
}

/* Every code point, with the flag off and on. Nought is the terminator and
   cannot be offered. */
static void sweepToMbcs(Conv *c)
{
    static uint16_t in[2];
    char *out;
    long u;
    int flag;

    for (flag = 0; flag <= 1; flag++)
        for (u = 1; u <= 0xffff; u++) {
            in[0] = (uint16_t)u;
            in[1] = 0;
            out = NULL;
            printf("UCS2 %04lx %d -> %d ", u, flag,
                   (int)TO_MBCS(c, in, &out, flag));
            putBytes(out);
            putchar('\n');
        }
}

/* A string rather than one character at a time, so that the walk and the
   growing of the buffers are exercised as well as the tables. */
static void sweepStrings(Conv *c)
{
    static const char *const mbcs[] = {
        "", "a", "abc", "\x82\xb1\x82\xf1\x82\xc9\x82\xbf\x82\xcd",
        "\x93\xfa\x96\x7b\x8c\xea", "A\x82\xa0" "1\x83\x41",
        "\xb1\xb2\xb3", "\x82\xa0\xb1" "z",
        NULL
    };
    static const uint16_t wide1[] = { 0x3053, 0x3093, 0 };
    static const uint16_t wide2[] = { 'a', 0x5c, 'b', 0xa000, 0xe000, 0 };
    static const uint16_t wide3[] = { 0xff21, 0x30a2, 0x4e00, 0 };
    const uint16_t *const wides[] = { wide1, wide2, wide3, NULL };
    int i;

    for (i = 0; mbcs[i] != NULL; i++) {
        uint16_t *out = NULL;

        printf("STR ");
        putBytes(mbcs[i]);
        printf(" -> %d ", (int)TO_UCS2(c, mbcs[i], &out));
        putWide(out);
        putchar('\n');
    }

    for (i = 0; wides[i] != NULL; i++) {
        char *out = NULL;

        printf("WSTR ");
        putWide(wides[i]);
        printf(" -> %d ", (int)TO_MBCS(c, wides[i], &out, 1));
        putBytes(out);
        putchar('\n');
    }

    /* And a long one, to make the byte buffer grow past its first kilobyte
       and be freed and made again. */
    {
        static uint16_t big[2000];
        char *out = NULL;
        int k;

        for (k = 0; k < 1999; k++)
            big[k] = (uint16_t)(0x3041 + (k % 80));
        big[1999] = 0;
        printf("LONG -> %d ", (int)TO_MBCS(c, big, &out, 1));
        printf("%d bytes\n", out ? (int)strlen(out) : -1);
    }
}

int main(void)
{
    Param *p;
    Conv  *c;

    setvbuf(stdout, NULL, _IOFBF, 1 << 16);
#ifdef EVV_ROMPRIMS_OURS
    evv_port_start();
#endif
    evvRunStaticInitialisers();

    p = makeParam("");
    if (p == NULL) {
        printf("romprims: no parameter block\n");
        return 1;
    }
    c = makeConv(p);
    if (c == NULL) {
        printf("romprims: no converter\n");
        return 1;
    }

    sweepToUcs2(c);
    sweepToMbcs(c);
    sweepStrings(c);

    fflush(stdout);
#ifdef EVV_ROMPRIMS_OURS
    evv_port_finish();
#endif
    return 0;
}
