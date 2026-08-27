/* One romanizer instance, and what it forwards to.
 *
 * IBM's RomInstance is a COM-shaped object of 0x18 bytes holding two things:
 * a RomInstParam it makes with the path it was given, and a Romanizer it makes
 * with that RomInstParam. Every one of its thirty-one methods then forwards --
 * the parameter calls to the first, everything about text to the second -- and
 * that is the whole of the object. So this is that forwarding, over the table
 * in src/eci_rom.h rather than over a vtable found in a loaded library.
 *
 * romedll_link.obj is what answers the manager's question in a build where the
 * romanizer is part of the program rather than a DLL, and jp_rom_new is that:
 * the manager takes it from the table in src/eci_romedll.c and calls it for an
 * instance.
 */

#include <string.h>
#include "jprom.h"

typedef struct JpRom {
    EvvRom       base;
    RomInstParam param;
} JpRom;

static void jp_release(EvvRom *r)
{
    JpRom *j = (JpRom *)r;

    rp_dtor(&j->param);
    cpp_delete(j);
}

static int32_t jp_addText(EvvRom *r, const char *text, int32_t len,
                          int32_t flag)
{
    (void)r; (void)text; (void)len; (void)flag;
    return 1;
}

static int32_t jp_insertIndex(EvvRom *r)
{
    (void)r;
    return 1;
}

static int32_t jp_processSentence(EvvRom *r, char **out, int32_t annotated)
{
    (void)r; (void)out; (void)annotated;
    return 0;
}

static int32_t jp_stop(EvvRom *r)
{
    (void)r;
    return 1;
}

static int32_t jp_resume(EvvRom *r)
{
    (void)r;
    return 1;
}

static int32_t jp_UCS2ToMBCS(EvvRom *r, const uint16_t *in, char **out,
                             int32_t n)
{
    (void)r; (void)in; (void)out; (void)n;
    return 0;
}

static int32_t jp_setParam(EvvRom *r, int32_t which, int32_t value)
{
    return rp_setParam(&((JpRom *)r)->param, which, value);
}

static int32_t jp_getParam(EvvRom *r, int32_t which)
{
    return rp_getParam(&((JpRom *)r)->param, which);
}

static void jp_clearErrors(EvvRom *r)
{
    rp_clearErrors(&((JpRom *)r)->param);
}

static uint32_t jp_progStatus(EvvRom *r)
{
    return rp_getErrors(&((JpRom *)r)->param);
}

static void jp_errorMessage(EvvRom *r, char *out)
{
    rp_getErrorMessage(&((JpRom *)r)->param, out);
}

static int32_t jp_addParam(EvvRom *r, const char *text, int32_t len)
{
    (void)r; (void)text; (void)len;
    return 1;
}

static const EvvRomOps JP_OPS = {
    jp_release,
    jp_addText,
    jp_insertIndex,
    jp_processSentence,
    jp_stop,
    jp_resume,
    jp_UCS2ToMBCS,
    jp_setParam,
    jp_getParam,
    jp_clearErrors,
    jp_progStatus,
    jp_errorMessage,
    jp_addParam,
};

/* What the manager calls for an instance. The original checks a licence
   first, makes the object, and asks it for the one interface the manager
   wants; if that comes back empty the object is thrown away again. */
EvvRom *jp_rom_new(const char *dir)
{
#if defined(JPROM_INCOMPLETE)
    /* Nothing here can convert Japanese yet, and answering an instance that
       quietly produced nothing would look exactly like an engine that works.
       See the note in jprom.h. */
    (void)dir;
    return 0;
#else
    JpRom *j = (JpRom *)cpp_new((uint32_t)sizeof *j);

    if (j == 0)
        return 0;
    memset(j, 0, sizeof *j);
    j->base.ops = &JP_OPS;
    rp_ctor(&j->param, dir);
    if (rp_getErrors(&j->param) & ROM_ERR_MEMORY) {
        jp_release(&j->base);
        return 0;
    }
    return &j->base;
#endif
}
