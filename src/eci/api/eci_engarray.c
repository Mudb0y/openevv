/* One engine per language, made on demand and kept in the list.
 *
 * EngineArray is an EngineList with a second settings reader of its own,
 * bolted on after the base. Asking it for an engine asks the list first,
 * and only builds one when the list has nothing.
 *
 * Building one is really a trial: make the wrapper, start it, and if
 * starting reports anything at all, close it, drop it and keep nothing.
 * What survives is an engine that has already proved it can start.
 *
 * Only one library name is ever answered, and only for one language. This
 * build has the engine linked in rather than loaded, so the name is a
 * formality; it is still allocated and freed the way a real one would be.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "eci_synththread.h"
#include "evv_abi.h"
#include "eci_engine.h"

/* Embedded in both the list and the array, so its size has to be right. */

/* What the list keeps in a slot. The callback word is never set here: the
   base leaves it alone and so does this, which is the original's doing. */
typedef struct EngineData {
    const void *vt;         /* +0x00 */
    uint32_t    callbacks;  /* +0x04 */
    int32_t     unused_08;
    void       *engine;     /* +0x0c, the wrapper, once it has proved itself */
    int (*factory)(int32_t kind, void **out);  /* +0x10 */
    int32_t     unused_14;
} EngineData;


/* Which kind of object the factory is asked for. */
#define OBJ_ENGINE 2

/* Slots of the engine's own table, which is stdcall with the object pushed
   like any other argument. */
#define VS_RELEASE 2
#define VS_START   3
#define VS_CLOSE   26

typedef STDCALL int32_t (*EngineFn)(void *self);

#define ENGINE_CALL(p, slot) (((EngineFn *)(*(void ***)(p)))[(slot)](p))

/* How anything in a slot is told to delete itself: through slot nought of
   its own table, with the object in ecx and "yes, free it" pushed. */
typedef THIS void *(*DeleteFn)(void *self, int32_t freeIt);
#define DELETE_ITSELF(p) ((*(DeleteFn *)(*(void ***)(p)))((p), 1))

/* What the ini calls the two version strings. */
#define KEY_CORPORA       "Corpora"
#define KEY_CONCATENATIVE "Concatenative"

/* What this build calls the engine linked into it, per language. The
   original spells one name and one number into getLibraryName -- "Static
   Engine ENU" and 0x10000 in the English module, "Static Engine DEU" and
   0x40000 in the German one -- because a library was one language. Here
   there may be several, so the answer is looked up. */
#include "delta_lang.h"

/* What a slot reports about its callbacks when nothing says otherwise. */
#define CALLBACK_DEFAULT 0x3f

#define LANG_PACKED(l) (*(const int32_t *)(l))
#define LANG_TEXT(l)   ((const char *)(l) + 4)

extern void *cpp_new(uint32_t n) MANGLED("??2@YAPAXI@Z");
extern void  cpp_delete(void *p) MANGLED("??3@YAXPAX@Z");

extern int getObject(int32_t kind, void **out);

extern THIS char *ini_getString(IniFileReader *r, const char *section,
                                const char *key)
    MANGLED("?getString@IniFileReader@@QAEPBDPBD0@Z");
extern THIS void *eng_getData(EngineList *el, const void *lang)
    MANGLED("?getData@EngineList@@QAEPAVEngineListData@@QBVLangIdentifier@@@Z");
extern THIS int32_t eng_setData(EngineList *el, const void *lang, void *data)
    MANGLED("?setData@EngineList@@QAEHQBVLangIdentifier@@PAVEngineListData@@@Z");
extern THIS void eng_removeData(EngineList *el, const void *lang)
    MANGLED("?removeData@EngineList@@QAEXQBVLangIdentifier@@@Z");

extern const void *vtbl_enginelistdata[1];
extern const void *vtbl_enginedata[1];

THIS void        *ed_deleteItself(EngineData *e, int32_t freeIt);
THIS EngineData  *ea_getEngineData(EngineArray *a, const void *lang);
THIS const char  *ea_getLibraryName(EngineArray *a, const void *lang);

/* Make one, try it, and keep it only if it started without complaint. The
   name is taken but not used: this build has the engine linked in. */
THIS EngineData *ed_ctor(EngineData *e, const char *name)
{
    (void)name;

    e->vt        = &vtbl_enginelistdata;
    e->vt        = &vtbl_enginedata;
    e->unused_08 = 0;
    e->engine    = 0;
    e->factory   = 0;
    e->unused_14 = 0;

    e->factory = getObject;
    if (e->factory != 0)
        e->factory(OBJ_ENGINE, &e->engine);

    if (e->engine != 0) {
        if (ENGINE_CALL(e->engine, VS_START)) {
            ENGINE_CALL(e->engine, VS_CLOSE);
            ENGINE_CALL(e->engine, VS_RELEASE);
            e->engine  = 0;
            e->factory = 0;
        }
    }

    return e;
}

THIS void ed_dtor(EngineData *e)
{
    e->vt = &vtbl_enginedata;

    if (e->engine != 0) {
        ENGINE_CALL(e->engine, VS_CLOSE);
        ENGINE_CALL(e->engine, VS_RELEASE);
        e->engine = 0;
    }
    e->factory = 0;

    e->vt = &vtbl_enginelistdata;
}

THIS void *ed_deleteItself(EngineData *e, int32_t freeIt)
{
    ed_dtor(e);
    if (freeIt & 1)
        cpp_delete(e);
    return e;
}

/* Only one language has a library, and the caller frees what comes back.
   When the language is not that one the buffer is still allocated and then
   simply dropped, which is what the original does. */
THIS const char *ea_getLibraryName(EngineArray *a, const void *lang)
{
    const delta_language *l = delta_lang_by_id(LANG_PACKED(lang));
    char *p;

    (void)a;
    if (l == 0)
        return 0;

    p = cpp_new((uint32_t)strlen(l->library_name) + 1);
    if (p == 0)
        return 0;

    strcpy(p, l->library_name);
    return p;
}

/* Ask the list; if it has nothing, build one and give it to the list to
   hold. If the list will not take it, it deletes itself. */
THIS EngineData *ea_getEngineData(EngineArray *a, const void *lang)
{
    EngineData *d = eng_getData(&a->base, lang);

    if (d == 0) {
        const char *name = ea_getLibraryName(a, lang);

        if (name != 0) {
            void *p = cpp_new(sizeof(EngineData));
            /* Everything the engine builds under here -- the machine, the
               tables it is handed, the rules that start it -- belongs to
               one language, and this is the only place that knows which
               before there is a machine to ask. */
            const delta_language *was =
                delta_lang_set(delta_lang_by_id(LANG_PACKED(lang)));

            d = p ? ed_ctor(p, name) : 0;
            cpp_delete((void *)name);

            if (d != 0 && !eng_setData(&a->base, lang, d)) {
                DELETE_ITSELF(d);
                d = 0;
            }
            delta_lang_set(was);
        }
    }

    return d;
}

THIS void *ea_getEngine(EngineArray *a, const void *lang)
{
    EngineData *d = ea_getEngineData(a, lang);

    if (d == 0)
        return 0;
    return d->engine;
}

THIS uint32_t ea_getCallbackFnFlag(EngineArray *a, const void *lang)
{
    EngineData *d = ea_getEngineData(a, lang);

    if (d == 0)
        return CALLBACK_DEFAULT;
    return d->callbacks;
}

THIS void ea_removeEngine(EngineArray *a, const void *lang)
{
    eng_removeData(&a->base, lang);
}

/* The two version strings the array's own reader carries. One is handed
   back as it stands; the other is four numbers packed one to a byte. */
THIS const char *ea_getConcatenativeVersion(EngineArray *a, const void *lang)
{
    return ini_getString(&a->ini, LANG_TEXT(lang), KEY_CONCATENATIVE);
}

THIS uint32_t ea_getCorporaVersion(EngineArray *a, const void *lang)
{
    uint32_t    w = 0, x = 0, y = 0, z = 0;
    const char *s = ini_getString(&a->ini, LANG_TEXT(lang), KEY_CORPORA);

    if (s != 0)
        sscanf(s, "%u.%u.%u.%u", &w, &x, &y, &z);

    return (w << 24) | (x << 16) | (y << 8) | z;
}

const void *vtbl_enginedata[1] = { (void *)ed_deleteItself };

ALIAS("??_7EngineData@@6B@", "vtbl_enginedata");
ALIAS("??_EEngineData@@UAEPAXI@Z", "ed_deleteItself");
ALIAS("??_GEngineData@@UAEPAXI@Z", "ed_deleteItself");
ALIAS("??0EngineData@@QAE@PBD@Z", "ed_ctor");
ALIAS("??1EngineData@@UAE@XZ", "ed_dtor");
ALIAS("?getEngine@EngineArray@@QAEPAVEngineWrapper@@QBVLangIdentifier@@@Z",
      "ea_getEngine");
ALIAS("?getEngineData@EngineArray@@QAEPAVEngineData@@QBVLangIdentifier@@@Z",
      "ea_getEngineData");
ALIAS("?getLibraryName@EngineArray@@AAEPBDQBVLangIdentifier@@@Z",
      "ea_getLibraryName");
ALIAS("?getCallbackFnFlag@EngineArray@@QAEKQBVLangIdentifier@@@Z",
      "ea_getCallbackFnFlag");
ALIAS("?getCorporaVersion@EngineArray@@QAEKQBVLangIdentifier@@@Z",
      "ea_getCorporaVersion");
ALIAS("?getConcatenativeVersion@EngineArray@@QAEPBDQBVLangIdentifier@@@Z",
      "ea_getConcatenativeVersion");
ALIAS("?removeEngine@EngineArray@@QAEXQBVLangIdentifier@@@Z",
      "ea_removeEngine");
