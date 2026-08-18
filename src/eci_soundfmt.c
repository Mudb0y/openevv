/* Which ways of writing sound the engine knows about.
 *
 * A format registers itself once at start-up and lives in a fixed table of
 * twenty. There is no unregistering and no ordering: the first one in is the
 * default, and lookup by name is a walk. That is enough because the list is
 * built once from a fixed set and never changes afterwards.
 *
 * A format is a block whose first word is its name and whose later words are
 * the things it can do. Only the two this file uses are named.
 *
 * The rest of the file is the fallbacks a format may point at for the parts
 * it does not care about: say nothing happened, say it worked, say there is
 * nothing outstanding. Two of them are real, because remembering an index
 * callback and calling it back is the same for every format.
 *
 * Names carry no prefix: the object uses plain C names and so do we.
 */

#include <stdint.h>
#include "eci_synththread.h"

#define MAX_FORMATS 20

/* A format, as far as anything here is concerned. */
#define FMT_NAME(f)   (*(const char *const *)(f))
#define FMT_MATCH(f)  (*(FmtMatchFn *)((char *)(f) + 0x14))

typedef int32_t (*FmtMatchFn)(void *fmt, void *file);

/* Where a sound file keeps the index callback and what to hand back with it.
   The same two words for every format, which is why these can be shared. */
#define FILE_INDEX_FN(s)    (*(FileIndexFn *)((char *)(s) + 0x20))
#define FILE_INDEX_PARAM(s) (*(void **)((char *)(s) + 0x24))

typedef void (*FileIndexFn)(int32_t index, void *param);

extern int ralStrIcmp(int n, const char *a, const char *b);

static void    *registeredFormats[MAX_FORMATS];
static int32_t  numFormats;

/* Room for twenty and no more; a twenty-first is refused rather than
   replacing anything. */
int32_t registerSoundFormat(void *fmt)
{
    if (numFormats >= MAX_FORMATS)
        return 0;

    registeredFormats[numFormats] = fmt;
    numFormats++;
    return 1;
}

/* By name, without regard to case. */
void *soundFormatNamed(const char *name)
{
    int32_t i;

    for (i = 0; i < numFormats; i++) {
        if (ralStrIcmp(0, name, FMT_NAME(registeredFormats[i])) == 0)
            return registeredFormats[i];
    }
    return 0;
}

/* Whichever registered first. */
void *defaultSoundFormat(void)
{
    return numFormats > 0 ? registeredFormats[0] : 0;
}

/* The first format that says it will have this file. A file with nothing at
   0x18 is not offered to anybody. */
void *findMatchingSoundFormat(void *file)
{
    int32_t i;

    if (*(int32_t *)((char *)file + 0x18) == 0)
        return 0;

    for (i = 0; i < numFormats; i++) {
        if (FMT_MATCH(registeredFormats[i])(registeredFormats[i], file) == 1)
            return registeredFormats[i];
    }
    return 0;
}

/* ---- what a format may fall back on --------------------------------- */

void defaultSetDuration(void)
{
}

int32_t defaultGetDuration(void)
{
    return -1;
}

int32_t defaultHold(void)
{
    return 1;
}

int32_t defaultReset(void)
{
    return 1;
}

int32_t defaultSamplesUncommitted(void)
{
    return 0;
}

int32_t defaultPoll(void)
{
    return 1;
}

void defaultSetIndexCallback(void *file, FileIndexFn fn, void *param)
{
    FILE_INDEX_FN(file) = fn;
    FILE_INDEX_PARAM(file) = param;
}

/* Reporting an index is calling straight back, if anybody asked. It says it
   worked whether or not anybody did. */
int32_t defaultInsertIndex(void *file, int32_t index)
{
    if (FILE_INDEX_FN(file) != 0)
        FILE_INDEX_FN(file)(index, FILE_INDEX_PARAM(file));
    return 1;
}
