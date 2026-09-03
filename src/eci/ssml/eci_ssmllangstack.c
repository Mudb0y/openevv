/* A stack of languages, with the runs folded up.
 *
 * The same arrangement as the stack of numbers beside it, and for the same
 * reason: a document may nest xml:lang, and the reader has to know which
 * language it is back in when an element closes. What makes it its own file
 * is the element -- a whole LanguageId rather than a word -- so the runs
 * are counted against an array of forty-eight byte records and two
 * languages count as the same one when `equals' says so, which ignores the
 * code set.
 *
 * The two answers that hand a language back give a language with nothing in
 * it when the stack has nothing to give, rather than refusing: the reader
 * compares what it gets against the environment's language, and nought is
 * a language no document names.
 */

#include <stdint.h>
#include <string.h>
#include "evv_abi.h"
#include "eci_ssml.h"

extern void *cpp_new(uint32_t n) MANGLED("??2@YAPAXI@Z");
extern void  cpp_delete(void *p) MANGLED("??3@YAXPAX@Z");

static LanguageId *sls_newSlots(int32_t slots)
{
    LanguageId *a = cpp_new((uint32_t)slots * (uint32_t)sizeof(LanguageId));
    int32_t     i;

    if (a == 0)
        return 0;
    for (i = 0; i < slots; i++)
        li_init(&a[i]);
    return a;
}

THIS void sls_ctor(SSMLLangStack *s)
{
    s->top   = 0;
    s->slots = SSML_STACK_SLOTS;
    s->count = 0;
    s->values = sls_newSlots(s->slots);
    s->runs   = cpp_new((uint32_t)s->slots * 4);
}

THIS void sls_dtor(SSMLLangStack *s)
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

THIS void sls_delete(SSMLLangStack *s)
{
    if (s == 0)
        return;
    sls_dtor(s);
    cpp_delete(s);
}

THIS int8_t sls_isValid(SSMLLangStack *s)
{
    return s->top >= 0 && s->top < s->slots;
}

THIS int8_t sls_isEmpty(SSMLLangStack *s)
{
    return s->count == 0;
}

THIS int32_t sls_stackSize(SSMLLangStack *s)
{
    return s->count;
}

THIS void sls_push(SSMLLangStack *s, LanguageId v)
{
    if (s->count == 0) {
        s->top = 0;
        s->values[s->top] = v;
        s->runs[s->top] = 1;
        s->count++;
        return;
    }

    if (li_equals(&s->values[s->top], &v)) {
        s->runs[s->top]++;
        s->count++;
        return;
    }

    s->top++;

    if (s->top == s->slots) {
        int32_t     want   = s->slots * 2;
        LanguageId *values = sls_newSlots(want);
        int32_t    *runs   = cpp_new((uint32_t)want * 4);
        int32_t     i;

        for (i = 0; i < s->slots; i++) {
            values[i] = s->values[i];
            runs[i]   = s->runs[i];
        }
        s->slots = want;
        cpp_delete(s->values);
        cpp_delete(s->runs);
        s->values = values;
        s->runs   = runs;
    }

    s->values[s->top] = v;
    s->runs[s->top] = 1;
    s->count++;
}

THIS LanguageId sls_pop(SSMLLangStack *s)
{
    LanguageId out;

    if (!sls_isValid(s) || sls_isEmpty(s)) {
        li_initPacked(&out, 0);
        return out;
    }

    s->runs[s->top]--;
    s->count--;
    out = s->values[s->top];

    if (s->runs[s->top] == 0 && s->top != 0)
        s->top--;

    return out;
}

THIS LanguageId sls_peek(SSMLLangStack *s)
{
    LanguageId out;

    if (!sls_isValid(s) || sls_isEmpty(s)) {
        li_initPacked(&out, 0);
        return out;
    }

    return s->values[s->top];
}

ALIAS("??0SSMLLangStack@@QAE@XZ", "sls_ctor");
ALIAS("??1SSMLLangStack@@QAE@XZ", "sls_dtor");
ALIAS("?deleteSSMLLangStack@SSMLLangStack@@QAEXXZ", "sls_delete");
ALIAS("?push@SSMLLangStack@@QAEXVLanguageId@@@Z", "sls_push");
ALIAS("?pop@SSMLLangStack@@QAE?AVLanguageId@@XZ", "sls_pop");
ALIAS("?peek@SSMLLangStack@@QAE?AVLanguageId@@XZ", "sls_peek");
ALIAS("?isValid@SSMLLangStack@@QAE_NXZ", "sls_isValid");
ALIAS("?isEmpty@SSMLLangStack@@QAE_NXZ", "sls_isEmpty");
ALIAS("?stackSize@SSMLLangStack@@QAEHXZ", "sls_stackSize");
