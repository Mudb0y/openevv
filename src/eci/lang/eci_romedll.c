/* Which romanizer is linked in.
 *
 * IBM's romedll_link.obj is one function, getRomObject, and it is what the
 * manager finds when the romanizer is part of the program rather than a
 * library beside it. The manager takes its address as a link-time symbol and
 * calls it for an instance; in a build with no romanizer the symbol is the
 * loaded library's instead, and in one with neither there is nothing to load
 * and the manager gets no instance.
 *
 * A binary of ours can hold several languages at once, so a link-time symbol
 * is not enough: two romanizers would have to answer to one name. Instead each
 * romanizer registers itself from its own language module, and this holds what
 * was registered. The manager asks by family and dialect and gets a maker or
 * nothing, which is the same question IBM's asks of the linker.
 *
 * Eighteen families of two dialects, indexed the way the manager's own arrays
 * are, so a family number means the same thing on both sides of the call.
 */

#include <stdint.h>
#include "eci_rom.h"

#define ROM_FAMILIES 0x12
#define ROM_DIALECTS 2

/* What is linked in. IBM's manager finds its romanizer by taking the address
   of getRomObject, a symbol that is there when the romanizer is part of the
   program and comes from the loaded library when it is not. The same question
   is answered here at compile time, because the Makefile is what knows which
   languages are in a build, and because a weak symbol -- the obvious way to
   ask the linker instead -- does not resolve in PE the way it would in ELF,
   and this engine is built both ways. */
#if defined(EVV_ROM_JAJP)
extern EvvRom *jp_rom_new(const char *dir);
#endif

static EvvRomMaker linked(int32_t family, int32_t dialect)
{
#if defined(EVV_ROM_JAJP)
    /* Japanese is family 8, dialect 0. */
    if (family == 8 && dialect == 0)
        return jp_rom_new;
#endif
    (void)family;
    (void)dialect;
    return 0;
}

/* And what a caller put there instead, which is how test/romcan.c stands a
   recording where the romanizer would be. */
static EvvRomMaker makers[ROM_FAMILIES][ROM_DIALECTS];

static int inRange(int32_t family, int32_t dialect)
{
    return family >= 1 && family <= ROM_FAMILIES
        && dialect >= 0 && dialect < ROM_DIALECTS;
}

void evv_rom_provide(int32_t family, int32_t dialect, EvvRomMaker make)
{
    if (inRange(family, dialect))
        makers[family - 1][dialect] = make;
}

EvvRomMaker evv_rom_maker(int32_t family, int32_t dialect)
{
    if (!inRange(family, dialect))
        return 0;
    if (makers[family - 1][dialect])
        return makers[family - 1][dialect];
    return linked(family, dialect);
}
