/* Three managers the formant engine never asks anything of.

   The synthesis thread holds one of each: a filter manager, a romanizer
   manager, and a concatenation manager. Between them they are the front
   doors to about two hundred and twenty thousand bytes of IBM's code -- the
   whole SSML and XML reader hangs off the first, the concatenative engine off
   the third -- and on the path this port takes, none of it runs.

   The filter manager is never asked to filter anything: the SSML filter
   reports itself inactive and annotations reach the engine directly. The
   romanizer belongs to the languages that are written in another script.
   The concatenative manager belongs to the other engine; this extraction
   runs the formant one.

   So these are interfaces met rather than code transcribed, on the same
   footing as the sound boundary. Each answers the way an empty manager
   would: nothing is present, nothing is active, nothing is supported.

   Names are prefixed and the aliases at the foot carry the real ones. */

#include <stdint.h>
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

/* Which of them setParam is about. */
#define CM_PARAM_LANGUAGE 0x02
#define CM_PARAM_VOICE    0x10
#define CM_PARAM_RATE     0x11

/* What a filter call answers when there is no filter. */
#define FILTER_OK          0
#define FILTER_NOT_FOUND   1

/* ---- filters, and the SSML reader behind them ----------------------- */

THIS void *fm_ctor(void *m, void *thread)
{
    SAW("FilterManager ctor");
    (void)thread;
    return m;
}

THIS void fm_dtor(void *m)
{
    SAW("FilterManager dtor");
    (void)m;
}

THIS int32_t fm_activateById(void *m, uint32_t id, int8_t on)
{
    SAW("activateFilter by id");
    (void)m; (void)id; (void)on;
    return FILTER_NOT_FOUND;
}

THIS int32_t fm_activateByHandle(void *m, void *filter)
{
    SAW("activateFilter by handle");
    (void)m; (void)filter;
    return FILTER_NOT_FOUND;
}

THIS void fm_autoLoadFilter(void *m, void *lang)
{
    SAW("autoLoadFilter");
    (void)m; (void)lang;
}

THIS int32_t fm_deactivateAll(void *m)
{
    SAW("deactivateAllFilters");
    (void)m;
    return FILTER_OK;
}

THIS int32_t fm_deactivateById(void *m, uint32_t id, int8_t on)
{
    SAW("deactivateFilter by id");
    (void)m; (void)id; (void)on;
    return FILTER_NOT_FOUND;
}

THIS int32_t fm_deactivateByHandle(void *m, void *filter)
{
    SAW("deactivateFilter by handle");
    (void)m; (void)filter;
    return FILTER_NOT_FOUND;
}

THIS int32_t fm_deleteById(void *m, int32_t id, int32_t which)
{
    SAW("deleteFilter by id");
    (void)m; (void)id; (void)which;
    return FILTER_NOT_FOUND;
}

THIS int32_t fm_deleteByHandle(void *m, void *filter)
{
    SAW("deleteFilter by handle");
    (void)m; (void)filter;
    return FILTER_NOT_FOUND;
}

/* Filtering hands back the text unchanged, which is what an empty chain of
   filters does. */
THIS char *fm_filterTextByHandle(void *m, void *filter, const char *text)
{
    SAW("filterText by handle");
    (void)m; (void)filter;
    return (char *)text;
}

THIS char *fm_filterText(void *m, const char *text, int32_t n)
{
    SAW("filterText");
    (void)m; (void)n;
    return (char *)text;
}

THIS void fm_getAvailableFilters(void *m, int32_t lang, uint32_t *ids,
                                 uint32_t *count)
{
    SAW("getAvailableFilters");
    (void)m; (void)lang; (void)ids;
    if (count)
        *count = 0;
}

THIS char **fm_getFilterDependencies(void *m, int32_t lang, uint32_t id)
{
    SAW("getFilterDependencies");
    (void)m; (void)lang; (void)id;
    return 0;
}

THIS void fm_getFilterDescription(void *m, int32_t lang, uint32_t id,
                                  char *out)
{
    SAW("getFilterDescription");
    (void)m; (void)lang; (void)id;
    if (out)
        out[0] = 0;
}

THIS int fm_isActiveById(void *m, uint32_t id)
{
    SAW("isFilterActive by id");
    (void)m; (void)id;
    return 0;
}

THIS int fm_isActive(void *m, int32_t lang, uint32_t id)
{
    SAW("isFilterActive");
    (void)m; (void)lang; (void)id;
    return 0;
}

THIS int fm_isAutoload(void *m, int32_t lang, uint32_t id)
{
    SAW("isFilterAutoload");
    (void)m; (void)lang; (void)id;
    return 0;
}

THIS int fm_isUsable(void *m, const char *name, int32_t lang, uint32_t id)
{
    SAW("isFilterUsable");
    (void)m; (void)name; (void)lang; (void)id;
    return 0;
}

THIS int32_t fm_loadFilter(void *m, int32_t lang, int32_t id, void **out)
{
    SAW("loadFilter");
    (void)m; (void)lang; (void)id;
    if (out)
        *out = 0;
    return FILTER_NOT_FOUND;
}

THIS int fm_registerFilter(void *m, void *attrib, uint32_t id, void *entry,
                           int8_t flag)
{
    SAW("registerFilter");
    (void)m; (void)attrib; (void)id; (void)entry; (void)flag;
    return FILTER_NOT_FOUND;
}

THIS int fm_unregisterFilter(void *m, void *attrib, uint32_t id)
{
    SAW("unregisterFilter");
    (void)m; (void)attrib; (void)id;
    return FILTER_NOT_FOUND;
}

THIS int fm_updateFilter(void *m, void *a, void *b, int32_t c, void *d,
                         int32_t e)
{
    SAW("updateFilter");
    (void)m; (void)a; (void)b; (void)c; (void)d; (void)e;
    return FILTER_NOT_FOUND;
}

/* The lock the original takes while loading a filter. It is a static member
   of the class, so it is data rather than a function, and it has to exist
   even though nothing here ever takes it. */
int32_t fm_protectFilterLoad[3];

/* And the one the SSML reader takes around its lexer, for the same reason. */
int32_t ssml_lexerMutex[3];

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
    CM_FAMILY(m) = 0;
    CM_DIALECT(m) = 0;
    CM_VOICE(m) = 0;
    CM_RATE(m) = 0;
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

ALIAS("??0FilterManager@@QAE@PAVSynthThread@@@Z", "fm_ctor");
ALIAS("??1FilterManager@@QAE@XZ", "fm_dtor");
ALIAS("?activateFilter@FilterManager@@QAEJK_N@Z", "fm_activateById");
ALIAS("?activateFilter@FilterManager@@QAEJPAX@Z", "fm_activateByHandle");
ALIAS("?autoLoadFilter@FilterManager@@QAEXPAVLangIdentifier@@@Z",
      "fm_autoLoadFilter");
ALIAS("?deactivateAllFilters@FilterManager@@QAEJXZ", "fm_deactivateAll");
ALIAS("?deactivateFilter@FilterManager@@QAEJK_N@Z", "fm_deactivateById");
ALIAS("?deactivateFilter@FilterManager@@QAEJPAX@Z", "fm_deactivateByHandle");
ALIAS("?deleteFilter@FilterManager@@QAEJJH@Z", "fm_deleteById");
ALIAS("?deleteFilter@FilterManager@@QAEJPAX@Z", "fm_deleteByHandle");
ALIAS("?filterText@FilterManager@@QAEPADPAXPBD@Z", "fm_filterTextByHandle");
ALIAS("?filterText@FilterManager@@QAEPADPBDJ@Z", "fm_filterText");
ALIAS("?getAvailableFilters@FilterManager@@QAEXJPAI0@Z",
      "fm_getAvailableFilters");
ALIAS("?getFilterDependencies@FilterManager@@QAEPAPADJI@Z",
      "fm_getFilterDependencies");
ALIAS("?getFilterDescription@FilterManager@@QAEXJIPAD@Z",
      "fm_getFilterDescription");
ALIAS("?isFilterActive@FilterManager@@QAEHI@Z", "fm_isActiveById");
ALIAS("?isFilterActive@FilterManager@@QAEHJI@Z", "fm_isActive");
ALIAS("?isFilterAutoload@FilterManager@@QAEHJI@Z", "fm_isAutoload");
ALIAS("?isFilterUsable@FilterManager@@QAEHPBDJI@Z", "fm_isUsable");
ALIAS("?loadFilter@FilterManager@@QAEJJJPAPAX@Z", "fm_loadFilter");
ALIAS("?m_protectFilterLoad@FilterManager@@0VMutex@@A",
      "fm_protectFilterLoad");
ALIAS("?registerFilter@FilterManager@@QAE?AW4ECIFilterError@@PAUECIFilterAttrib@@IPAP6GHKPAPAX@Z_N@Z",
      "fm_registerFilter");
ALIAS("?unregisterFilter@FilterManager@@QAE?AW4ECIFilterError@@PAUECIFilterAttrib@@I@Z",
      "fm_unregisterFilter");
ALIAS("?updateFilter@FilterManager@@QAE?AW4ECIFilterError@@PAX0J0J@Z",
      "fm_updateFilter");

ALIAS("?lexerMutex@SSMLFilter@@1VMutex@@A", "ssml_lexerMutex");


ALIAS("??0ConcatenationManager@@QAE@PAVSynthThread@@@Z", "cm_ctor");
ALIAS("??1ConcatenationManager@@QAE@XZ", "cm_dtor");
ALIAS("?bufferSPR@ConcatenationManager@@QAEXPBDH@Z", "cm_bufferSPR");
ALIAS("?engineSupportsConcatenative@ConcatenationManager@@QAEHKK@Z",
      "cm_engineSupports");
ALIAS("?getActiveSampleRate@ConcatenationManager@@QAEIXZ",
      "cm_getActiveSampleRate");
ALIAS("?processStarCommand@ConcatenationManager@@QAEXPAD@Z",
      "cm_processStarCommand");
ALIAS("?registerCallback@ConcatenationManager@@QAEXKP6AXHPAX@Z0@Z",
      "cm_registerCallbackA");
ALIAS("?registerCallback@ConcatenationManager@@QAEXKP6AXPAX@Z0@Z",
      "cm_registerCallbackB");
ALIAS("?registerCallback@ConcatenationManager@@QAEXP6AXHPAJPAX@Z1@Z",
      "cm_registerCallbackC");
ALIAS("?registerVoice@ConcatenationManager@@QAEJHPAUECIExtendedVoiceAttrib@@PAX@Z",
      "cm_registerVoice");
ALIAS("?setParam@ConcatenationManager@@QAEHJHH@Z", "cm_setParam");
ALIAS("?setTorrentParam1@ConcatenationManager@@QAEHKJ@Z",
      "cm_setTorrentParam1");
ALIAS("?setTorrentParam2@ConcatenationManager@@QAEHKJ@Z",
      "cm_setTorrentParam2");
ALIAS("?unregisterVoice@ConcatenationManager@@QAEJHPAUECIVoiceAttrib@@PAPAX@Z",
      "cm_unregisterVoice");
ALIAS("?usingConcatenativeEngine@ConcatenationManager@@QAEHXZ",
      "cm_usingConcatenativeEngine");
ALIAS("?voiceIsConcatenative@ConcatenationManager@@QAEHH@Z",
      "cm_voiceIsConcatenative");
