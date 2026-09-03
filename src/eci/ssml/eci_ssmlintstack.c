/* A stack of numbers with the runs folded up.
 *
 * The reader pushes one of these for every element it enters that carries a
 * voice number or a volume, and pops it again on the way out. A document
 * that nests the same value ten deep would take ten slots, so pushing what
 * is already on top bumps that slot's run instead. `count' is how many
 * pushes are outstanding either way, which is what the reader asks for when
 * it wants to know whether anything is open at all.
 *
 * Two things the original does that are kept. `isValid' calls a stack
 * exactly full invalid rather than merely at its limit, so a twentieth
 * value on a fresh stack can be pushed and then neither peeked nor popped
 * -- the doubling happens on the push after the one that fills it. And
 * popping the last of a run leaves `top' at nought rather than stepping
 * below it, so the bottom slot keeps its value after the stack empties.
 */

#include <stdint.h>
#include <string.h>
#include "evv_abi.h"
#include "eci_ssml.h"

extern void *cpp_new(uint32_t n) MANGLED("??2@YAPAXI@Z");
extern void  cpp_delete(void *p) MANGLED("??3@YAXPAX@Z");

THIS void sis_ctor(SSMLIntStack *s)
{
    s->top   = 0;
    s->slots = SSML_STACK_SLOTS;
    s->count = 0;
    s->values = cpp_new((uint32_t)s->slots * 4);
    s->runs   = cpp_new((uint32_t)s->slots * 4);
}

THIS void sis_dtor(SSMLIntStack *s)
{
    if (s->values != 0) {
        cpp_delete(s->values);
        s->values = 0;
    }
    if (s->runs != 0) {
        cpp_delete(s->runs);
        s->runs = 0;
    }
}

THIS void sis_delete(SSMLIntStack *s)
{
    if (s == 0)
        return;
    sis_dtor(s);
    cpp_delete(s);
}

THIS int8_t sis_isValid(SSMLIntStack *s)
{
    return s->top >= 0 && s->top < s->slots;
}

THIS int8_t sis_isEmpty(SSMLIntStack *s)
{
    return s->count == 0;
}

THIS int32_t sis_stackSize(SSMLIntStack *s)
{
    return s->count;
}

THIS void sis_push(SSMLIntStack *s, int32_t v)
{
    if (s->count == 0) {
        s->top = 0;
        s->values[s->top] = v;
        s->runs[s->top] = 1;
        s->count++;
        return;
    }

    if (s->values[s->top] == v) {
        s->runs[s->top]++;
        s->count++;
        return;
    }

    s->top++;

    if (s->top == s->slots) {
        int32_t *values = cpp_new((uint32_t)s->slots * 2 * 4);
        int32_t *runs   = cpp_new((uint32_t)s->slots * 2 * 4);
        int32_t  i;

        for (i = 0; i < s->slots; i++) {
            values[i] = s->values[i];
            runs[i]   = s->runs[i];
        }
        s->slots *= 2;
        cpp_delete(s->values);
        cpp_delete(s->runs);
        s->values = values;
        s->runs   = runs;
    }

    s->values[s->top] = v;
    s->runs[s->top] = 1;
    s->count++;
}

THIS int32_t sis_pop(SSMLIntStack *s)
{
    int32_t v;

    if (!sis_isValid(s) || sis_isEmpty(s))
        return -1;

    s->runs[s->top]--;
    s->count--;
    v = s->values[s->top];

    if (s->runs[s->top] == 0 && s->top != 0)
        s->top--;

    return v;
}

THIS int32_t sis_peek(SSMLIntStack *s)
{
    if (!sis_isValid(s) || sis_isEmpty(s))
        return -1;
    return s->values[s->top];
}

ALIAS("??0SSMLIntStack@@QAE@XZ", "sis_ctor");
ALIAS("??1SSMLIntStack@@QAE@XZ", "sis_dtor");
ALIAS("?deleteSSMLIntStack@SSMLIntStack@@QAEXXZ", "sis_delete");
ALIAS("?push@SSMLIntStack@@QAEXH@Z", "sis_push");
ALIAS("?pop@SSMLIntStack@@QAEHXZ", "sis_pop");
ALIAS("?peek@SSMLIntStack@@QAEHXZ", "sis_peek");
ALIAS("?isValid@SSMLIntStack@@QAE_NXZ", "sis_isValid");
ALIAS("?isEmpty@SSMLIntStack@@QAE_NXZ", "sis_isEmpty");
ALIAS("?stackSize@SSMLIntStack@@QAEHXZ", "sis_stackSize");
