/* The list of engines the installation has, and the ini reader underneath it.
 *
 * Both are embedded by value -- the array holds a list and each holds a
 * reader -- so their size decides where the fields after them sit, and six
 * files used to say what that size was, each on its own. This is the one
 * place now.
 */

#ifndef ECI_ENGINE_H
#define ECI_ENGINE_H

#include <stdint.h>

/* The gap is never touched from our side: what is in it is the reader's own
   working room, and only its size matters here. */
typedef struct IniFileReader {
    const char *text;
    int32_t     room;       /* only used by the growing buffer */
    int32_t     unused_08;
    char        gap[0x110];
    int32_t     at;         /* where the walk has got to */
    int32_t     size;
} IniFileReader;

typedef struct EngineList {
    void        **data;      /* one slot per language and dialect */
    uint8_t       langs;     /* the widest language numbered */
    uint8_t       dialects;  /* and the widest dialect, plus one */
    uint8_t       pad_06[2];
    IniFileReader ini;
} EngineList;

/* What the list holds in a slot, as much of it as the list itself knows: a
   table of its own to be deleted through, and the callback flag the list
   fills in. The engine's record and the phoneme table both start this way. */
typedef struct ListData {
    const void *vt;
    int32_t     callbacks;
} ListData;

typedef struct EngineArray {
    EngineList    base;
    IniFileReader ini;   /* the array's own, not the list's */
} EngineArray;

#endif
