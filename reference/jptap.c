/* What IBM's path search is given and what it leaves behind.
 *
 * romtap.c taps the romanizer's outermost seam, which says whether the two
 * romanizers agree on a sentence but not where they part company. This taps
 * one seam further in: JPath::Make, which is handed the character to search
 * from and leaves the lattice of dictionary entries and the paths over it in
 * two objects whose offsets are IBM's own. Both are printed in the same form
 * the trace in rom/jajp/txtanal.c prints, so the two dumps diff as they
 * stand.
 *
 * The dump goes to the file EVV_JPTAP names, and nothing is written without
 * it. `make TAG=jajp BUILD=../build/reference-jajp jptap'.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "evv_abi.h"

/* IBM's own offsets, which is what this side has. */
#define JP_PATH         0x0008
#define JP_PATH_SIZE    0x000e
#define JP_PATH_COUNT   0x7484
#define JP_SEARCH       0x7ce8

#define JPT_COUNT       0x00
#define JPT_COST        0x01
#define JPT_AT          0x02
#define JPT_END         0x0c
#define JPT_CONT        0x0d

#define DS_ENTRY        0x0008
#define DS_ENTRY_SIZE   32
#define DS_COUNT        0x80ac

#define DE_ACCENT       0x00
#define DE_KANALEN      0x02
#define DE_CHARS        0x03
#define DE_POS          0x05
#define DE_ATTR         0x06
#define DE_ATTR2        0x07
#define DE_KANA         0x08
#define DE_AT           0x12
#define DE_COST         0x1c

static FILE *tap;
static int   looked;

static FILE *tap_open(void)
{
    if (!looked) {
        const char *path = getenv("EVV_JPTAP");

        looked = 1;
        tap = path ? fopen(path, "wb") : NULL;
    }
    return tap;
}

extern THIS void jpMake_ibm(void *jp, int16_t at);

THIS void jpMake(void *jp, int16_t at)
{
    FILE *f = tap_open();

    jpMake_ibm(jp, at);
    if (f) {
        uint8_t *p  = (uint8_t *)jp;
        uint8_t *d  = *(uint8_t **)(p + JP_SEARCH);
        int16_t  ne = *(int16_t *)(d + DS_COUNT);
        int16_t  np = (int16_t)*(uint16_t *)(p + JP_PATH_COUNT);
        int16_t  i;

        fprintf(f, "entries %d\n", (int)ne);
        for (i = 0; i < ne; i++) {
            uint8_t *e = d + DS_ENTRY + (size_t)i * DS_ENTRY_SIZE;
            int      j;

            fprintf(f, "  e%d at=%d chars=%d pos=%d kanalen=%d accent=%d "
                    "attr=%02x attr2=%02x cost=%d kana=", (int)i,
                    (int)*(int16_t *)(e + DE_AT), (int)e[DE_CHARS],
                    (int)e[DE_POS], (int)e[DE_KANALEN],
                    (int)*(int16_t *)(e + DE_ACCENT), e[DE_ATTR], e[DE_ATTR2],
                    (int)*(int32_t *)(e + DE_COST));
            for (j = 0; j < e[DE_KANALEN] && j < 10; j++)
                fprintf(f, "%02x", e[DE_KANA + j]);
            fputc('\n', f);
        }
        fprintf(f, "paths %d\n", (int)np);
        for (i = 0; i < np; i++) {
            uint8_t *pt = p + JP_PATH + (size_t)i * JP_PATH_SIZE;
            int      j;

            fprintf(f, "  p%d count=%d cost=%d end=%d cont=%d at=", (int)i,
                    (int)pt[JPT_COUNT], (int)pt[JPT_COST], (int)pt[JPT_END],
                    (int)pt[JPT_CONT]);
            for (j = 0; j < pt[JPT_COUNT]; j++)
                fprintf(f, "%d,", (int)pt[JPT_AT + j]);
            fputc('\n', f);
        }
        fflush(f);
    }
}

ALIAS("?Make@JPath@@QAEXF@Z", "jpMake");

/* And the English dictionary's own lookup, which is what says how long a run
   of letters the scan above took and how many words it found for it. */
extern THIS int16_t engLookup_ibm(void *d, uint8_t *roman, int16_t slot,
                                  int16_t at, int16_t want, int32_t mark);

THIS int16_t engLookup(void *d, uint8_t *roman, int16_t slot, int16_t at,
                       int16_t want, int32_t mark)
{
    FILE   *f  = tap_open();
    int16_t rc = engLookup_ibm(d, roman, slot, at, want, mark);

    if (f) {
        int i;

        fprintf(f, "eng at=%d want=%d mark=%d wrote=%d roman=", (int)at,
                (int)want, (int)mark, (int)rc);
        for (i = 0; roman[i] && i < 40; i++)
            fprintf(f, "%02x", roman[i]);
        fputc('\n', f);
        fflush(f);
    }
    return rc;
}

ALIAS("?LookupEngWordDict@DictSearch@@QAEFPAEFFFH@Z", "engLookup");

