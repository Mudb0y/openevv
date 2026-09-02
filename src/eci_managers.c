/* Two managers the formant engine never asks anything of.

   The synthesis thread holds three: a filter manager, a romanizer manager,
   and a concatenation manager. The first of those is real and is in
   src/eci_filtermanager.c, since the SSML reader hangs off it. The other
   two are here. The romanizer belongs to the languages that are written in
   another script. The concatenative manager belongs to the other engine;
   this extraction runs the formant one.

   So these are interfaces met rather than code transcribed, on the same
   footing as the sound boundary. Each answers the way an empty manager
   would: nothing is present, nothing is active, nothing is supported.

   Names are prefixed and the aliases at the foot carry the real ones. */

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "eci_synththread.h"
#include "evv_abi.h"

/* While this is set every call reports itself, which is how the claim above
   was established rather than assumed. */
#define MANAGERS_REPORT 0

#if MANAGERS_REPORT
#define SAW(name) do { fprintf(stderr, "MGR %s\n", name); fflush(stderr); } \
                  while (0)
#else
#define SAW(name) ((void)0)
#endif

/* The four numbers the concatenation manager is told and asked for again.
 *
 * Everything else about that manager is an interface met rather than code
 * transcribed, because the concatenative engine is not in this extraction.
 * These four are not about that engine at all: they are what the synthesis
 * thread hands the manager and reads back out of it, and one of them -- the
 * sample rate -- is handed on to a romanizer, which is the only thing in the
 * engine that ever asks. So an empty manager cannot answer nought here; it
 * has to remember.
 *
 * They sit where IBM's setActiveLanguage puts them, which is inside the
 * 0x2c0 bytes the thread allocates for one, and nothing but this file reads
 * them. The three that are bytes are bytes in the original too. */
#define CM_FAMILY(m)  (*(uint8_t *)((char *)(m) + 0x144))
#define CM_DIALECT(m) (*(uint8_t *)((char *)(m) + 0x148))
#define CM_VOICE(m)   (*(uint8_t *)((char *)(m) + 0x14c))
#define CM_RATE(m)    (*(uint32_t *)((char *)(m) + 0x150))

/* And a fifth, which is not one of those four and is read by somebody else:
   the synthesis thread's sample callback asks the manager whether what it is
   about to hand on still needs converting. IBM's concatenative engine can
   produce audio already at the caller's rate, and says so here.

   Ours never can, because in this extraction every sample comes from the
   formant synthesiser at the engine's own rate. So it is one, always, and
   the converter in src/eci_pcm.c is what raises it. It was left unset before
   there was a converter to skip, which mattered not at all while there was
   none and would have decided the question on whatever malloc last had at
   that address the moment there was. */
#define CM_RAW(m)     (*(uint8_t *)((char *)(m) + 0x2ac))

/* How much the thread allocates for one, so that the whole of it can be put
   in a known state rather than the handful of fields named here. */
#define CM_BYTES      0x2c0

/* Which of them setParam is about. */
#define CM_PARAM_LANGUAGE 0x02
#define CM_PARAM_VOICE    0x10
#define CM_PARAM_RATE     0x11

/* ---- the romanizer -------------------------------------------------- */

THIS void *rm_ctor(void *m, void *thread)
{
    SAW("RomanizerManager ctor");
    (void)thread;
    return m;
}

THIS void rm_dtor(void *m)
{
    SAW("RomanizerManager dtor");
    (void)m;
}

THIS int rm_addParam(void *m, const char *s, int32_t n)
{
    SAW("rom addParam");
    (void)m; (void)s; (void)n;
    return 0;
}

THIS int rm_addText(void *m, const char *s, int32_t a, int32_t b)
{
    SAW("rom addText");
    (void)m; (void)s; (void)a; (void)b;
    return 0;
}

THIS void rm_clear(void *m)
{
    SAW("rom clear");
    (void)m;
}

THIS void *rm_getRom(void *m, uint32_t lang)
{
    SAW("getRom");
    (void)m; (void)lang;
    return 0;
}

THIS int rm_insertIndex(void *m)
{
    SAW("rom insertIndex");
    (void)m;
    return 0;
}

THIS int rm_MBCSToUnicode(void *m, uint32_t lang, const char *in,
                          uint16_t **out)
{
    SAW("MBCSToUnicode");
    (void)m; (void)lang; (void)in;
    if (out)
        *out = 0;
    return 0;
}

THIS int rm_processRemaining(void *m, char **out)
{
    SAW("rom processRemaining");
    (void)m;
    if (out)
        *out = 0;
    return 0;
}

THIS int rm_processSentence(void *m, char **out, int32_t n)
{
    SAW("rom processSentence");
    (void)m; (void)n;
    if (out)
        *out = 0;
    return 0;
}

THIS void rm_removeUnused(void *m, void *lang)
{
    SAW("removeUnusedRomanizer");
    (void)m; (void)lang;
}

THIS int rm_resume(void *m)
{
    SAW("rom resume");
    (void)m;
    return 0;
}

THIS void rm_clearErrors(void *m)
{
    SAW("romClearErrors");
    (void)m;
}

THIS int rm_setParam(void *m, int32_t which, int32_t value)
{
    SAW("rom setParam");
    (void)m; (void)which; (void)value;
    return 0;
}

THIS int rm_stop(void *m)
{
    SAW("rom stop");
    (void)m;
    return 0;
}

THIS int rm_UnicodeToMBCS(void *m, uint32_t lang, const uint16_t *in,
                          char **out, int32_t n)
{
    SAW("UnicodeToMBCS");
    (void)m; (void)lang; (void)in; (void)n;
    if (out)
        *out = 0;
    return 0;
}

/* ---- the concatenative engine --------------------------------------- */

THIS void *cm_ctor(void *m, void *thread)
{
    SAW("ConcatenationManager ctor");
    (void)thread;
    /* All of it, not just the fields below: this block is malloc'd and read
       by offset, so anything named later that nobody thought to clear here
       would start as whatever was in that memory. */
    memset(m, 0, CM_BYTES);
    CM_FAMILY(m) = 0;
    CM_DIALECT(m) = 0;
    CM_VOICE(m) = 0;
    CM_RATE(m) = 0;
    CM_RAW(m) = 1;
    return m;
}

THIS void cm_dtor(void *m)
{
    SAW("ConcatenationManager dtor");
    (void)m;
}

THIS void cm_bufferSPR(void *m, const char *s, int32_t n)
{
    SAW("bufferSPR");
    (void)m; (void)s; (void)n;
}

THIS int cm_engineSupports(void *m, uint32_t a, uint32_t b)
{
    SAW("engineSupportsConcatenative");
    (void)m; (void)a; (void)b;
    return 0;
}

THIS uint32_t cm_getActiveSampleRate(void *m)
{
    SAW("getActiveSampleRate");
    return CM_RATE(m);
}

THIS void cm_processStarCommand(void *m, char *s)
{
    SAW("processStarCommand");
    (void)m; (void)s;
}

THIS void cm_registerCallbackA(void *m, uint32_t a, void *fn, void *data)
{
    SAW("concat registerCallback A");
    (void)m; (void)a; (void)fn; (void)data;
}

THIS void cm_registerCallbackB(void *m, uint32_t a, void *fn, void *data)
{
    SAW("concat registerCallback B");
    (void)m; (void)a; (void)fn; (void)data;
}

THIS void cm_registerCallbackC(void *m, void *fn, void *data)
{
    SAW("concat registerCallback C");
    (void)m; (void)fn; (void)data;
}

THIS int32_t cm_registerVoice(void *m, int32_t n, void *attrib, void *data)
{
    SAW("concat registerVoice");
    (void)m; (void)n; (void)attrib; (void)data;
    return 0;
}

/* The original works out the new set of four, calls its own
   setActiveLanguage with all of them, and that is what writes them down.
   With no concatenative engine to set a language on, the writing down is
   all there is. */
THIS int cm_setParam(void *m, int32_t which, int32_t a, int32_t b)
{
    SAW("concat setParam");
    (void)b;
    switch (which) {
    case CM_PARAM_LANGUAGE:
        CM_FAMILY(m) = (uint8_t)((a & 0xff0000) >> 16);
        CM_DIALECT(m) = (uint8_t)(a & 0xff);
        break;
    case CM_PARAM_VOICE:
        CM_VOICE(m) = (uint8_t)a;
        break;
    case CM_PARAM_RATE:
        CM_RATE(m) = (uint32_t)a;
        break;
    default:
        break;
    }
    return 0;
}

THIS int cm_setTorrentParam1(void *m, uint32_t a, int32_t b)
{
    SAW("setTorrentParam1");
    (void)m; (void)a; (void)b;
    return 0;
}

THIS int cm_setTorrentParam2(void *m, uint32_t a, int32_t b)
{
    SAW("setTorrentParam2");
    (void)m; (void)a; (void)b;
    return 0;
}

THIS int32_t cm_unregisterVoice(void *m, int32_t n, void *attrib, void **out)
{
    SAW("concat unregisterVoice");
    (void)m; (void)n; (void)attrib;
    if (out)
        *out = 0;
    return 0;
}

THIS int cm_usingConcatenativeEngine(void *m)
{
    SAW("usingConcatenativeEngine");
    (void)m;
    return 0;
}

THIS int cm_voiceIsConcatenative(void *m, int32_t voice)
{
    SAW("voiceIsConcatenative");
    (void)m; (void)voice;
    return 0;
}


ALIAS("??0ConcatenationManager@@QAE@PAVSynthThread@@@Z", "cm_ctor");
ALIAS("??1ConcatenationManager@@QAE@XZ", "cm_dtor");
ALIAS("?bufferSPR@ConcatenationManager@@QAEXPBDH@Z", "cm_bufferSPR");
ALIAS("?engineSupportsConcatenative@ConcatenationManager@@QAEHKK@Z", "cm_engineSupports");
ALIAS("?getActiveSampleRate@ConcatenationManager@@QAEIXZ", "cm_getActiveSampleRate");
ALIAS("?processStarCommand@ConcatenationManager@@QAEXPAD@Z", "cm_processStarCommand");
ALIAS("?registerCallback@ConcatenationManager@@QAEXKP6AXHPAX@Z0@Z", "cm_registerCallbackA");
ALIAS("?registerCallback@ConcatenationManager@@QAEXKP6AXPAX@Z0@Z", "cm_registerCallbackB");
ALIAS("?registerCallback@ConcatenationManager@@QAEXP6AXHPAJPAX@Z1@Z", "cm_registerCallbackC");
ALIAS("?registerVoice@ConcatenationManager@@QAEJHPAUECIExtendedVoiceAttrib@@PAX@Z", "cm_registerVoice");
ALIAS("?setParam@ConcatenationManager@@QAEHJHH@Z", "cm_setParam");
ALIAS("?setTorrentParam1@ConcatenationManager@@QAEHKJ@Z", "cm_setTorrentParam1");
ALIAS("?setTorrentParam2@ConcatenationManager@@QAEHKJ@Z", "cm_setTorrentParam2");
ALIAS("?unregisterVoice@ConcatenationManager@@QAEJHPAUECIVoiceAttrib@@PAPAX@Z", "cm_unregisterVoice");
ALIAS("?usingConcatenativeEngine@ConcatenationManager@@QAEHXZ", "cm_usingConcatenativeEngine");
ALIAS("?voiceIsConcatenative@ConcatenationManager@@QAEHH@Z", "cm_voiceIsConcatenative");
