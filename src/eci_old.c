/* The published interface, as it was before the engine grew a second one.

   Everything a program links against is here. Underneath it there is a newer
   interface, the one whose entry points end in a two, and most of what this
   layer does is hold a record of its own beside that one and hand the call
   on. The record is what the older interface promised: a callback, a queue
   the caller can fill before speaking, eight voices it can edit, and the
   settings that go with them.

   Two things run through nearly every entry point. The first is a guard
   against being called from inside its own callback: a call that arrives
   while another is still running is refused, and the bit it would have used
   is remembered so the layer above can tell what was missed. The second is
   that the answer from underneath is turned into the older interface's own
   notion of an error before anyone sees it.

   This is the first part of that object. Names are prefixed and the aliases
   at the foot carry the real ones. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "eci_synththread.h"

/* Which call was refused. Every entry point has its own bit; the ones here
   share the same one because the original gives them the same one. */
#define REFUSED_GENERAL 0x800
#define REFUSED_SYNTH   0x80

/* What the layer underneath answers when the caller has gone away. */
#define POLL_ABORTED   (-18)
/* And the two answers that mean it is still speaking. */
#define POLL_WORKING     1
#define POLL_BUSY        3

typedef struct OldInst OldInst;

/* One thing the caller put on the queue before asking for it to be spoken.
   Only what this file touches is named. */
typedef struct QueueElement {
    int32_t kind;                   /* +0x00, nought means it owns text */
    void   *text;                   /* +0x04 */
    uint8_t pad_08[0xa0 - 0x08];
    struct QueueElement *next;      /* +0xa0 */
} QueueElement;

/* The old interface's own record. Named by offset because most of it still
   belongs to the original. */
#define OI_NEW(h)       (*(void **)((char *)(h) + 0x00c))
#define OI_CALLBACK(h)  (*(void **)((char *)(h) + 0x010))
#define OI_CBDATA(h)    (*(void **)((char *)(h) + 0x014))
#define OI_LASTINDEX(h) (*(int32_t *)((char *)(h) + 0x3d8))
#define OI_QHEAD(h)     (*(QueueElement **)((char *)(h) + 0x60c))
#define OI_QTAIL(h)     (*(QueueElement **)((char *)(h) + 0x610))
#define OI_STOPPED(h)   (*(int32_t *)((char *)(h) + 0x6a4))
#define OI_REFUSEDALL(h) (*(uint32_t *)((char *)(h) + 0x6ac))
#define OI_REFUSED(h)   (*(uint32_t *)((char *)(h) + 0x6b0))
#define OI_BUSY(h)      (*(int32_t *)((char *)(h) + 0x6b4))

extern int32_t __stdcall eciPoll2(void *h) MANGLED("_eciPoll2@4");
extern int32_t __stdcall eciStop2(void *h) MANGLED("_eciStop2@4");
extern int32_t __stdcall eciSynthesize2(void *h) MANGLED("_eciSynthesize2@4");
extern void __stdcall eciVersion2(int32_t *a, int32_t *b, int32_t *c,
                                  int32_t *d) MANGLED("_eciVersion2@16");
extern int eciGetAvailableLanguages2(uint32_t *out, int *count)
    MANGLED("?eciGetAvailableLanguages2@@YAHPAW4ECILanguageDialect@@PAH@Z");
/* The one routine in this object that is driven by a jump table, so it is
   still the original's until those tables are lifted. */
extern int32_t setECIerror(int32_t rc, OldInst *h)
    MANGLED("?setECIerror@@YAJJPAUoldECIInstData@@@Z");

/* Declared here because the entry points call one another. The queue that
   the caller fills is still the original's; only emptying it is ours. */
int __stdcall eo_stop(OldInst *h);

/* Refuse a call that arrived while another was still running, and remember
   which one it was. Answers true when the call must not go on. */
static int eo_reentered(OldInst *h, uint32_t bit)
{
    if (!h || !OI_BUSY(h))
        return 0;
    OI_REFUSED(h) = bit;
    OI_REFUSEDALL(h) |= bit;
    return 1;
}

/* ---- the queue the caller fills before speaking ---- */

/* Throw away everything waiting, and the text any of it owns. */
void eo_clearManualQueue(OldInst *h)
{
    QueueElement *e = OI_QHEAD(h);

    while (e) {
        QueueElement *gone;

        if (e->kind == 0)
            free(e->text);
        gone = e;
        e = e->next;
        free(gone);
    }
    OI_QHEAD(h) = 0;
    OI_QTAIL(h) = 0;
}

/* ---- the entry points ---- */

/* Nothing to do: errors are cleared as they are read. */
void __stdcall eo_clearErrors(OldInst *h)
{
    (void)h;
}

/* Nor here: the newer interface has no separate step for this. */
void __stdcall eo_synchronizeSynth(OldInst *h)
{
    (void)h;
}

int __stdcall eo_getAvailableLanguages(uint32_t *out, int *count)
{
    return eciGetAvailableLanguages2(out, count);
}

/* The last index mark the callback was told about, kept so a caller that did
   not want a callback can ask instead. */
int32_t __stdcall eo_getIndex(OldInst *h)
{
    if (!h)
        return 0;
    return OI_LASTINDEX(h);
}

/* Where the caller's own callback is put. Nothing else happens: the bridge
   between it and the engine's was installed when the instance was made. */
void __stdcall eo_registerCallback(OldInst *h, void *cb, void *data)
{
    if (eo_reentered(h, REFUSED_GENERAL))
        return;
    if (!h)
        return;
    OI_CALLBACK(h) = cb;
    OI_CBDATA(h) = data;
}

/* Throw away what has been given but not yet spoken. */
int __stdcall eo_clearInput(OldInst *h)
{
    if (eo_reentered(h, REFUSED_GENERAL))
        return 0;
    if (!h)
        return 0;
    eo_clearManualQueue(h);
    return 1;
}

/* Stop speaking and forget what was queued. Answering that it worked is not
   the same as the engine agreeing: a refusal from underneath leaves the
   instance marked not busy and answers false. */
int __stdcall eo_stop(OldInst *h)
{
    if (eo_reentered(h, REFUSED_GENERAL))
        return 0;
    if (h)
        OI_BUSY(h) = 1;
    if (!h)
        return 0;

    eo_clearManualQueue(h);
    if (setECIerror(eciStop2(OI_NEW(h)), h)) {
        OI_BUSY(h) = 0;
        return 0;
    }
    OI_STOPPED(h) = 1;
    OI_BUSY(h) = 0;
    return 1;
}

/* eciSynthesize is not here. It has to push the caller's own queue across
   first, and the routine that does that is one the original keeps to
   itself, so nothing outside its object can reach it. It comes with the
   text path, which is where that queue is built. */

/* Is it still speaking? Asking is also what drives the queue of results, so
   a caller that never asks never hears anything.

   A caller that has gone away is answered by stopping outright. */
int __stdcall eo_speaking(OldInst *h)
{
    int32_t rc;

    if (eo_reentered(h, REFUSED_GENERAL))
        return 0;
    if (h)
        OI_BUSY(h) = 1;
    if (!h)
        return 0;

    rc = setECIerror(eciPoll2(OI_NEW(h)), h);
    OI_BUSY(h) = 0;
    if (rc == POLL_ABORTED) {
        eo_stop(h);
        return 0;
    }
    return rc == POLL_BUSY || rc == POLL_WORKING;
}

/* The version, as four numbers in a string. */
void __stdcall eo_version(char *out)
{
    int32_t a = 0, b = 0, c = 0, d = 0;

    if (!out)
        return;
    eciVersion2(&a, &b, &c, &d);
    sprintf(out, "%d.%d.%d.%d", a, b, c, d);
}

/* A stdcall name carries the size of its arguments, so these need the alias
   that puts it back. */
#define ALIAS_N(mangled, ours, n) \
    __asm__(".globl \"" mangled "\"\n.set \"" mangled "\", _" ours "@" #n "\n")

ALIAS_N("_eciClearErrors@4", "eo_clearErrors", 4);
ALIAS_N("_eciSynchronizeSynth@4", "eo_synchronizeSynth", 4);
ALIAS_N("_eciGetAvailableLanguages@8", "eo_getAvailableLanguages", 8);
ALIAS_N("_eciGetIndex@4", "eo_getIndex", 4);
ALIAS_N("_eciRegisterCallback@12", "eo_registerCallback", 12);
ALIAS_N("_eciClearInput@4", "eo_clearInput", 4);
ALIAS_N("_eciStop@4", "eo_stop", 4);
ALIAS_N("_eciSpeaking@4", "eo_speaking", 4);
ALIAS_N("_eciVersion@4", "eo_version", 4);
ALIAS("?clearManualQueue@@YAXPAUoldECIInstData@@@Z", "eo_clearManualQueue");
