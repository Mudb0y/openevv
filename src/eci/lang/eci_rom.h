/* What a romanizer is, from the manager's side.
 *
 * A language written in another script carries a romanizer: the thing that is
 * jpnrom.dll in stock Eloquence, which takes text in that script and hands the
 * engine something it can read. The manager in eci_romanizer.c holds one per
 * language family and calls it for every scrap of text.
 *
 * IBM's romanizer is a COM-shaped C++ object and the manager reaches it
 * through numbered vtable slots -- addText at 0x0c, processSentence at 0x14
 * and so on. Those numbers exist because the romanizer arrived as a separate
 * library whose entry points had to be found at run time; ours is compiled in,
 * so the slots are named here instead. Nothing else about the arrangement
 * changes: one object, one table of functions, found through getRomObject
 * exactly as IBM's manager finds it.
 *
 * How one is found. IBM takes the address of getRomObject as a link-time
 * symbol, which is answered by romedll_link.obj when the romanizer is linked
 * in and by the loaded library otherwise. A binary of ours can hold several
 * languages at once, so a romanizer says it is there by registering itself
 * from its own language module's bind function, and eci_romedll.c holds what
 * it registered. An English-only build registers nothing and has no Japanese
 * in it.
 */

#ifndef ECI_ROM_H
#define ECI_ROM_H

#include <stdint.h>

typedef struct EvvRom EvvRom;

/* The calls the manager makes. The comment on each is the slot IBM's manager
   reaches it through, so the two can be read against each other. */
typedef struct EvvRomOps {
    void     (*release)(EvvRom *r);                          /* 0x08 */
    int32_t  (*addText)(EvvRom *r, const char *text, int32_t len,
                        int32_t flag);                       /* 0x0c */
    int32_t  (*insertIndex)(EvvRom *r);                      /* 0x10 */
    /* Answers RomResult: 2 when it has a sentence, and *out is it. */
    int32_t  (*processSentence)(EvvRom *r, char **out,
                                int32_t annotated);          /* 0x14 */
    int32_t  (*stop)(EvvRom *r);                             /* 0x18 */
    int32_t  (*resume)(EvvRom *r);                           /* 0x1c */
    int32_t  (*UCS2ToMBCS)(EvvRom *r, const uint16_t *in, char **out,
                           int32_t n);                       /* 0x20 */
    int32_t  (*setParam)(EvvRom *r, int32_t which, int32_t value); /* 0x28 */
    int32_t  (*getParam)(EvvRom *r, int32_t which);          /* 0x2c */
    void     (*clearErrors)(EvvRom *r);                      /* 0x3c */
    uint32_t (*progStatus)(EvvRom *r);                       /* 0x40 */
    void     (*errorMessage)(EvvRom *r, char *out);          /* 0x44 */
    int32_t  (*addParam)(EvvRom *r, const char *text, int32_t len); /* 0x6c */
} EvvRomOps;

/* Every romanizer instance begins with its table, which is what the manager
   holds a pointer to. */
struct EvvRom {
    const EvvRomOps *ops;
};

/* What a romanizer registers: a maker, told the directory the program was
   loaded from, which is the one thing IBM's manager works out for itself
   before asking. */
typedef EvvRom *(*EvvRomMaker)(const char *dir);

void evv_rom_provide(int32_t family, int32_t dialect, EvvRomMaker make);

/* What the manager asks for, in place of the address of getRomObject that
   IBM's takes: whichever maker was registered for that language, or nought.
   IBM has one romanizer library per family and needs no such argument; a
   binary of ours can hold several languages at once, so the family it is
   about is said here. */
EvvRomMaker evv_rom_maker(int32_t family, int32_t dialect);

#endif
