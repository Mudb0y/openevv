/* Handing out the one object the library exports, and the engine facade it
 * wraps.
 *
 * Three kinds can be asked for. One and two both answer with an
 * EngineWrapper, so the caller gets the same thing whichever it names;
 * three answers with the licence object itself. Anything else is refused.
 * Whatever comes back has had a reference added before it is handed over.
 *
 * The licence lives in a static built the first time somebody asks. Its
 * guard is one bit of a word rather than a flag of its own, which is how
 * the compiler that built this arranged a function-local static.
 */

#include <stdint.h>
#include "eci_synththread.h"

#define STDCALL __attribute__((stdcall))

typedef STDCALL uint32_t (*AddRefFn)(void *self);

/* Slot one of anything's table adds a reference. */
#define ADD_REF(p) (((AddRefFn *)(*(void ***)(p)))[1](p))

/* Which kind the caller is asking for. */
#define OBJ_ENGINE_A 1
#define OBJ_ENGINE_B 2
#define OBJ_LICENCE  3

/* The engine facade. Only the constructor is written here; the rest of the
   class is still the original's, and so is its table. */
typedef struct EngineWrapper {
    const void *vt;        /* +0x00 */
    int32_t     unknown_04;
    void       *machine;   /* +0x08, what delta_new made */
    int32_t     failed;    /* +0x0c, set when it made nothing */
    int32_t     unknown_10;
} EngineWrapper;

#define ENGINE_WRAPPER_BYTES 0x14

typedef struct RequestLicense {
    const void *vt;
    int32_t     granted;
} RequestLicense;

extern const void *vtbl_unknown[3];
extern const void *vtbl_enginewrapper[] MANGLED("??_7EngineWrapper@@6B@");

extern void *delta_new(void);
extern void *cpp_new(uint32_t n) MANGLED("??2@YAPAXI@Z");
extern THIS RequestLicense *rl_ctor(RequestLicense *r);
extern THIS int32_t rl_licenseGranted(RequestLicense *r);

/* The licence, and the one bit that says it has been built. */
static RequestLicense licence;
static uint32_t       licence_guard;

/* Nothing to do. Whatever this once set up is set up by the time anything
   calls it, and the original left the body empty too. */
void initDllLink(void)
{
}

THIS EngineWrapper *ew_ctor(EngineWrapper *e)
{
    e->vt         = &vtbl_unknown;
    e->vt         = &vtbl_enginewrapper;
    e->unknown_04 = 0;
    e->failed     = 0;
    e->unknown_10 = 0;

    e->machine = delta_new();
    if (e->machine == 0)
        e->failed = 1;

    return e;
}

int getObject(int32_t kind, void **out)
{
    if ((licence_guard & 1) == 0) {
        licence_guard |= 1;
        rl_ctor(&licence);
    }

    *out = 0;

    if (kind == OBJ_ENGINE_A || kind == OBJ_ENGINE_B) {
        if (rl_licenseGranted(&licence)) {
            void *p = cpp_new(ENGINE_WRAPPER_BYTES);

            *out = p ? ew_ctor(p) : 0;
            if (*out != 0)
                ADD_REF(*out);
        }
    }

    if (kind == OBJ_LICENCE) {
        *out = &licence;
        ADD_REF(*out);
    }

    return *out != 0;
}

ALIAS("??0EngineWrapper@@QAE@XZ", "ew_ctor");
