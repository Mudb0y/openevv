/* A sound file, which is usually not a file.

   One record stands for wherever finished samples are going: a file on disk,
   or a device, told apart only by the name. Everything it can actually do is
   deferred to a format object looked up from that name, so this file is
   almost entirely a set of front doors -- check we are not already open,
   remember the value, and let the format decide whether it will have it.

   The one field that matters is the status. While it is anything but nought
   the record is in use and every setter below refuses, which is how the
   engine is stopped from changing the rate underneath a device that is
   already playing.

   Names carry no prefix: the object uses plain C names throughout, so ours
   are the same and the swap stands the original's aside on that alone. */

#include <stdint.h>
#include <stdio.h>
#include "eci_synththread.h"
#include "evv_abi.h"

/* Where the samples are going. Only what this file touches is named. */
typedef struct SoundFile {
    char     *name;             /* +0x00 */
    int32_t   mode;             /* +0x04 */
    int32_t   device;           /* +0x08 */
    void     *format;           /* +0x0c */
    int32_t   rate;             /* +0x10 */
    int32_t   width;            /* +0x14 */
    FILE     *file;             /* +0x18 */
    int32_t   status;           /* +0x1c, nought means closed */
    int32_t   unknown_20;
    int32_t   unknown_24;
    int32_t   unknown_28;
    int32_t   numDeviceBlocks;      /* +0x2c */
    int32_t   sizeDeviceBlocks;     /* +0x30 */
    int32_t   numPrerollBlocks;     /* +0x34 */
    int32_t   sizePrerollBlocks;    /* +0x38 */
} SoundFile;

/* What the status can be. */
#define SF_CLOSED   0
#define SF_OUTPUT   2
#define SF_APPEND   3

/* What a format can do, by slot in its own table. */
#define FMT_USES_FILE(f)  (*(int32_t *)((char *)(f) + 0x04))
#define FMT_SLOT(f, off)  ((*(void ***)(f))[(off) / 4])
#define FMT_CONFIGURE     0x10
#define FMT_CREATE_OUTPUT 0x14
#define FMT_CLOSE         0x18
#define FMT_FINISH        0x28
#define FMT_SET_INDEX_CB  0x44
#define FMT_POLL          0x4c

typedef int  (*FmtOneFn)(SoundFile *sf);
typedef int  (*FmtTwoFn)(void *fmt, SoundFile *sf);
typedef void (*FmtIndexFn)(SoundFile *sf, void *cb, void *param);

extern void *soundFormatNamed(const char *ext) MANGLED("_soundFormatNamed");
extern void *defaultSoundFormat(void) MANGLED("_defaultSoundFormat");

/* How long an extension this will carry, and where it is kept. There is one
   buffer, so the answer only lasts until the next call -- which is all any
   caller here needs, since every one of them passes it straight on. */
#define EXTENSION_ROOM 0x100

int soundFileFormat(SoundFile *sf, const char *ext, int32_t mode);

/* The part of a name after the last dot, lower case or not as it came. A
   name with no dot in it has no extension and the answer is empty. */
char *extension(const char *name)
{
    static char ext[EXTENSION_ROOM];
    int seen = 0;
    int n = 0;

    for (; *name; name++) {
        if (*name == '.') {
            seen = 1;
            n = 0;
            continue;
        }
        if (seen && n < EXTENSION_ROOM)
            ext[n++] = *name;
    }
    ext[n] = 0;
    return ext;
}

/* Everything starts at nought. */
void constructSoundFile(SoundFile *sf)
{
    int i;

    for (i = 0; i < 15; i++)
        ((int32_t *)sf)[i] = 0;
}

/* Choose the format from an extension and let it look at what it has been
   given. Answers true only when a format was found by name and it accepted
   the rest; falling back on the default counts as not found. */
int soundFileFormat(SoundFile *sf, const char *ext, int32_t mode)
{
    int named = 0;
    int accepted = 0;

    if (sf->status != SF_CLOSED)
        return 0;

    sf->format = soundFormatNamed(ext);
    if (!sf->format)
        sf->format = defaultSoundFormat();
    else
        named = 1;

    sf->mode = mode;
    accepted = ((FmtOneFn)FMT_SLOT(sf->format, FMT_CONFIGURE))(sf);
    return named & accepted;
}

/* ---- what the caller may set --------------------------------------- */

int soundFileSetDevice(SoundFile *sf, int32_t device, int32_t mode)
{
    int ok = 0;

    if (sf->status != SF_CLOSED)
        return 0;

    sf->device = device;
    ok = soundFileFormat(sf, "dev", mode);
    if (ok)
        sf->name = 0;
    return ok;
}

int soundFileWidth(SoundFile *sf, int32_t width)
{
    if (sf->status != SF_CLOSED)
        return 0;

    sf->width = width;
    if (!sf->format)
        return 0;
    return ((FmtOneFn)FMT_SLOT(sf->format, FMT_CONFIGURE))(sf);
}

/* ---- opening and closing -------------------------------------------- */

/* Open for writing. A format that works through a file wants one opened
   first; one that works through a device does not. If the format then
   refuses, whatever was opened is closed again. */
int soundFileCreateOutput(SoundFile *sf)
{
    int rc;

    if (sf->status != SF_CLOSED || !sf->format)
        return 0;

    if (FMT_USES_FILE(sf->format)) {
        if (!sf->name)
            return 0;
        sf->file = fopen(sf->name, "wb");
        if (!sf->file)
            return 0;
    }

    sf->status = SF_OUTPUT;
    rc = ((FmtTwoFn)FMT_SLOT(sf->format, FMT_CREATE_OUTPUT))(sf->format, sf);
    if (rc != 1) {
        if (FMT_USES_FILE(sf->format)) {
            fclose(sf->file);
            sf->file = 0;
        }
        sf->status = SF_CLOSED;
    }
    return rc;
}

/* Close. Anything that was being written is finished off first, and the
   answer is false if either step complained, but the record is closed
   either way. */
int soundFileClose(SoundFile *sf)
{
    int ok = 1;

    if (sf->status != SF_CLOSED) {
        if (sf->status == SF_OUTPUT || sf->status == SF_APPEND) {
            if (!((FmtOneFn)FMT_SLOT(sf->format, FMT_FINISH))(sf))
                ok = 0;
        }
        if (!((FmtOneFn)FMT_SLOT(sf->format, FMT_CLOSE))(sf))
            ok = 0;
        if (FMT_USES_FILE(sf->format)) {
            fclose(sf->file);
            sf->file = 0;
        }
    }
    sf->status = SF_CLOSED;
    return ok;
}

/* ---- what it does while open ---------------------------------------- */

void soundFilePoll(SoundFile *sf)
{
    ((FmtOneFn)FMT_SLOT(sf->format, FMT_POLL))(sf);
}

void soundFileSetIndexCallback(SoundFile *sf, void *cb, void *param)
{
    ((FmtIndexFn)FMT_SLOT(sf->format, FMT_SET_INDEX_CB))(sf, cb, param);
}
