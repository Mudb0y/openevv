/* What the synthesiser was told, frame by frame.
 *
 * KlattSynth takes one frame of sixty-two parameters and answers a run of
 * samples, and it is the only way anything reaches the formant engine. So
 * everything that decides how this engine sounds passes through here, and
 * writing each frame down is IBM's own parameter data observed at the point
 * of use -- not a table read out of a module, and not a measurement by ear.
 *
 * The names are the engine's own, from parmNames in src/klatt/klatt_run.c:
 * the first word is the step in milliseconds and the last is the duration
 * slot, with f0, the eight formants and their bandwidths, the nasal and
 * tracheal poles and zeros, the voice quality four, and the frication and
 * voicing amplitudes between them.
 *
 * Off unless EVV_KLATT_TAP names a file to write. One line a frame, the
 * parameters in frame order, so a run over a corpus gives every value this
 * engine ever asks the synthesiser for.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "evv_klatttap.h"

#define TAP_PARMS 62

static FILE *tap;
static int   tried;
static long  frames;

/* In frame order, which is the order KlattSynth reads them. */
const char *const evv_klatt_parm_names[TAP_PARMS] = {
    "step", "f0",  "av",  "oq",  "tl",  "fl",  "di",  "ah",
    "af",   "f1",  "b1",  "df1", "db1", "f2",  "b2",  "f3",
    "b3",   "f4",  "b4",  "f5",  "b5",  "f6",  "b6",  "f7",
    "b7",   "f8",  "b8",  "fnp", "bnp", "fnz", "bnz", "ftp",
    "btp",  "ftz", "btz", "a1f", "a2f", "a3f", "a4f", "a5f",
    "a6f",  "a7f", "a8f", "ab",  "b1f", "b2f", "b3f", "b4f",
    "b5f",  "b6f", "b7f", "b8f", "anv", "a1v", "a2v", "a3v",
    "a4v",  "a5v", "a6v", "a7v", "a8v", "atv"
};

static void tap_close(void)
{
    if (tap != 0) {
        fprintf(stderr, "klatttap: %ld frames\n", frames);
        fclose(tap);
        tap = 0;
    }
}

static int tap_open(void)
{
    const char *where;
    int i;

    if (tried)
        return tap != 0;
    tried = 1;

    where = getenv("EVV_KLATT_TAP");
    if (where == 0 || *where == 0)
        return 0;

    /* Appending, because the harnesses speak one case a process and the
       whole point is a corpus rather than a sentence. */
    tap = fopen(where, "a");
    if (tap == 0)
        return 0;
    atexit(tap_close);

    /* A header only where the file was empty, so an appended run does not
       repeat it. */
    if (ftell(tap) == 0) {
        for (i = 0; i < TAP_PARMS; i++)
            fprintf(tap, "%s%s", i ? "\t" : "", evv_klatt_parm_names[i]);
        fputc('\n', tap);
    }
    return 1;
}

void evv_klatt_tap(const int32_t *parms)
{
    int i;

    if (!tap_open())
        return;

    for (i = 0; i < TAP_PARMS; i++)
        fprintf(tap, "%s%d", i ? "\t" : "", (int)parms[i]);
    fputc('\n', tap);
    frames++;
}
