/* What IBM's romanizer manager is handed and what it hands back.
 *
 * The romanizer is a text-to-text stage. Everything it produces reaches the
 * engine as a byte string through processSentence and processRemaining, and
 * everything it is given arrives through addText, addParam, insertIndex,
 * setParam, stop and resume. Those eight are RomanizerManager's own public
 * methods, called from synthtext.obj, synthrun.obj and synthwork.obj --
 * across an object boundary, so the rename trick reaches them. See the head of tap.c for how that works
 * and why it does not work on a caller sitting in the same object.
 *
 * What this is for. A Japanese romanizer is a hundred and sixty thousand bytes
 * of x86 to transcribe, and audio comparison says only that two runs differ,
 * not where. This turns the whole of it into a function with an exact oracle:
 * for a given input, these are the bytes IBM's romanizer produces. The same
 * dump replayed by test/romcan.c is a romanizer for our engine that has no
 * Japanese in it at all, which is what proves that everything downstream of
 * this seam is already right before a line of the romanizer is written.
 *
 * The dump goes to the file EVV_ROMTAP names, and nothing is written without
 * it, so the tapped binary stands in for the plain one. Check that it does:
 * the samples must be identical with the variable unset.
 *
 * Text is hex because Shift-JIS carries bytes no line-oriented format keeps.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "evv_abi.h"

static FILE *tap;
static int   looked;

static FILE *tap_open(void)
{
    if (!looked) {
        const char *path = getenv("EVV_ROMTAP");

        looked = 1;
        /* Binary, so the lines carry the same newline our own side writes
           and the two dumps can be diffed as they stand. */
        tap = path ? fopen(path, "wb") : NULL;
    }
    return tap;
}

/* One string, as hex, with a nought-length one written as nothing at all.
   A length of -1 means the string says where it ends itself. */
static void hex(FILE *f, const char *s, int32_t n)
{
    int32_t i;

    if (s == NULL) {
        fputs("-", f);
        return;
    }
    if (n < 0)
        for (n = 0; s[n]; n++)
            ;
    if (n == 0) {
        fputs(".", f);
        return;
    }
    for (i = 0; i < n; i++)
        fprintf(f, "%02x", (unsigned char)s[i]);
}

extern THIS int32_t rmAddText_ibm(void *m, const char *s, int32_t n,
                                  int32_t flag);
extern THIS int32_t rmAddParam_ibm(void *m, const char *s, int32_t n);
extern THIS int32_t rmInsertIndex_ibm(void *m);
extern THIS int32_t rmProcessSentence_ibm(void *m, char **out,
                                          int32_t annotated);
extern THIS int32_t rmProcessRemaining_ibm(void *m, char **out);
extern THIS int32_t rmSetParam_ibm(void *m, int32_t which, int32_t value);
extern THIS int32_t rmStop_ibm(void *m);
extern THIS int32_t rmResume_ibm(void *m);

THIS int32_t rmAddText(void *m, const char *s, int32_t n, int32_t flag)
{
    FILE   *f = tap_open();
    int32_t rc = rmAddText_ibm(m, s, n, flag);

    if (f) {
        fprintf(f, "ADDTEXT len=%d flag=%d rc=%d text=", (int)n, (int)flag,
                (int)rc);
        hex(f, s, n);
        fputc('\n', f);
        fflush(f);
    }
    return rc;
}

THIS int32_t rmAddParam(void *m, const char *s, int32_t n)
{
    FILE   *f = tap_open();
    int32_t rc = rmAddParam_ibm(m, s, n);

    if (f) {
        fprintf(f, "ADDPARAM len=%d rc=%d text=", (int)n, (int)rc);
        hex(f, s, n);
        fputc('\n', f);
        fflush(f);
    }
    return rc;
}

THIS int32_t rmInsertIndex(void *m)
{
    FILE   *f = tap_open();
    int32_t rc = rmInsertIndex_ibm(m);

    if (f) {
        fprintf(f, "INDEX rc=%d\n", (int)rc);
        fflush(f);
    }
    return rc;
}

THIS int32_t rmProcessSentence(void *m, char **out, int32_t annotated)
{
    FILE   *f = tap_open();
    int32_t rc = rmProcessSentence_ibm(m, out, annotated);

    if (f) {
        fprintf(f, "PROCESS anno=%d rc=%d out=", (int)annotated, (int)rc);
        hex(f, out ? *out : NULL, rc > 0 ? rc : (out && *out ? -1 : 0));
        fputc('\n', f);
        fflush(f);
    }
    return rc;
}

THIS int32_t rmProcessRemaining(void *m, char **out)
{
    FILE   *f = tap_open();
    int32_t rc = rmProcessRemaining_ibm(m, out);

    if (f) {
        fprintf(f, "REMAIN rc=%d out=", (int)rc);
        hex(f, out ? *out : NULL, rc > 0 ? rc : (out && *out ? -1 : 0));
        fputc('\n', f);
        fflush(f);
    }
    return rc;
}

THIS int32_t rmSetParam(void *m, int32_t which, int32_t value)
{
    FILE   *f = tap_open();
    int32_t rc = rmSetParam_ibm(m, which, value);

    if (f) {
        fprintf(f, "SETPARAM which=%d value=%d rc=%d\n", (int)which,
                (int)value, (int)rc);
        fflush(f);
    }
    return rc;
}

THIS int32_t rmStop(void *m)
{
    FILE   *f = tap_open();
    int32_t rc = rmStop_ibm(m);

    if (f) {
        fprintf(f, "STOP rc=%d\n", (int)rc);
        fflush(f);
    }
    return rc;
}

THIS int32_t rmResume(void *m)
{
    FILE   *f = tap_open();
    int32_t rc = rmResume_ibm(m);

    if (f) {
        fprintf(f, "RESUME rc=%d\n", (int)rc);
        fflush(f);
    }
    return rc;
}

ALIAS("?addText@RomanizerManager@@QAEHPBDHH@Z", "rmAddText");
ALIAS("?addParam@RomanizerManager@@QAEHPBDH@Z", "rmAddParam");
ALIAS("?insertIndex@RomanizerManager@@QAEHXZ", "rmInsertIndex");
ALIAS("?processSentence@RomanizerManager@@QAEHPAPADH@Z", "rmProcessSentence");
ALIAS("?processRemaining@RomanizerManager@@QAEHPAPAD@Z", "rmProcessRemaining");
ALIAS("?setParam@RomanizerManager@@QAEHJH@Z", "rmSetParam");
ALIAS("?stop@RomanizerManager@@QAEHXZ", "rmStop");
ALIAS("?resume@RomanizerManager@@QAEHXZ", "rmResume");
