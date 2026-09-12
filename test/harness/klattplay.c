/* Turn a file of parameter frames into sound, without the rest of the engine.
 *
 * tools/measure/klatttap.c writes down every frame the formant engine is
 * handed, and tools/measure/compose.py builds frames for an utterance nobody
 * measured. Neither can be listened to: the tap only reads out, and the
 * composer's frames never reach a synthesiser. This does that half, driving
 * KlattSynth directly with frames from a file so a composed utterance and a
 * measured one can be heard against each other.
 *
 * The comparison is only fair if both sides come through here, so the
 * measured frames are rendered by this too rather than by the engine. That
 * also makes it checkable: this ought to reproduce the engine's own wave file
 * from the engine's own frames, and if it does not then the difference is
 * here rather than in what is being tested.
 *
 *   klattplay <frames.tsv> <out.wav> [rate] [addC addA addB addD]
 *
 * The last argument is the volume multiplier in per cent, which a rule also
 * supplies at run time. The four offsets before it are the voice's, in decibels, added to voicing,
 * aspiration and frication inside KlattSynth. A rule supplies them at run
 * time, so they cannot be known from the outside; they default to what
 * reproduces the engine's own output on the default voice.
 *
 * The file is what the tap writes: a header line, then one line a frame of
 * sixty-two tab-separated numbers in the order KlattSynth reads them.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "klatt_state.h"

#define FRAME_WORDS 62
#define MAX_SAMPLES (60 * 44100)

static int16_t out[MAX_SAMPLES];
static long nout;
static long clipped;

/* The synthesiser hands back thirty-two bit samples and the wave file wants
   sixteen, which is what the engine's own output path does with them too. */
static int collect(void *user, KlattSamplesStruct *s)
{
    int32_t i;

    (void)user;
    for (i = 0; i < s->count; i++) {
        int32_t v = s->samples[i];

        if (nout >= MAX_SAMPLES)
            return 0;
        if (v > 32767) {
            v = 32767;
            clipped++;
        } else if (v < -32768) {
            v = -32768;
            clipped++;
        }
        out[nout++] = (int16_t)v;
    }
    return 1;
}

static void put32(FILE *f, unsigned long v)
{
    fputc((int)(v & 0xff), f);
    fputc((int)((v >> 8) & 0xff), f);
    fputc((int)((v >> 16) & 0xff), f);
    fputc((int)((v >> 24) & 0xff), f);
}

static void put16(FILE *f, unsigned v)
{
    fputc((int)(v & 0xff), f);
    fputc((int)((v >> 8) & 0xff), f);
}

static int write_wav(const char *path, unsigned long rate)
{
    FILE *f = fopen(path, "wb");

    if (!f)
        return 0;
    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + (unsigned long)nout * 2);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    put32(f, 16);
    put16(f, 1);
    put16(f, 1);
    put32(f, rate);
    put32(f, rate * 2);
    put16(f, 2);
    put16(f, 16);
    fwrite("data", 1, 4, f);
    put32(f, (unsigned long)nout * 2);
    fwrite(out, 2, (size_t)nout, f);
    fclose(f);
    return 1;
}

int main(int argc, char **argv)
{
    KlattConstParms cp;
    void *h;
    FILE *f;
    char line[4096];
    long frames = 0;
    int32_t rate;

    if (argc < 3) {
        fprintf(stderr, "usage: klattplay <frames.tsv> <out.wav> [rate]\n");
        return 2;
    }
    rate = (argc > 3) ? (int32_t)strtol(argv[3], NULL, 10) : 11025;
    if (rate != 8000 && rate != 11025) {
        /* Any other rate wants tables built for it, which is the engine's
           job and not this one's. */
        fprintf(stderr, "klattplay: only 8000 and 11025 without the engine\n");
        return 2;
    }

    f = fopen(argv[1], "r");
    if (!f) {
        fprintf(stderr, "klattplay: cannot read %s\n", argv[1]);
        return 1;
    }

    h = klatt_new(NULL);
    if (!h) {
        fprintf(stderr, "klattplay: out of memory\n");
        return 1;
    }

    /* Exactly what makeSound builds, less the four voice offsets, which a
       rule supplies at run time and which are decibels added to voicing,
       aspiration and frication. */
    memset(&cp, 0, sizeof cp);
    if (argc > 7) {
        cp.unknown_2c = (int32_t)strtol(argv[4], NULL, 10);   /* addC */
        cp.unknown_24 = (int32_t)strtol(argv[5], NULL, 10);   /* addA */
        cp.unknown_28 = (int32_t)strtol(argv[6], NULL, 10);   /* addB */
        cp.unknown_20 = (int32_t)strtol(argv[7], NULL, 10);   /* addD */
    }
    cp.unknown_00 = 100;
    cp.sample_rate = rate;
    cp.unknown_08 = 16;
    cp.n_formants = 5;
    cp.unknown_10 = 8;
    cp.unknown_14 = 1;
    cp.error_fn = (klatt_error_fn)errorKlattIgnore;
    cp.callback_mode = 2;
    cp.samples_fn = collect;
    KlattSetConstParms(h, cp);

    /* Nought from the calloc, and every sample is multiplied by it, so
       leaving this out gives the right number of perfectly silent samples. */
    klattSetVolumeMultiplier(h, (argc > 8) ?
                             (int32_t)strtol(argv[8], NULL, 10) : 100);

    if (!KlattOpen(h)) {
        fprintf(stderr, "klattplay: KlattOpen failed\n");
        return 1;
    }

    while (fgets(line, sizeof line, f)) {
        int32_t parms[FRAME_WORDS + 1];
        char *p = line;
        int i;

        if (line[0] == '#' || strncmp(line, "step", 4) == 0)
            continue;
        for (i = 0; i < FRAME_WORDS; i++) {
            char *end;
            long v = strtol(p, &end, 10);

            if (end == p)
                break;
            parms[i] = (int32_t)v;
            p = end;
            while (*p == '\t' || *p == ' ')
                p++;
        }
        if (i < FRAME_WORDS)
            continue;
        parms[FRAME_WORDS] = 0;          /* P_MS, which the tap does not write */
        if (!KlattSynth(h, parms)) {
            fprintf(stderr, "klattplay: KlattSynth refused frame %ld\n",
                    frames);
            return 1;
        }
        frames++;
    }
    fclose(f);

    if (!write_wav(argv[2], (unsigned long)rate)) {
        fprintf(stderr, "klattplay: cannot write %s\n", argv[2]);
        return 1;
    }
    printf("klattplay: %ld frames, %ld samples", frames, nout);
    if (clipped)
        printf(", %ld clipped", clipped);
    printf("\n");
    klatt_delete(h);
    return 0;
}
