/* The one filter IBM shipped: SSML in, the engine's own annotations out.
 *
 * It owns three things -- the reader's state, the XML parser, and a copy of
 * the last answer -- and does very little itself. `filterText' is what the
 * manager calls: it resets the state, hands the text to `filterSSMLText',
 * which drives the parser, and takes a copy of what the reader built so
 * that the caller has something that outlives the next call.
 *
 * A lock is held across the whole of that, and has to be. The scanner in
 * src/eci_xmltok.c keeps its state in variables of its own rather than in
 * the parser, so two threads filtering at once would read each other's
 * document. IBM's lock is one for the class, not one for the object,
 * which is the same reasoning.
 *
 * Names are prefixed and the aliases at the foot carry the real ones.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "evv_abi.h"
#include "eci_filter.h"
#include "eci_parserxml.h"

extern void *cpp_new(uint32_t n) MANGLED("??2@YAPAXI@Z");
extern void  cpp_delete(void *p) MANGLED("??3@YAXPAX@Z");
extern THIS int32_t sy_mutexWait(void *m, int32_t ms)
    MANGLED("?wait@Mutex@@QAEHJ@Z");
extern THIS int32_t sy_mutexRelease(void *m) MANGLED("?release@Mutex@@QAEHXZ");

extern void  *CreateSSMLState(void);
extern void   DeleteSSMLState(void *state);
extern char  *GetProcessedText(void *state);
extern int8_t ResetSSMLState(void *state);
extern void   ResetSSMLStateText(void *state);
extern void   SetSSMLStateEnv(void *state, void *env);
extern void   OpenTagHandler(void *data, const char *name, const char **atts);
extern void   CloseTagHandler(void *data, const char *name);
extern void   CharDataHandler(void *data, char *text, int32_t length);

extern THIS Filter *filter_ctor(Filter *self) MANGLED("??0Filter@@QAE@XZ");
extern THIS void filter_dtor(Filter *self) MANGLED("??1Filter@@QAE@XZ");
extern THIS int32_t filter_activateFilter(Filter *self)
    MANGLED("?activateFilter@Filter@@UAE?AW4ECIFilterError@@XZ");
extern THIS int32_t filter_deactivateFilter(Filter *self)
    MANGLED("?deactivateFilter@Filter@@UAE?AW4ECIFilterError@@XZ");
extern THIS int8_t filter_isActive(Filter *self)
    MANGLED("?isActive@Filter@@UAE_NXZ");
extern THIS int32_t filter_getFilterLanguage(Filter *self)
    MANGLED("?getFilterLanguage@Filter@@UAE?AW4ECILanguageDialect@@XZ");

typedef struct {
    Filter   base;
    int32_t  lastError;
    void    *state;
    char    *result;   /* the answer, kept until the next call */
    void    *parser;
} SSMLFilter;

/* What it calls itself, which is the name a caller registering it gets
   back and the name the manager keys its description on. */
#define SSML_FILTER_NAME "IBM SSML Filter"

/* How long the version is, and what it is. Only the first two numbers are
   ever looked at: the manager hands a filter the environment when it says
   seven or better, which is what lets an older filter go without one. */
#define FILTER_VERSION_LONGS 4
int32_t g_aiFilterVersion[FILTER_VERSION_LONGS] = { 7, 0, 0, 0 };

/* Enough room for the empty answer, which is what a document that said
   nothing gets. */
#define EMPTY_ANSWER_ROOM 0xa

/* One lock for every filter of this kind, because what it protects is the
   scanner, which there is only one of. It is constructed at startup by
   src/port_ctors.c, along with the others the original's compiler would
   have run an initialiser for. */
int32_t ssml_lexerMutex[3];

extern const FilterVtbl vtbl_ssmlfilter;

static THIS void ssf_initialize(SSMLFilter *self);

/* ---- making and unmaking one ----------------------------------------- */

THIS SSMLFilter *ssf_ctor(SSMLFilter *self)
{
    filter_ctor(&self->base);
    self->base.vt   = &vtbl_ssmlfilter;
    self->result    = 0;
    self->parser    = 0;
    self->state     = CreateSSMLState();
    if (self->state != 0)
        ssf_initialize(self);
    return self;
}

THIS void ssf_dtor(SSMLFilter *self)
{
    self->base.vt = &vtbl_ssmlfilter;

    if (self->state != 0)
        DeleteSSMLState(self->state);
    if (self->result != 0) {
        cpp_delete(self->result);
        self->result = 0;
    }
    if (self->parser != 0) {
        DeleteParserXML(self->parser);
        self->parser = 0;
    }

    filter_dtor(&self->base);
}

/* A parser of its own, with the three handlers and the reader's state
   registered on it. Called once when the filter is made, and again by
   nothing: a filter that could not get a parser stays without one and
   reports that on every call. */
static THIS void ssf_initialize(SSMLFilter *self)
{
    if (self->result != 0) {
        cpp_delete(self->result);
        self->result = 0;
    }
    if (self->parser != 0)
        DeleteParserXML(self->parser);

    self->lastError = FILTER_OK;
    self->parser    = CreateParserXML();
    if (self->parser == 0)
        return;

    px_setTagHandler(self->parser, OpenTagHandler, CloseTagHandler);
    px_setTextHandler(self->parser, CharDataHandler);
    px_setData(self->parser, self->state);
}

THIS int32_t ssf_deleteFilter(SSMLFilter *self)
{
    if (self != 0) {
        ssf_dtor(self);
        cpp_delete(self);
    }
    return FILTER_OK;
}

/* ---- what it says about itself --------------------------------------- */

THIS char *ssf_getFilterDescription(SSMLFilter *self)
{
    (void)self;
    return (char *)SSML_FILTER_NAME;
}

THIS int32_t ssf_getFilterVersion(SSMLFilter *self, int32_t *out)
{
    int32_t i;

    (void)self;
    for (i = 0; i < FILTER_VERSION_LONGS; i++)
        out[i] = g_aiFilterVersion[i];
    return 1;
}

/* No filter has to be loaded before this one. */
THIS char **ssf_getFilterDependencies(SSMLFilter *self)
{
    (void)self;
    return 0;
}

THIS int32_t ssf_updateFilter(SSMLFilter *self, const char *a, const char *b)
{
    (void)self; (void)a; (void)b;
    return FILTER_OK;
}

THIS void ssf_setEnvironment(SSMLFilter *self, void *env)
{
    SetSSMLStateEnv(self->state, env);
}

/* ---- reading a document ---------------------------------------------- */

/* The parser run: put the reader's answer back to empty, drive the
   scanner over the text, and hand back what the handlers built. What
   comes back belongs to the reader and is only good until the next
   call. */
THIS char *ssf_filterSSMLText(SSMLFilter *self, const char *text,
                              int32_t length)
{
    if (text == 0)
        return 0;

    if (self->parser == 0) {
        self->lastError = FILTER_NO_PARSER;
        return 0;
    }

    ResetSSMLStateText(self->state);
    px_parseText(self->parser, text, length, 0, 1);
    return GetProcessedText(self->state);
}

/* And the call the manager makes. A filter that is off passes the text
   through untouched unless the caller insists, which is what `force' is
   for -- `eciGetFilteredText' asks that way, so that a caller can see what
   the filter would do without turning it on.
 *
 * What is handed back is a copy of the reader's answer rather than the
 * answer itself, so that it survives the next document. The copy is the
 * filter's and is freed by the call after it. */
THIS int32_t ssf_filterText(SSMLFilter *self, const char *text, char **out,
                            int8_t force)
{
    self->lastError = FILTER_OK;

    if ((filter_isActive(&self->base) || force) && text != 0) {
        void *lock = ssml_lexerMutex;

        sy_mutexWait(lock, -1);

        if (ResetSSMLState(self->state)) {
            char *answer = ssf_filterSSMLText(self, text,
                                              (int32_t)strlen(text));

            if (self->result != 0) {
                cpp_delete(self->result);
                self->result = 0;
            }

            if (answer != 0) {
                self->result = cpp_new((uint32_t)strlen(answer) + 1);
                strcpy(self->result, answer);
            } else {
                self->result = cpp_new(EMPTY_ANSWER_ROOM);
                strcpy(self->result, "");
            }

            *out = self->result;
        } else {
            self->lastError = FILTER_NO_STATE;
        }

        sy_mutexRelease(lock);
    } else if (text != 0) {
        *out = (char *)text;
    } else {
        out = 0;
    }

    return self->lastError;
}

/* ---- what a caller registering this reaches it through --------------- */

/* Whether a string is worth handing to this filter at all. Two markers are
   looked for anywhere in it, not only at the front, so text with a
   document buried in it is still offered. */
int isSSMLFilterUsable(const char *text)
{
    if (strstr(text, "<?xml") != 0 || strstr(text, "<speak") != 0)
        return 1;
    return 0;
}

/* The published entry point: number seven asks for a filter, number eight
   for the test above. Anything else is answered with nothing.
 *
 * Number one is accepted as well as seven and gives the same thing, which
 * is IBM's and is kept: an older header numbered the filter interface one
 * and the engine still answers to it. */
STDCALL int ssml_getFilterObject(uint32_t idInterface, void **out)
{
    void *object = 0;

    if (idInterface == FILTER_INTERFACE_USABLE) {
        *out = (void *)isSSMLFilterUsable;
        return 1;
    }

    if (idInterface == 1 || idInterface == FILTER_INTERFACE_OBJECT) {
        SSMLFilter *filter = cpp_new(sizeof *filter);

        if (filter != 0)
            object = ssf_ctor(filter);
        if (object != 0)
            *out = object;
    }

    /* What is answered is whether the slot holds something, not whether
       this call put it there -- so the slot has to arrive empty. Every
       caller in this tree clears it first; the original leaves that to
       whoever asks and would report success on a stale pointer. */
    return *out != 0;
}

const FilterVtbl vtbl_ssmlfilter = {
    (THIS int32_t (*)(Filter *, const char *, char **, int8_t))
        ssf_filterText,
    filter_activateFilter,
    filter_deactivateFilter,
    (THIS int32_t (*)(Filter *))ssf_deleteFilter,
    filter_isActive,
    (THIS int32_t (*)(Filter *, const char *, const char *))ssf_updateFilter,
    (THIS void (*)(Filter *, void *))ssf_setEnvironment,
    (THIS char *(*)(Filter *))ssf_getFilterDescription,
    filter_getFilterLanguage,
    (THIS int32_t (*)(Filter *, int32_t *))ssf_getFilterVersion,
    (THIS char **(*)(Filter *))ssf_getFilterDependencies,
    (THIS char *(*)(Filter *, const char *, int32_t))ssf_filterSSMLText
};

ALIAS("??_7SSMLFilter@@6B@", "vtbl_ssmlfilter");
ALIAS("??0SSMLFilter@@QAE@XZ", "ssf_ctor");
ALIAS("??1SSMLFilter@@QAE@XZ", "ssf_dtor");
ALIAS("?initialize@SSMLFilter@@AAEXXZ", "ssf_initialize");
ALIAS("?filterText@SSMLFilter@@UAE?AW4ECIFilterError@@PBDPAPAD_N@Z",
      "ssf_filterText");
ALIAS("?filterSSMLText@SSMLFilter@@UAEPADPBDH@Z", "ssf_filterSSMLText");
ALIAS("?deleteFilter@SSMLFilter@@UAE?AW4ECIFilterError@@XZ",
      "ssf_deleteFilter");
ALIAS("?updateFilter@SSMLFilter@@UAE?AW4ECIFilterError@@PBD0@Z",
      "ssf_updateFilter");
ALIAS("?setEnvironment@SSMLFilter@@UAEXPAVECIEnvironment@@@Z",
      "ssf_setEnvironment");
ALIAS("?getFilterDescription@SSMLFilter@@UAEPADXZ",
      "ssf_getFilterDescription");
ALIAS("?getFilterVersion@SSMLFilter@@UAEHQAJ@Z", "ssf_getFilterVersion");
ALIAS("?getFilterDependencies@SSMLFilter@@UAEPAPADXZ",
      "ssf_getFilterDependencies");
ALIAS("_ssmlFilterGetObject", "ssml_getFilterObject");
ALIAS("_isSSMLFilterUsable", "isSSMLFilterUsable");
ALIAS("_g_aiFilterVersion", "g_aiFilterVersion");
ALIAS("?lexerMutex@SSMLFilter@@1VMutex@@A", "ssml_lexerMutex");
