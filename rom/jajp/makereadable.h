/* What MakeReadableJP is, and what it is handed.
 *
 * This is the front of the analyser rather than the back of it: before a
 * sentence reaches `TextAnalysis' at all, the pieces of it that are not words
 * are rewritten into words. A date becomes the reading of a date, a currency
 * amount becomes the amount and the currency, a telephone number becomes
 * digits with the pauses in the right places. `TextNormalizer' is what decides
 * which of the eight normalisers a stretch of text wants; this class is the
 * eight of them, for Japanese.
 *
 * The class holds nothing but its vtable, and neither does the base it
 * derives from: every one of its methods is handed the text, a buffer to put
 * the answer in, and how big that buffer is, and grows the buffer itself
 * where the answer will not fit.
 *
 * The twelve predicates all ask one question of one table apiece -- does the
 * text begin with one of these symbols, and if so what does it mean and how
 * long is it. The tables are IBM's and lang/jajp/rom_tables_jajp.c holds
 * them, lifted by tools/rom/tables.py: each is a run of pairs ending on a
 * pair with no string.
 */

#ifndef MAKEREADABLE_H
#define MAKEREADABLE_H

#include <stdint.h>

/* Both classes hold one field, which is the vtable, and nothing else. Ours
   parks it past the record for the reason every other record here does. */
#define MR_BYTES        0x04
#define MR_ROOM         (MR_BYTES + sizeof(void *))
#define MR_VTABLE_AT    (MR_BYTES)

/* How much room a growing buffer is given beyond what the answer needs: a
   quarter of a kilobyte, so that a long answer is not reallocated a byte at a
   time. The two appenders differ by one and by two, which is the terminator
   each of them leaves room for. */
#define MR_SLACK        0x100

/* What a symbol table entry is is declared by the lift itself, in
   lang/jajp/rom_tables_jajp.h, as `<tag>_symbol'. */

#endif
