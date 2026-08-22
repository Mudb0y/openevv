/* Taps on IBM's own binary, so that what it computes can be set against what
 * we compute at the same point rather than only at the end.
 *
 * The comparison tests hold the two engines' samples against each other, which
 * says whether they agree and nothing about where they stopped agreeing. This
 * says where. Each tap below stands in front of one of IBM's functions, writes
 * down what it was handed, and calls the real one; the audio is unchanged, so a
 * tapped run can be checked against an untapped one before its dump is
 * believed.
 *
 * How the standing-in works. The link is told to rename IBM's definition --
 * `objcopy --redefine-sym _foo=_foo_ibm' -- and the wrapper here takes the
 * plain name, so every caller reaches this first. That only works where the
 * call crosses from one object into another: a rename applies to the object's
 * own relocations too, so a caller in the same object follows it and never sees
 * the wrapper. Every function tapped here is called from an object other than
 * the one defining it, which is why these four and not the ones next to them.
 * `synthesize' is the obvious one to want and cannot be had: runklatt.obj calls
 * it itself.
 *
 * Nothing is written unless the matching variable names a file, so the tapped
 * binary can stand in for the plain one.
 *
 *   EVV_TAP_SYNTH    callSynthesizeArray -- the cells and the frame overrides
 *                    a rule hands the synthesiser
 *   EVV_TAP_KLATT    KlattSynth -- the sixty-two parameters of every frame,
 *                    the last thing before sound
 *   EVV_TAP_STREAM   addStreamArrayPtValC and addStreamArraySsValC -- every
 *                    point a rule writes into a stream array
 *
 * The other side of each tap is a few lines in our own C at the same function,
 * printing the same line to the same variable. They are not kept in the tree:
 * a diagnostic that is always compiled in is a diagnostic nobody checks, and
 * the point of the exercise is to put one in for an afternoon and take it out
 * again. docs/building.md says what the lines look like.
 *
 * This found the one thing that is wrong with German: of the hundred and
 * thirty-one points the language writes into its streams for one word, a
 * hundred and thirty are ours to the byte and one is not.
 */

#include <stdio.h>
#include <stdlib.h>

/* One of the engine's value cells, as klatt_run.c has it: a word at two and a
   long at four. */
typedef struct Cell { short pad; short w; int l; } Cell;

/* How many stack words past the last named argument a variadic call is
   followed with. Cdecl leaves them all in a row and the caller cleans up, so
   passing more than the callee reads costs nothing and forwarding is a matter
   of copying enough of them. */
#define SPARE 96

/* How many parameters a synthesiser frame carries. */
#define FRAME_WORDS 62

static FILE *tap_open(const char *var, FILE **slot)
{
    if (*slot == NULL) {
        const char *path = getenv(var);

        *slot = path ? fopen(path, "w") : NULL;
    }
    return *slot;
}

/* ---- callSynthesizeArray -------------------------------------------- */

/* Thirteen cells and then a run of pairs, an index and a value, ending at an
   index of nought. Each of those is the address of a cell rather than a value,
   because a rule pushes it as one. */

extern int callSynthesizeArray_ibm();

int callSynthesizeArray(void *d, Cell *rate, Cell *c2, Cell *c3, Cell *c4,
                        Cell *c5, Cell *c6, Cell *c7, Cell *c8, Cell *c9,
                        Cell *c10, Cell *c11, Cell *c12, Cell *c13, ...)
{
    static FILE *out;
    int *tail = (int *)((char *)&c13 + sizeof c13);
    int spare[SPARE];
    Cell *cells[13];
    FILE *f = tap_open("EVV_TAP_SYNTH", &out);
    int i;

    if (f != NULL) {
        cells[0] = rate; cells[1] = c2;   cells[2] = c3;   cells[3] = c4;
        cells[4] = c5;   cells[5] = c6;   cells[6] = c7;   cells[7] = c8;
        cells[8] = c9;   cells[9] = c10;  cells[10] = c11; cells[11] = c12;
        cells[12] = c13;

        fputs("CALL cells", f);
        for (i = 0; i < 13; i++)
            fprintf(f, " %d/%d", cells[i] ? cells[i]->w : 0,
                    cells[i] ? cells[i]->l : 0);
        fputc('\n', f);

        fputs("PAIRS", f);
        {
            int k = 0;
            int idx = ((Cell *)(long)tail[k++])->w;

            while (idx != 0 && k < SPARE - 2) {
                int val;

                idx--;
                if (idx < 0)
                    break;
                val = ((Cell *)(long)tail[k++])->w;
                fprintf(f, " %d=%d", idx, val);
                idx = ((Cell *)(long)tail[k++])->w;
            }
        }
        fputc('\n', f);
        fflush(f);
    }

    for (i = 0; i < SPARE; i++)
        spare[i] = tail[i];

    return callSynthesizeArray_ibm(d, rate, c2, c3, c4, c5, c6, c7, c8, c9,
        c10, c11, c12, c13,
        spare[0], spare[1], spare[2], spare[3], spare[4], spare[5],
        spare[6], spare[7], spare[8], spare[9], spare[10], spare[11],
        spare[12], spare[13], spare[14], spare[15], spare[16], spare[17],
        spare[18], spare[19], spare[20], spare[21], spare[22], spare[23],
        spare[24], spare[25], spare[26], spare[27], spare[28], spare[29],
        spare[30], spare[31], spare[32], spare[33], spare[34], spare[35],
        spare[36], spare[37], spare[38], spare[39], spare[40], spare[41],
        spare[42], spare[43], spare[44], spare[45], spare[46], spare[47],
        spare[48], spare[49], spare[50], spare[51], spare[52], spare[53],
        spare[54], spare[55], spare[56], spare[57], spare[58], spare[59],
        spare[60], spare[61], spare[62], spare[63], spare[64], spare[65],
        spare[66], spare[67], spare[68], spare[69], spare[70], spare[71],
        spare[72], spare[73], spare[74], spare[75], spare[76], spare[77],
        spare[78], spare[79], spare[80], spare[81], spare[82], spare[83],
        spare[84], spare[85], spare[86], spare[87], spare[88], spare[89],
        spare[90], spare[91], spare[92], spare[93], spare[94], spare[95]);
}

/* ---- KlattSynth ------------------------------------------------------ */

/* One parameter frame in, a run of samples out. clsyn.obj defines it and
   arraygen.obj and stmgen.obj call it, and both of those are byte for byte the
   same object in every language module, so a frame crossing here in the German
   build is directly comparable with the same frame in ours. */

extern int KlattSynth_ibm(void *handle, const int *parms);

int KlattSynth(void *handle, const int *parms)
{
    static FILE *out;
    static long seq;
    FILE *f = tap_open("EVV_TAP_KLATT", &out);

    if (f != NULL) {
        int i;

        fprintf(f, "F%ld", seq++);
        for (i = 0; i < FRAME_WORDS; i++)
            fprintf(f, " %d", parms[i]);
        fputc('\n', f);
        fflush(f);
    }

    return KlattSynth_ibm(handle, parms);
}

/* ---- the stream arrays ----------------------------------------------- */

/* Where a rule puts a value into a stream. The names of the two arguments are
   the other way round from what they carry, which is the original's doing:
   what is called the moment is the value and what is called the value is the
   moment. The lines below say val and t so as not to repeat the confusion. */

extern int addStreamArrayPtValC_ibm(void *d, const short *stream,
                                    const short *when, const int *value);
extern int addStreamArraySsValC_ibm(void *d, const short *stream,
                                    const short *when, const int *first,
                                    const int *second);

static FILE *stream_out;

int addStreamArrayPtValC(void *d, const short *stream, const short *when,
                         const int *value)
{
    FILE *f = tap_open("EVV_TAP_STREAM", &stream_out);

    if (f != NULL) {
        fprintf(f, "PT stream=%d val=%d t=%d\n",
                stream[1], when[1], value[1]);
        fflush(f);
    }

    return addStreamArrayPtValC_ibm(d, stream, when, value);
}

int addStreamArraySsValC(void *d, const short *stream, const short *when,
                         const int *first, const int *second)
{
    FILE *f = tap_open("EVV_TAP_STREAM", &stream_out);

    if (f != NULL) {
        fprintf(f, "SS stream=%d val=%d t1=%d t2=%d\n",
                stream[1], when[1], first[1], second[1]);
        fflush(f);
    }

    return addStreamArraySsValC_ibm(d, stream, when, first, second);
}
