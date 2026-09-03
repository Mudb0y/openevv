/* Which filters exist, which are loaded, and which are on.
 *
 * Two lists, and they are not the same list. The registry is what
 * `eciRegisterFilter' writes: a chain of records saying that a filter of
 * such a name, for such a language, under such a number, can be reached
 * through such an entry point. Nothing is loaded by registering. The
 * loaded list is what `eciNewFilter' writes: an actual filter object, got
 * by calling that entry point, with the version it reported beside it.
 *
 * So a number means two different things depending on which list is being
 * asked. In the registry it is the number the caller chose, one of twenty;
 * in the loaded list it is the same number, but a filter can be loaded
 * once per language and the pair is what identifies it.
 *
 * Three things in here are stubs in IBM's own build, not just in this one:
 * `getINIValue' answers nothing and `autoLoadFilter' does nothing, which
 * together are the whole of the path that would have found a filter in a
 * library and loaded it without being asked. That is why the SSML filter,
 * which is compiled into every language module, is never on unless a
 * caller registers it -- and why registering it by hand works, since the
 * rest of the path is all here.
 *
 * The environment is the other half of this file. A filter that says it is
 * version seven or better is handed one when it loads, and what it can ask
 * through it is the language, the voice and the volume the engine is
 * currently set to. The SSML reader needs all three: a percentage
 * loudness has to be a percentage of something, and an `xml:lang' has to
 * be compared against what is in force.
 *
 * Names are prefixed and the aliases at the foot carry the real ones.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "eci_synththread.h"
#include "eci_engine.h"
#include "evv_abi.h"
#include "eci_filter.h"

extern void *cpp_new(uint32_t n) MANGLED("??2@YAPAXI@Z");
extern void  cpp_delete(void *p) MANGLED("??3@YAXPAX@Z");
extern THIS int32_t sy_mutexWait(void *m, int32_t ms)
    MANGLED("?wait@Mutex@@QAEHJ@Z");
extern THIS int32_t sy_mutexRelease(void *m) MANGLED("?release@Mutex@@QAEHXZ");
extern THIS IniFileReader *ini_ctor(IniFileReader *r)
    MANGLED("??0IniFileReader@@QAE@XZ");
extern THIS void ini_dtor(IniFileReader *r) MANGLED("??1IniFileReader@@QAE@XZ");
extern THIS int32_t es_getParam(void *state, int32_t voice, int32_t key,
                                int32_t *out)
    MANGLED("?getParam@ECIstate@@QAEJJJPAJ@Z");

/* How many filters a caller may register, which is how many numbers there
   are rather than how many can be loaded. */
#define FILTER_ID_MAX 0x13
#define FILTER_ID_COUNT (FILTER_ID_MAX + 1)

/* How long a description the caller's buffer is taken to be. */
#define DESCRIPTION_ROOM 0x100

/* What a filter with no name registered under this number is called. */
#define UNKNOWN_FILTER_NAME "Unknown Filter"

/* From which version a filter is handed an environment. Both numbers are
   tested, so a six point seven gets one and a six point nought does
   not. */
#define ENV_FROM_MAJOR 6
#define ENV_FROM_MINOR 7
#define ENV_ALWAYS_MAJOR 7

/* Which of the state's own keys the two published parameter numbers mean.
   The names on the caller's side and the names inside the state were never
   the same numbers. */
#define ECIPARAM_LANGUAGE   9
#define ECIPARAM_VOICE      0x11
#define STATEKEY_LANGUAGE   2
#define STATEKEY_VOICE      0x10

/* And where the voice parameters start, which is a straight run of eight
   from here. */
#define STATEKEY_VOICE_FIRST 5
#define VOICEPARAM_LAST      7

/* Whether a key belongs to the voice or to the instance, which is the
   state's first argument. */
#define FOR_INSTANCE 0
#define FOR_VOICE    1

/* ---- what is registered ---------------------------------------------- */

/* One record per filter a caller has registered. The name and the language
   are the filter's own answers rather than anything the caller said: the
   manager asks the filter for both while registering and writes them back
   into the caller's structure. */
typedef struct FilterRegistryElement {
    struct FilterRegistryElement *next;
    char              name[80];
    int32_t           language;
    uint32_t          id;
    GetFilterObjectFn entry;
    char            **dependencies;
    int8_t            autoload;
} FilterRegistryElement;

/* And one per filter actually loaded. The version is asked for once, on
   the way in, because whether the filter gets an environment turns on it.
 *
 * The library slot is where IBM put the loader for a filter that came out
 * of a DLL of its own, so that unloading the filter could unload the
 * library with it. Nothing in this extraction can put one there: the only
 * path that loaded a library was `autoLoadFilter', which IBM's own build
 * leaves empty. */
typedef struct LoadedFilter {
    void    *library;
    int32_t  version[4];
    Filter  *filter;
    int32_t  language;
    uint32_t id;
    int8_t   wasActive;   /* it was on when everything was turned off */
} LoadedFilter;

typedef struct FilterManager {
    IniFileReader          ini;      /* it is one, and inherits from one */
    SynthThread           *thread;
    GetFilterObjectFn      entry;    /* whichever is being asked just now */
    int32_t                lastLoad;
    int32_t                unused;
    LoadedFilter         **loaded;
    int32_t                count;
    void                  *env;
    FilterRegistryElement *registry;
} FilterManager;

/* How much room one takes here. IBM's is 0x144 bytes; on a wider host the
   pointers in it are wider, so whoever makes one has to ask. */
const uint32_t fm_bytes = sizeof(FilterManager);

/* The lock the original takes while tearing a manager down, which is one
   for the class rather than one for the manager. src/port/port_ctors.c builds
   it at startup. */
int32_t fm_protectFilterLoad[3];

/* ---- the environment a filter is given ------------------------------- */

/* Twelve bytes: what it is, the thread it belongs to, and that thread's
   state, which is where every answer actually comes from. */
typedef struct IBMECIEnvironment {
    const void  *vt;
    SynthThread *thread;
    void        *state;
} IBMECIEnvironment;

extern const void *vtbl_ibmecienvironment[2];

THIS IBMECIEnvironment *env_ctor(IBMECIEnvironment *self, SynthThread *thread)
{
    self->vt     = &vtbl_ibmecienvironment;
    self->thread = 0;
    self->state  = 0;
    self->thread = thread;
    self->state  = ST_STATE(thread);
    return self;
}

THIS void env_dtor(IBMECIEnvironment *self)
{
    self->vt = &vtbl_ibmecienvironment;
}

/* Two of the published parameters can be asked for and no others, because
   those two are all any filter has ever wanted. */
THIS int32_t env_getParam(IBMECIEnvironment *self, int32_t which)
{
    int32_t key;
    int32_t value = 0;

    if (which == ECIPARAM_LANGUAGE)
        key = STATEKEY_LANGUAGE;
    else if (which == ECIPARAM_VOICE)
        key = STATEKEY_VOICE;
    else
        return -1;

    es_getParam(self->state, FOR_INSTANCE, key, &value);
    return value;
}

/* The voice parameters are a straight run, so the eight of them are the
   eight keys from five up. */
THIS int32_t env_getVoiceParam(IBMECIEnvironment *self, int32_t which)
{
    int32_t value = 0;

    if ((uint32_t)which > VOICEPARAM_LAST)
        return -1;

    es_getParam(self->state, FOR_VOICE, STATEKEY_VOICE_FIRST + which, &value);
    return value;
}

const void *vtbl_ibmecienvironment[2] = {
    (void *)env_getVoiceParam,
    (void *)env_getParam
};

/* Two the manager asks itself, out of order. */
THIS int32_t fm_isActiveById(FilterManager *self, uint32_t id);
static THIS int8_t fm_removeElement(FilterManager *self,
                                    FilterRegistryElement *which);

/* ---- the registry ---------------------------------------------------- */

/* A record at the end of the chain, or the first one if there is none.
   Only the link is set; whoever asked fills the rest in. */
static THIS FilterRegistryElement *fm_addNewElement(FilterManager *self)
{
    FilterRegistryElement *at = self->registry;
    FilterRegistryElement *made;

    if (at == 0) {
        self->registry = cpp_new(sizeof *made);
        self->registry->next = 0;
        return self->registry;
    }

    while (at->next != 0)
        at = at->next;

    made = cpp_new(sizeof *made);
    made->next = 0;
    at->next = made;
    return made;
}

/* The one registered for this language under this number. The walk does
   not stop when it finds one, so what comes back is the last match rather
   than the first; there can only be one, since registering a pair that is
   already there is refused. */
static THIS FilterRegistryElement *fm_findElement(FilterManager *self,
                                                  int32_t language,
                                                  int32_t id)
{
    FilterRegistryElement *at;
    FilterRegistryElement *found = 0;

    for (at = self->registry; at != 0; at = at->next)
        if (at->language == language && (int32_t)at->id == id)
            found = at;

    return found;
}

static THIS int8_t fm_removeElement(FilterManager *self,
                                    FilterRegistryElement *which)
{
    FilterRegistryElement *at;
    FilterRegistryElement *after;
    int8_t removed = 0;

    if (which == 0)
        return 0;

    after = which->next;

    if (self->registry == which) {
        cpp_delete(which);
        self->registry = after;
        return 1;
    }

    at = self->registry;
    while (at->next != which && at->next != 0)
        at = at->next;

    if (at->next == which) {
        cpp_delete(which);
        removed = 1;
    }
    at->next = after;

    return removed;
}

/* ---- making and unmaking a manager ----------------------------------- */

THIS FilterManager *fm_ctor(FilterManager *self, SynthThread *thread)
{
    void *env;

    ini_ctor(&self->ini);

    self->unused   = 0;
    self->loaded   = 0;
    self->count    = 0;
    self->env      = 0;
    self->thread   = thread;

    env = cpp_new(sizeof(IBMECIEnvironment));
    self->env      = env ? env_ctor(env, thread) : 0;
    self->registry = 0;

    return self;
}

/* Every loaded filter is told to delete itself and the array goes. The
   registry is not touched, which is the original's: a record in it names
   an entry point in somebody else's library and owns nothing. */
THIS void fm_dtor(FilterManager *self)
{
    void   *lock = fm_protectFilterLoad;
    int32_t i;

    sy_mutexWait(lock, -1);

    for (i = 0; i < self->count; i++) {
        LoadedFilter *lf = self->loaded[i];

        if (lf == 0)
            continue;
        if (lf->filter != 0) {
            lf->filter->vt->deleteFilter(lf->filter);
            lf->filter = 0;
        }
        cpp_delete(lf);
        self->loaded[i] = 0;
    }

    free(self->loaded);
    self->loaded = 0;

    if (self->env != 0) {
        env_dtor(self->env);
        cpp_delete(self->env);
        self->env = 0;
    }

    sy_mutexRelease(lock);
    ini_dtor(&self->ini);
}

/* ---- registering and unregistering ----------------------------------- */

/* The caller hands over a number of its own choosing and a pointer to its
   entry point, and gets back the filter's name and language written into
   its own structure -- which it has to, since the manager keys on the
   language and the caller has no way of knowing what the filter will say.
 *
 * A filter is made and immediately deleted here. That is the only way to
 * ask it anything: what is registered is an entry point, and the questions
 * are methods on an object. The original checks its arguments twice over,
 * to the same effect both times. */
THIS int32_t fm_registerFilter(FilterManager *self, ECIFilterAttrib *attrib,
                               uint32_t id, GetFilterObjectFn *entry,
                               int8_t autoload)
{
    int32_t                error = FILTER_REFUSED;
    void                  *object = 0;
    Filter                *filter;
    FilterRegistryElement *made;

    if (entry == 0 || *entry == 0 || id > FILTER_ID_MAX)
        return error;

    self->entry = *entry;
    self->entry(FILTER_INTERFACE_OBJECT, &object);
    if (object == 0)
        return error;

    filter = object;
    attrib->language = filter->vt->getFilterLanguage(filter);
    strcpy(attrib->eciFilterName, filter->vt->getFilterDescription(filter));

    if (attrib == 0 || id > FILTER_ID_MAX || *entry == 0)
        return error;

    if (fm_findElement(self, attrib->language, (int32_t)id) != 0)
        return FILTER_NOT_REGISTERED;

    made = fm_addNewElement(self);
    strcpy(made->name, filter->vt->getFilterDescription(filter));
    made->language     = filter->vt->getFilterLanguage(filter);
    made->dependencies = filter->vt->getFilterDependencies(filter);
    made->id           = id;
    made->autoload     = autoload;
    made->entry        = *entry;
    strcpy(attrib->eciFilterName, made->name);

    filter->vt->deleteFilter(filter);
    return FILTER_OK;
}

/* Taking a registration away, which is refused while anything is using
   it: a filter of that number that is loaded, or one that is on. */
THIS int32_t fm_unregisterFilter(FilterManager *self, ECIFilterAttrib *attrib,
                                 uint32_t id)
{
    int32_t                error = FILTER_REFUSED;
    FilterRegistryElement *found;
    int32_t                i;

    if (id > FILTER_ID_MAX || attrib == 0)
        return error;

    found = fm_findElement(self, attrib->language, (int32_t)id);
    if (found == 0)
        return FILTER_NOT_REGISTERED;

    for (i = 0; i < self->count; i++)
        if (self->loaded[i]->id == id && self->loaded[i]->filter != 0)
            return error;

    if (fm_isActiveById(self, id) != 1)
        if (fm_removeElement(self, found))
            error = FILTER_OK;

    return error;
}

/* ---- loading one ----------------------------------------------------- */

/* The entry point is called and what comes back is remembered, with the
   version it reports and the language it was loaded for. A filter already
   loaded for that pair is handed back rather than made again.
 *
 * The array grows by one each time, which is what the original does: a
 * realloc when there is one already and a malloc of a single slot when
 * there is not. */
THIS int32_t fm_loadFilter(FilterManager *self, int32_t language, int32_t id,
                           void **out)
{
    int32_t                rc = FILTER_NOT_FOUND;
    int8_t                  found = 0;
    FilterRegistryElement *element;
    LoadedFilter          *made;
    int32_t                i;

    if (out == 0)
        return rc;

    for (i = 0; i < self->count; i++) {
        LoadedFilter *lf = self->loaded ? self->loaded[i] : 0;

        if (lf == 0)
            continue;
        if (lf->language != language || (int32_t)lf->id != id)
            continue;
        *out  = lf->filter;
        found = 1;
        rc    = FILTER_OK;
        break;
    }

    if (found)
        return rc;

    element = fm_findElement(self, language, id);
    if (element == 0)
        return rc;

    self->entry = element->entry;
    if (self->entry == 0)
        return rc;

    self->entry(FILTER_INTERFACE_OBJECT, out);
    if (*out == 0)
        return rc;

    made = cpp_new(sizeof *made);
    if (made == 0)
        return rc;

    memset(made, 0, sizeof *made);
    made->library  = 0;
    made->filter   = *out;
    made->language = language;
    made->id       = (uint32_t)id;
    self->lastLoad = 0;

    made->filter->vt->getFilterVersion(made->filter, made->version);

    if ((made->version[0] >= ENV_FROM_MAJOR
         && made->version[1] >= ENV_FROM_MINOR)
        || made->version[0] >= ENV_ALWAYS_MAJOR)
        made->filter->vt->setEnvironment(made->filter, self->env);

    if (self->loaded == 0)
        self->loaded = malloc(sizeof *self->loaded);
    else
        self->loaded = realloc(self->loaded,
                               (size_t)(self->count + 1)
                                   * sizeof *self->loaded);
    if (self->loaded == 0)
        return rc;

    self->loaded[self->count] = made;
    self->count++;
    return FILTER_OK;
}

/* ---- turning them on and off ----------------------------------------- */

/* By number, for whichever language is in force. A filter registered
   against no language at all is global and is turned on whenever the
   caller says so.
 *
 * What comes back is always the refusal, whether or not anything was
 * turned on. That is the original's, and it is why `eciActivateFilter'
 * reports a failure over a filter that is now working. */
THIS int32_t fm_activateById(FilterManager *self, uint32_t id, int8_t global)
{
    LangIdentifier lang = self->thread->lang;
    int32_t        i;

    for (i = 0; i < self->count; i++) {
        LoadedFilter *lf = self->loaded ? self->loaded[i] : 0;

        if (lf == 0 || lf->id != id)
            continue;

        if (lf->language == 0 && global) {
            if (lf->filter != 0)
                lf->filter->vt->activateFilter(lf->filter);
            continue;
        }

        if (lf->language != lang.packed || lf->filter == 0)
            continue;

        lf->filter->vt->activateFilter(lf->filter);
    }

    return FILTER_NOT_FOUND;
}

THIS int32_t fm_activateByHandle(FilterManager *self, void *handle)
{
    int32_t rc = FILTER_NOT_FOUND;
    int32_t i;

    if (self->loaded == 0 || handle == 0)
        return rc;

    for (i = 0; i < self->count; i++) {
        LoadedFilter *lf = self->loaded[i];

        if (lf == 0 || lf->filter != handle)
            continue;

        ((Filter *)handle)->vt->activateFilter(handle);
        rc = FILTER_OK;
        break;
    }

    return rc;
}

/* The same by number, and unlike activating, this one stops at the first
   it turned off. */
THIS int32_t fm_deactivateById(FilterManager *self, uint32_t id,
                               int8_t global)
{
    LangIdentifier lang = self->thread->lang;
    int32_t        i;

    for (i = 0; i < self->count; i++) {
        LoadedFilter *lf = self->loaded ? self->loaded[i] : 0;

        if (lf == 0 || lf->id != id)
            continue;

        if (lf->language == 0 && global) {
            if (lf->filter != 0) {
                lf->filter->vt->deactivateFilter(lf->filter);
                break;
            }
            continue;
        }

        if (lf->language != lang.packed || lf->filter == 0)
            continue;

        lf->filter->vt->deactivateFilter(lf->filter);
        break;
    }

    return FILTER_NOT_FOUND;
}

THIS int32_t fm_deactivateByHandle(FilterManager *self, void *handle)
{
    int32_t rc = FILTER_NOT_FOUND;
    int32_t i;

    if (self->loaded == 0 || handle == 0)
        return rc;

    for (i = 0; i < self->count; i++) {
        LoadedFilter *lf = self->loaded[i];

        if (lf == 0 || lf->filter != handle)
            continue;

        ((Filter *)handle)->vt->deactivateFilter(handle);
        rc = FILTER_OK;
        break;
    }

    return rc;
}

/* Everything belonging to a language goes off, and which of them was on is
   remembered -- the flag is written and nothing in this extraction reads
   it, which is the original's too. The global ones are left alone. */
THIS int32_t fm_deactivateAll(FilterManager *self)
{
    int32_t i;

    for (i = 0; i < self->count; i++) {
        LoadedFilter *lf = self->loaded ? self->loaded[i] : 0;

        if (lf == 0 || lf->language == 0 || lf->filter == 0)
            continue;

        if (lf->filter->vt->isActive(lf->filter))
            lf->wasActive = 1;

        lf->filter->vt->deactivateFilter(lf->filter);
    }

    return FILTER_OK;
}

/* ---- unloading ------------------------------------------------------- */

/* One loaded filter goes: it is told to delete itself, its record goes,
   and the array is squeezed up. The array is made one shorter and the
   surviving records copied across, so the last one to go leaves no array
   at all. */
THIS int32_t fm_deleteByHandle(FilterManager *self, void *handle)
{
    LoadedFilter **smaller = 0;
    int32_t        i;
    int32_t        kept;

    if (self->loaded == 0)
        return FILTER_OK;

    if (self->count > 1)
        smaller = malloc((size_t)self->count * sizeof *smaller);

    if (handle != 0)
        for (i = 0; i < self->count; i++) {
            LoadedFilter *lf = self->loaded[i];

            if (lf->filter != handle)
                continue;

            /* Where a library would have been let go. See LoadedFilter. */
            lf->library = 0;

            if (lf->filter != 0) {
                lf->filter->vt->deleteFilter(lf->filter);
                lf->filter = 0;
            }
            cpp_delete(lf);
            self->loaded[i] = 0;
            break;
        }

    if (smaller != 0) {
        kept = 0;
        for (i = 0; i < self->count; i++)
            if (self->loaded[i] != 0)
                smaller[kept++] = self->loaded[i];
    }

    free(self->loaded);
    self->loaded = smaller;
    self->count--;

    return FILTER_OK;
}

/* And by language and number, which finds the handle and does the
   above. */
THIS int32_t fm_deleteById(FilterManager *self, int32_t language, int32_t id)
{
    int32_t rc = FILTER_OK;
    int32_t i;

    for (i = 0; i < self->count; i++) {
        LoadedFilter *lf = self->loaded ? self->loaded[i] : 0;

        if (lf == 0 || lf->filter == 0)
            continue;
        if ((int32_t)lf->id != id || lf->language != language)
            continue;

        rc = fm_deleteByHandle(self, lf->filter);
    }

    return rc;
}

/* ---- filtering ------------------------------------------------------- */

/* One named filter, asked whether it is on or not: `eciGetFilteredText' is
   how a caller sees what a filter would do to a string without turning it
   on, so the insistent flag is set here. */
THIS char *fm_filterTextByHandle(FilterManager *self, void *handle,
                                 const char *text)
{
    char *out = 0;

    (void)self;

    if (handle == 0)
        return (char *)text;

    ((Filter *)handle)->vt->filterText(handle, text, &out, 1);
    return out;
}

/* And the chain: every filter that is on and belongs to this language, in
   the order they were loaded, each handed what the one before it
   produced. */
THIS char *fm_filterText(FilterManager *self, const char *text,
                         int32_t language)
{
    char *result = (char *)text;
    char *at     = (char *)text;
    int32_t i;

    if (text == 0 || self->loaded == 0)
        return result;

    for (i = 0; i < self->count; i++) {
        LoadedFilter *lf = self->loaded[i];

        if (lf == 0 || lf->filter == 0)
            continue;
        if (!lf->filter->vt->isActive(lf->filter))
            continue;
        if (lf->language != language)
            continue;

        lf->filter->vt->filterText(lf->filter, at, &result, 0);
        at = result;
    }

    return result;
}

/* ---- what a caller can ask ------------------------------------------- */

/* Which numbers this language has something registered under, up to as
   many as the caller left room for. */
THIS void fm_getAvailableFilters(FilterManager *self, int32_t language,
                                 uint32_t *ids, uint32_t *count)
{
    uint32_t found = 0;
    int32_t  id;

    for (id = 0; id < FILTER_ID_COUNT; id++) {
        if (fm_findElement(self, language, id) == 0)
            continue;
        if (found >= *count)
            break;
        ids[found++] = (uint32_t)id;
    }

    *count = found;
}

THIS char **fm_getFilterDependencies(FilterManager *self, int32_t language,
                                     uint32_t id)
{
    FilterRegistryElement *found = fm_findElement(self, language,
                                                  (int32_t)id);

    return found ? found->dependencies : 0;
}

/* The name, which is how a caller tells one filter from another: the
   number is its own choice and says nothing. */
THIS void fm_getFilterDescription(FilterManager *self, int32_t language,
                                  uint32_t id, char *out)
{
    FilterRegistryElement *found = fm_findElement(self, language,
                                                  (int32_t)id);
    char                  *name = 0;

    if (found != 0) {
        name = cpp_new((uint32_t)strlen(found->name) + 1);
        strcpy(name, found->name);
    }

    if (name != 0) {
        strncpy(out, name, DESCRIPTION_ROOM);
        cpp_delete(name);
    } else {
        strcpy(out, UNKNOWN_FILTER_NAME);
    }
}

THIS int32_t fm_isActiveById(FilterManager *self, uint32_t id)
{
    int32_t i;

    for (i = 0; i < self->count; i++) {
        LoadedFilter *lf = self->loaded ? self->loaded[i] : 0;

        if (lf == 0 || lf->filter == 0 || lf->id != id)
            continue;
        return lf->filter->vt->isActive(lf->filter);
    }

    return 0;
}

THIS int32_t fm_isActive(FilterManager *self, int32_t language, uint32_t id)
{
    int32_t i;

    for (i = 0; i < self->count; i++) {
        LoadedFilter *lf = self->loaded ? self->loaded[i] : 0;

        if (lf == 0 || lf->filter == 0)
            continue;
        if (lf->id != id || lf->language != language)
            continue;
        return lf->filter->vt->isActive(lf->filter);
    }

    return 0;
}

THIS int32_t fm_isAutoload(FilterManager *self, int32_t language, uint32_t id)
{
    FilterRegistryElement *found = fm_findElement(self, language,
                                                  (int32_t)id);

    return found ? found->autoload : 0;
}

/* Whether a filter would want this string, asked of the filter's own entry
   point rather than of anything loaded -- which is the point: it is how the
   engine decides whether to load one at all. */
THIS int32_t fm_isUsable(FilterManager *self, const char *text,
                         int32_t language, uint32_t id)
{
    FilterRegistryElement *found = fm_findElement(self, language,
                                                  (int32_t)id);
    GetFilterObjectFn      entry;
    FilterUsableFn         usable = 0;

    if (found == 0)
        return 0;

    entry = found->entry;
    entry(FILTER_INTERFACE_USABLE, (void **)&usable);
    if (usable == 0)
        return 0;

    return usable(text);
}

THIS int32_t fm_updateFilter(FilterManager *self, void *handle, void *a,
                             int32_t b, void *c, int32_t d)
{
    int32_t rc = FILTER_REFUSED;

    (void)self; (void)d;

    if (b != 0 && handle != 0)
        rc = ((Filter *)handle)->vt->updateFilter(handle, a, c);

    return rc;
}

/* ---- the path IBM left empty ----------------------------------------- */

/* A filter named in the settings, loaded without being asked. Both of
   these are empty in IBM's own objects, not just here: there is no reading
   of the settings and no loading. Registering a filter by hand is
   therefore the only way one is ever turned on. */
static THIS const char *fm_getINIValue(FilterManager *self,
                                       const char *section, const char *key)
{
    (void)self; (void)section; (void)key;
    return 0;
}

THIS void fm_autoLoadFilter(FilterManager *self, LangIdentifier *language)
{
    (void)self; (void)language;
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
ALIAS("?fr_AddNewElement@FilterManager@@AAEPAUFilterRegistryElement@@XZ",
      "fm_addNewElement");
ALIAS("?fr_FindElement@FilterManager@@AAEPAUFilterRegistryElement@@JJ@Z",
      "fm_findElement");
ALIAS("?fr_RemoveElement@FilterManager@@AAE_NPAUFilterRegistryElement@@@Z",
      "fm_removeElement");
ALIAS("?getAvailableFilters@FilterManager@@QAEXJPAI0@Z",
      "fm_getAvailableFilters");
ALIAS("?getFilterDependencies@FilterManager@@QAEPAPADJI@Z",
      "fm_getFilterDependencies");
ALIAS("?getFilterDescription@FilterManager@@QAEXJIPAD@Z",
      "fm_getFilterDescription");
ALIAS("?getINIValue@FilterManager@@AAEPBDPBD0@Z", "fm_getINIValue");
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
ALIAS("??0IBMECIEnvironment@@QAE@PAVSynthThread@@@Z", "env_ctor");
ALIAS("??1IBMECIEnvironment@@QAE@XZ", "env_dtor");
ALIAS("?getParam@IBMECIEnvironment@@UAEHW4ECIParam@@@Z", "env_getParam");
ALIAS("?getVoiceParam@IBMECIEnvironment@@UAEHW4ECIVoiceParam@@@Z",
      "env_getVoiceParam");
ALIAS("??_7IBMECIEnvironment@@6B@", "vtbl_ibmecienvironment");
