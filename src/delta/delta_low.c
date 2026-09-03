/* The language's own data, put where a value can name it.
 *
 * The rules and the tables name their data by address: delta_rule_sym holds
 * six and a half thousand pointers into the seventy-five stores of bytes the
 * compiler left in its objects, the set and action tables hold one pointer per
 * entry into two more stores, and a rule or the runtime hands one of those
 * addresses to the machine as a value. A value is thirty-two bits, so that
 * only works while the program itself sits somewhere a thirty-two bit value
 * can reach -- which is why the engine was linked low and told not to move.
 *
 * A shared library cannot be linked low: the loader puts it where it likes,
 * and in a sixty-four bit process that is far above what a value can name. So
 * every store the machine can be handed an address inside is copied, once,
 * into the arena, and an address in the program is translated to the address
 * of its copy at the few places where one crosses over into a value. After
 * that nothing the machine holds points into the program, and the program can
 * be loaded anywhere at all.
 *
 * Copied once for the process rather than once per engine, because sharing the
 * stores in the program amounted to the same thing: two instances have always
 * written to the same bytes, and they go on doing so.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "delta.h"
#include "delta_rules_c.h"
#include "evv_arena.h"

/* As in delta_rules.c: the language's own tables, reached through the one
   in force. delta_sym_ref is a macro too, in delta_rules_c.h, and reaches
   a slot the language module owns rather than one of ours. */
#define L                      (delta_lang_now())
#define delta_const_store      (L->const_store)
#define delta_authored_store   (L->authored_store)
#define delta_rule_sym         (L->rule_sym)
#define delta_rule_sym_count   (L->rule_sym_count)

#define ROUND16(n) (((n) + 15u) & ~15u)

/* One entry per store of bytes a language names by address, and a program
   may have several languages in it, each with its own: English has
   seventy-five and German ninety, plus two of dictionary entries apiece. A
   language is registered the first time one of its machines is made, so a
   program that never speaks the second one never spends its share. */
#define REGIONS 512

static struct {
    const unsigned char *at;
    size_t               bytes;
    unsigned char       *copy;
} region[REGIONS];

static int regions;

static void *low_alloc(size_t bytes, const char *what)
{
    void *p = evv_arena_alloc(bytes);

    if (p == 0) {
        fprintf(stderr, "evv: no room to copy %lu bytes of %s\n",
                (unsigned long)bytes, what);
        abort();
    }
    return p;
}

/* Copy a store of the language's bytes into the arena, once. Registering the
   same store twice is ordinary: the tables do not know what the rules have
   already asked for. */
void delta_low_region(const void *at, size_t bytes)
{
    const unsigned char *c = at;
    int i;

    if (at == 0 || bytes == 0)
        return;

    for (i = 0; i < regions; i++)
        if (region[i].at == c && region[i].bytes == bytes)
            return;

    if (regions >= REGIONS) {
        fprintf(stderr, "evv: more stores of language data than there is room"
                " to copy\n");
        abort();
    }

    region[regions].at = c;
    region[regions].bytes = bytes;
    region[regions].copy = low_alloc(ROUND16(bytes), "the language");
    memcpy(region[regions].copy, c, bytes);
    regions++;
}

/* The same address, in the copy. Anything the machine is handed an address of
   has to have been registered; a pointer from nowhere is a mistake worth
   stopping for rather than a value that will be wrong later.

   One past the end of a store counts as inside it, because a rule may name
   the byte after a string. */
void *delta_low_at(const void *p)
{
    const unsigned char *c = p;
    int i;

    if (p == 0)
        return 0;

    for (i = 0; i < regions; i++)
        if (c >= region[i].at && c <= region[i].at + region[i].bytes)
            return region[i].copy + (c - region[i].at);

    fprintf(stderr, "evv: %p is in the program and in none of the stores"
            " copied out of it\n", p);
    abort();
    return 0;
}

/* A copy of one thing in the program, made once. The link tables hand the
   machine the address of an array -- the set entries, the action entries, the
   language's file names -- and the array is in the program too. Its elements
   are ordinary pointers that C follows, so the copy needs no translating
   inside it; where an element does become a value, delta_low_at does that. */
void *delta_low_copy(const void *what, size_t bytes)
{
    static struct {
        const void *what;
        void       *copy;
    } made[16];
    static int mades;

    void *copy;
    int   i;

    if (what == 0 || bytes == 0)
        return 0;

    for (i = 0; i < mades; i++)
        if (made[i].what == what)
            return made[i].copy;

    copy = low_alloc(bytes, "a table of the language's");
    memcpy(copy, what, bytes);

    if (mades < (int)(sizeof made / sizeof made[0])) {
        made[mades].what = what;
        made[mades].copy = copy;
        mades++;
    }
    return copy;
}

/* The symbol table, as values pointing at the copies. */
void delta_syms_bind(void)
{
    const delta_store *s;
    int32_t           *ref;
    int                i;

    if (delta_sym_ref != 0)
        return;

    for (s = delta_const_store; s->at != 0; s++)
        delta_low_region(s->at, s->bytes);
    for (s = delta_authored_store; s->at != 0; s++)
        delta_low_region(s->at, s->bytes);

    ref = low_alloc((size_t)delta_rule_sym_count * sizeof *ref,
                    "the symbol table");
    for (i = 0; i < delta_rule_sym_count; i++)
        ref[i] = EVV_REF(delta_low_at(delta_rule_sym[i]));

    delta_sym_ref = ref;
}
