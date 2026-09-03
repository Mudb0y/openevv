/* The licence, which is always granted.
 *
 * A small object in the engine's own reference-counted style: query for an
 * interface, add a reference, drop one. Asking whether a licence is needed
 * says no, asking for one does nothing, and asking whether one was granted
 * says yes, because the constructor sets that and nothing ever clears it.
 * So the whole thing is a formality the engine goes through on the way up.
 *
 * The reference counting is a formality too. Adding and dropping both
 * answer nought and neither keeps a count, which is safe only because this
 * object is never actually shared -- the original's doing, kept as it is.
 *
 * Two of the five in the table are stdcall with the object pushed like any
 * other argument rather than passed in a register, which is what the engine
 * uses for anything reached across an interface boundary.
 */

#include <stdint.h>
#include "eci_synththread.h"
#include "evv_abi.h"



typedef struct RequestLicense {
    const void *vt;      /* +0x00 */
    int32_t     granted; /* +0x04 */
} RequestLicense;

/* Which interfaces this will answer to. */
#define IID_UNKNOWN  1
#define IID_LICENCE  3

typedef STDCALL void (*AddRefFn)(void *self);

extern const void *vtbl_requestlicense[5];
extern const void *vtbl_unknown[3];
extern void purecall(void) MANGLED("__purecall");

STDCALL int32_t  rl_queryInterface(RequestLicense *r, uint32_t iid, void **out);
STDCALL uint32_t rl_addRef(RequestLicense *r);
STDCALL uint32_t rl_release(RequestLicense *r);
THIS    void     rl_requestLicense(RequestLicense *r, int32_t which);
THIS    int32_t  rl_licenseNeeded(RequestLicense *r);

/* Granted from the moment it exists. */
THIS RequestLicense *rl_ctor(RequestLicense *r)
{
    r->vt      = &vtbl_unknown;
    r->vt      = &vtbl_requestlicense;
    r->granted = 1;
    return r;
}

THIS int32_t rl_licenseGranted(RequestLicense *r)
{
    return r->granted;
}

/* Hand back the object itself for either name it answers to, and nothing at
   all for anything else. The answer says whether something came back. */
STDCALL int32_t rl_queryInterface(RequestLicense *r, uint32_t iid, void **out)
{
    *out = 0;

    if (iid == IID_UNKNOWN || iid == IID_LICENCE) {
        *out = r;
        ((AddRefFn)((void **)r->vt)[1])(r);
    }

    return *out != 0;
}

STDCALL uint32_t rl_addRef(RequestLicense *r)
{
    (void)r;
    return 0;
}

STDCALL uint32_t rl_release(RequestLicense *r)
{
    (void)r;
    return 0;
}

THIS void rl_requestLicense(RequestLicense *r, int32_t which)
{
    (void)r;
    (void)which;
}

THIS int32_t rl_licenseNeeded(RequestLicense *r)
{
    (void)r;
    return 0;
}

const void *vtbl_requestlicense[5] = {
    (void *)rl_queryInterface,
    (void *)rl_addRef,
    (void *)rl_release,
    (void *)rl_requestLicense,
    (void *)rl_licenseNeeded
};

/* The interface it inherits declares three and implements none. */
const void *vtbl_unknown[3] = {
    (void *)purecall, (void *)purecall, (void *)purecall
};

ALIAS("??_7RequestLicense@@6B@", "vtbl_requestlicense");
ALIAS("??_7Unknown@@6B@", "vtbl_unknown");
ALIAS("??0RequestLicense@@QAE@XZ", "rl_ctor");
ALIAS("?licenseGranted@RequestLicense@@QAEHXZ", "rl_licenseGranted");
ALIAS_N("?queryInterface@RequestLicense@@UAGHKPAPAX@Z", "rl_queryInterface", 12);
ALIAS_N("?addRef@RequestLicense@@UAGKXZ", "rl_addRef", 4);
ALIAS_N("?release@RequestLicense@@UAGKXZ", "rl_release", 4);
ALIAS("?requestLicense@RequestLicense@@UAEXH@Z", "rl_requestLicense");
ALIAS("?licenseNeeded@RequestLicense@@UAEHXZ", "rl_licenseNeeded");
