/* The romanizer manager -- UNFINISHED, and not yet in the build.
 *
 * This file is committed part-done on purpose. Everything below is
 * transcribed and believed right, but three functions are missing and
 * without them nothing links, so it is deliberately absent from the
 * Makefile and from tools/delta-swap.py. Wiring it in is the last step, not
 * the first.
 *
 * What this object is. The synthesis thread hands every scrap of text to a
 * romanizer manager before the engine sees it. For a language written in
 * another script that manager loads a romanizer and converts; for the rest
 * it is a pass-through that tidies the text and hands it on. US English
 * takes the second road, so the pass-through is live and matters, and the
 * conversion is dead.
 *
 * That was my mistake earlier in this work: I assumed a thing called a
 * romanizer was only for other scripts and stubbed the whole manager out.
 * The engine went silent. It is called seventeen times for addParam and ten
 * for processRemaining on a single sentence, and it is on the text path
 * proper.
 *
 * What is missing, and why. Three functions:
 *
 *   processText          663 bytes, walks the text and rewrites it
 *   countAdditionSpace   334 bytes, works out how much room that will need
 *   processSpecial       333 bytes, handles the characters that mean
 *                        something to the engine
 *
 * All three are on the live path, which is exactly why they are not here:
 * a misread branch in them would change the audio subtly rather than
 * loudly, and they need reading with more care than I could give them at
 * the end of a long session.
 *
 * One further finding worth keeping. getRomanizerInst loads a romanizer
 * through Win32 LoadLibrary and GetProcAddress, and getLibraryName -- in
 * IBM's own code -- returns nothing at all. So the loading half of this
 * object is platform code, and belongs on the far side of the same boundary
 * as the sound device rather than being transcribed. It is written that way
 * below.
 */

#include <stdint.h>
#include <string.h>
#include "eci_synththread.h"

typedef struct RomanizerManager RomanizerManager;
typedef struct RomInstance RomInstance;
typedef struct SynthThread SynthThread;

/* The manager's own record. The two arrays are eighteen language families
   of two dialects each: one of romanizers held open, one of the names they
   were loaded from. */
#define RM_LOCK(m)        ((void *)(m))                         /* +0x000 */
#define RM_INI(m)         ((void *)((char *)(m) + 0x00c))       /* +0x00c */
#define RM_NAMES(m, f, d) (*(char **)((char *)(m) + 0x130 + (f) * 8 + (d) * 4))
#define RM_ACTIVE(m)      (*(RomInstance **)((char *)(m) + 0x1c4))
#define RM_LAST_FLAG(m)   (*(int32_t *)((char *)(m) + 0x1c8))
#define RM_STOPPED(m)     (*(int32_t *)((char *)(m) + 0x1cc))
#define RM_ROMS(m, f, d)  (*(RomInstance **)((char *)(m) + 0x1d0 + (f) * 8 \
                                             + (d) * 4))
#define RM_THREAD(m)      (*(SynthThread **)((char *)(m) + 0x260))
#define RM_FAMILY(m)      (*(int32_t *)((char *)(m) + 0x264))
#define RM_DIALECT(m)     (*(int32_t *)((char *)(m) + 0x268))
#define RM_PENDING(m)     (*(char **)((char *)(m) + 0x26c))
#define RM_OUT(m)         (*(char **)((char *)(m) + 0x270))
#define RM_HAVE_PENDING(m) (*(int32_t *)((char *)(m) + 0x274))

#define RM_FAMILIES  0x12
#define RM_DIALECTS  2

/* Slots in a romanizer's own table. */
#define ROM_DELETE          0x08
#define ROM_ADD_TEXT        0x0c
#define ROM_INSERT_INDEX    0x10
#define ROM_PROCESS         0x14
#define ROM_STOP            0x18
#define ROM_RESUME          0x1c
#define ROM_UNICODE_TO_MBCS 0x20
#define ROM_SET_PARAM       0x28
#define ROM_SET_LANGUAGE    0x2c
#define ROM_CLEAR_ERRORS    0x3c
#define ROM_PROG_STATUS     0x40
#define ROM_ERROR_MESSAGE   0x44
#define ROM_ADD_PARAM       0x6c

#define ROM_SLOT(r, off) ((*(void ***)(r))[(off) / 4])

typedef int  (THIS *RomIntFn)(RomInstance *r);
typedef int  (THIS *RomOneFn)(RomInstance *r, int32_t a);
typedef int  (THIS *RomTwoFn)(RomInstance *r, const char *s, int32_t n);
typedef void (THIS *RomVoidOneFn)(RomInstance *r, void *a);

extern THIS void *mutexCtor(void *m, int32_t recursive)
    MANGLED("??0Mutex@@QAE@H@Z");
extern THIS void mutexDtor(void *m) MANGLED("??1Mutex@@QAE@XZ");
extern THIS int mutexWait(void *m, int32_t ms) MANGLED("?wait@Mutex@@QAEHJ@Z");
extern THIS int mutexRelease(void *m) MANGLED("?release@Mutex@@QAEHXZ");
extern THIS void *iniFileReaderCtor(void *r)
    MANGLED("??0IniFileReader@@QAE@XZ");
extern THIS void iniFileReaderDtor(void *r)
    MANGLED("??1IniFileReader@@QAE@XZ");
extern THIS void addTextToEngine(SynthThread *t, char *text, int32_t n)
    MANGLED("?addTextToEngine@SynthThread@@QAEXPADH@Z");
extern THIS void threadProcessRemaining(SynthThread *t)
    MANGLED("?processRemaining@SynthThread@@QAEXXZ");

/* The table that maps one byte to another when no corpus is loaded. */
extern const uint8_t ConversionTable[256] MANGLED("_ConversionTable");

/* Where the synthesis thread keeps its corpora. */
#define ST_CORPORA_AT(t) (*(void **)((char *)(t) + 0x3c0))

int rz_isRomExist(int32_t family, int32_t dialect);
void rz_removeUnusedByCode(RomanizerManager *m, uint8_t f, uint8_t d);

/* ---- which languages have a romanizer at all ------------------------ */

/* Five families do, and only some of their dialects. Everything else is
   spoken as it is written. */
int rz_isRomExist(int32_t family, int32_t dialect)
{
    switch (family) {
    case 6:  return dialect == 0 || dialect == 1;
    case 8:  return dialect == 0;
    case 10: return dialect == 0;
    case 11: return dialect == 0 || dialect == 1;
    case 16: return dialect == 0;
    default: return 0;
    }
}

/* ---- making and unmaking -------------------------------------------- */

static void rz_forgetAll(RomanizerManager *m)
{
    int f, d;

    for (f = 0; f < RM_FAMILIES; f++)
        for (d = 0; d < RM_DIALECTS; d++) {
            RM_ROMS(m, f, d) = 0;
            RM_NAMES(m, f, d) = 0;
        }
    RM_ACTIVE(m) = 0;
    RM_FAMILY(m) = 0;
    RM_DIALECT(m) = 0;
    RM_HAVE_PENDING(m) = 0;
    RM_LAST_FLAG(m) = 0;
}

THIS void *rz_ctor(RomanizerManager *m, SynthThread *thread)
{
    mutexCtor(RM_LOCK(m), 0);
    iniFileReaderCtor(RM_INI(m));
    RM_THREAD(m) = thread;
    rz_forgetAll(m);
    RM_OUT(m) = 0;
    RM_STOPPED(m) = 0;
    return m;
}

THIS void rz_dtor(RomanizerManager *m)
{
    int f, d;

    for (f = 0; f < RM_FAMILIES; f++)
        for (d = 0; d < RM_DIALECTS; d++) {
            RomInstance *r = RM_ROMS(m, f, d);

            if (r)
                ((RomVoidOneFn)ROM_SLOT(r, ROM_DELETE))(r, 0);
        }

    if (RM_OUT(m))
        cpp_delete(RM_OUT(m));

    iniFileReaderDtor(RM_INI(m));
    mutexDtor(RM_LOCK(m));
}

/* ---- what runs when there is no romanizer --------------------------- */

/* A parameter on its way down. With a romanizer it is that romanizer's
   business; without one it is text like any other and goes straight to the
   engine. */
THIS int rz_addParam(RomanizerManager *m, const char *s, int32_t n)
{
    if (RM_ACTIVE(m))
        return ((RomTwoFn)ROM_SLOT(RM_ACTIVE(m), ROM_ADD_PARAM))(
                   RM_ACTIVE(m), s, n);

    addTextToEngine(RM_THREAD(m), (char *)s, n);
    return 1;
}

/* An index mark. Without a romanizer it becomes the annotation that means
   the same thing. */
THIS int rz_insertIndex(RomanizerManager *m)
{
    if (RM_ACTIVE(m))
        return ((RomIntFn)ROM_SLOT(RM_ACTIVE(m), ROM_INSERT_INDEX))(
                   RM_ACTIVE(m));

    addTextToEngine(RM_THREAD(m), "`ui", 3);
    return 1;
}

THIS void rz_clear(RomanizerManager *m)
{
    RM_PENDING(m) = 0;
    RM_HAVE_PENDING(m) = 0;
}

THIS int rz_resume(RomanizerManager *m)
{
    if (RM_ACTIVE(m))
        ((RomIntFn)ROM_SLOT(RM_ACTIVE(m), ROM_RESUME))(RM_ACTIVE(m));
    RM_STOPPED(m) = 0;
    return 1;
}

THIS int rz_stop(RomanizerManager *m)
{
    RM_STOPPED(m) = 1;
    if (RM_ACTIVE(m)
        && !((RomIntFn)ROM_SLOT(RM_ACTIVE(m), ROM_STOP))(RM_ACTIVE(m)))
        return 0;
    return 1;
}

THIS RomInstance *rz_getRom(RomanizerManager *m, uint32_t lang)
{
    (void)lang;
    return RM_ACTIVE(m);
}

THIS void rz_romClearErrors(RomanizerManager *m)
{
    int f, d;

    for (f = 0; f < RM_FAMILIES; f++)
        for (d = 0; d < RM_DIALECTS; d++) {
            RomInstance *r = RM_ROMS(m, f, d);

            if (r)
                ((RomIntFn)ROM_SLOT(r, ROM_CLEAR_ERRORS))(r);
        }
}

THIS uint32_t rz_romProgStatus(RomanizerManager *m)
{
    if (!RM_ACTIVE(m))
        return 0;
    return ((RomIntFn)ROM_SLOT(RM_ACTIVE(m), ROM_PROG_STATUS))(RM_ACTIVE(m));
}

THIS void rz_romErrorMessage(RomanizerManager *m, char *out)
{
    if (RM_ACTIVE(m)) {
        ((RomVoidOneFn)ROM_SLOT(RM_ACTIVE(m), ROM_ERROR_MESSAGE))(
            RM_ACTIVE(m), out);
        return;
    }
    strcpy(out, "No Romanizer Error");
}

/* Taking a romanizer out of use does nothing at all in this build. */
void rz_removeUnusedByCode(RomanizerManager *m, uint8_t f, uint8_t d)
{
    (void)m;
    (void)f;
    (void)d;
}

THIS void rz_removeUnused(RomanizerManager *m, int32_t *lang)
{
    rz_removeUnusedByCode(m, (uint8_t)((*lang & 0xff0000) >> 16),
                          (uint8_t)(*lang & 0xff));
}

/* ---- the one byte-for-byte conversion that is not a romanizer -------- */

/* With no corpus loaded, every byte is mapped through one table. With a
   corpus the text is left alone, because the corpus has already had it. */
THIS void rz_convertText(RomanizerManager *m, uint8_t *text)
{
    if (ST_CORPORA_AT(RM_THREAD(m)))
        return;

    for (; *text; text++)
        *text = ConversionTable[*text];
}

/* ---- MISSING ---------------------------------------------------------
 *
 * processText, countAdditionSpace and processSpecial go here. Until they do
 * this file cannot be linked, which is why it is not in the build.
 *
 * processText is the pass-through proper: it walks the text, turns a
 * newline into a space unless one is already there, copies runs across with
 * strncat, and hands anything the engine treats as special to
 * processSpecial. countAdditionSpace works out in advance how much longer
 * the rewritten text will be, so one allocation can hold it.
 */
