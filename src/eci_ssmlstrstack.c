/* A stack of strings, each one a copy of its own.
 *
 * The reader pushes a string for every element it enters that carries one
 * as an attribute -- a pitch given as "+2st", an emphasis level, the source
 * of an audio element -- and pops it again on the way out. Unlike the other
 * two stacks there is no run folding here: two elements with the same pitch
 * are two slots, because what comes back off a pop is a string the caller
 * then owns and frees.
 *
 * Whatever is still on the stack when it goes is freed with it, which is
 * why a document that is missing its closing tags does not leak.
 *
 * One thing the original does that is kept: `isValid' calls a stack exactly
 * full invalid, and the doubling happens on the push after the one that
 * fills it, so the twentieth string on a fresh stack goes on and can then
 * be neither peeked nor popped until a twenty-first arrives.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "evv_abi.h"
#include "eci_ssml.h"

extern void  cpp_delete(void *p) MANGLED("??3@YAXPAX@Z");

THIS void sss_ctor(SSMLStrStack *s)
{
    s->count = 0;
    s->slots = SSML_STACK_SLOTS;
    s->items = malloc((size_t)s->slots * 4);
}

THIS void sss_dtor(SSMLStrStack *s)
{
    int32_t i;

    if (s->items == 0)
        return;

    for (i = 0; i < s->count; i++) {
        if (s->items[i] != 0) {
            free(s->items[i]);
            s->items[i] = 0;
        }
    }
    free(s->items);
    s->items = 0;
}

THIS void sss_delete(SSMLStrStack *s)
{
    if (s == 0)
        return;
    sss_dtor(s);
    cpp_delete(s);
}

THIS int8_t sss_isValid(SSMLStrStack *s)
{
    return s->count >= 0 && s->count < s->slots;
}

THIS int8_t sss_isEmpty(SSMLStrStack *s)
{
    return s->count == 0;
}

THIS int32_t sss_stackSize(SSMLStrStack *s)
{
    return s->count;
}

THIS void sss_push(SSMLStrStack *s, char *v)
{
    char *copy;

    if (s->count == s->slots) {
        char  **items = malloc((size_t)s->slots * 4 * 2);
        int32_t i;

        for (i = 0; i < s->slots; i++)
            items[i] = s->items[i];
        s->slots *= 2;
        free(s->items);
        s->items = items;
    }

    copy = malloc(strlen(v) + 1);
    strcpy(copy, v);
    s->items[s->count] = copy;
    s->count++;
}

/* What comes off is the caller's to free. */
THIS char *sss_pop(SSMLStrStack *s)
{
    char *v;

    if (!sss_isValid(s) || sss_isEmpty(s))
        return 0;

    v = s->items[s->count - 1];
    s->count--;
    return v;
}

THIS char *sss_peek(SSMLStrStack *s)
{
    if (!sss_isValid(s) || sss_isEmpty(s))
        return 0;
    return s->items[s->count - 1];
}

/* Counted from the bottom and one-based, which is how the reader asks for
   the one under the top. */
THIS char *sss_peekAt(SSMLStrStack *s, int32_t which)
{
    if (which <= 0 || which > s->count || sss_isEmpty(s))
        return 0;
    return s->items[which - 1];
}

ALIAS("??0SSMLStrStack@@QAE@XZ", "sss_ctor");
ALIAS("??1SSMLStrStack@@QAE@XZ", "sss_dtor");
ALIAS("?deleteSSMLStrStack@SSMLStrStack@@QAEXXZ", "sss_delete");
ALIAS("?push@SSMLStrStack@@QAEXPAD@Z", "sss_push");
ALIAS("?pop@SSMLStrStack@@QAEPADXZ", "sss_pop");
ALIAS("?peek@SSMLStrStack@@QAEPADXZ", "sss_peek");
ALIAS("?peek@SSMLStrStack@@QAEPADH@Z", "sss_peekAt");
ALIAS("?isValid@SSMLStrStack@@QAE_NXZ", "sss_isValid");
ALIAS("?isEmpty@SSMLStrStack@@QAE_NXZ", "sss_isEmpty");
ALIAS("?stackSize@SSMLStrStack@@QAEHXZ", "sss_stackSize");
