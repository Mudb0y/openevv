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
#include "evv_abi.h"


typedef STDCALL uint32_t (*AddRefFn)(void *self);

/* Slot one of anything's table adds a reference. */
#define ADD_REF(p) (((AddRefFn *)(*(void ***)(p)))[1](p))

/* Which kind the caller is asking for. */
#define OBJ_ENGINE_A 1
#define OBJ_ENGINE_B 2
#define OBJ_LICENCE  3

/* The engine facade, which is in eci_enginewrap.c. Only its size is
   wanted here, to ask for the room before constructing one. */
extern const uint32_t ew_bytes;

typedef struct RequestLicense {
    const void *vt;
    int32_t     granted;
} RequestLicense;

extern void *cpp_new(uint32_t n) MANGLED("??2@YAPAXI@Z");
extern THIS RequestLicense *rl_ctor(RequestLicense *r);
extern THIS int32_t rl_licenseGranted(RequestLicense *r);
extern THIS void *ew_ctor(void *e);

/* The licence, and the one bit that says it has been built. */
static RequestLicense licence;
static uint32_t       licence_guard;

/* Nothing to do. Whatever this once set up is set up by the time anything
   calls it, and the original left the body empty too. */
void initDllLink(void)
{
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
            void *p = cpp_new(ew_bytes);

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

