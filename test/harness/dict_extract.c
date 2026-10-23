/* The phone faulted reading the right fence at NULL + 0x44 in extract.
   Exercise the real static helper with a minimal arena and statement table. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "delta.h"
#include "eci_deltadict.c"

static uint64_t storage[8192];
uint8_t *evv_arena_base = (uint8_t *)storage;
__thread const delta_language *delta_lang_current;
void delta_lang_none(void) { abort(); }
void *TFLDS(void *p) { return (char *)p + 8; }
static void *get_value(void *p) { return p; }

int main(void)
{
    delta_state state = {0};
    delta_stmt statements[2] = {{0}};
    void *(*getters[])(void *) = {get_value};
    delta_language language = {0};
    uint8_t out[256] = {0};
    state.vars = 16;
    state.stack = 8192;
    delta_vars *vars = EVV_AT(delta_vars *, state.vars);
    delta_stack *stack = EVV_AT(delta_stack *, state.stack);
    vars->fence_base = 16;
    stack->spine_r = 8704;
    int32_t *left = EVV_AT(int32_t *, 8448);
    int32_t *right = EVV_AT(int32_t *, 8704);
    left[17] = 8960 | 1;
    right[17] = 1;
    int32_t *data = EVV_AT(int32_t *, 8960);
    data[1] = 8704;
    ((char *)data)[8] = 'X';
    statements[1].get = getters;
    language.stmtbl = statements;
    delta_lang_current = &language;

    assert(!extract(&state, 8448, 0, 1, out, 254, 1));
    assert(!extract(&state, 0, 8704, 1, out, 254, 1));
    assert(!extract(&state, 8448, 8704, 1, out, 254, 0));
    assert(!extract(&state, 8448, 8704, 1, out, 254, 255));
    assert(extract(&state, 8448, 8704, 1, out, 254, 1));
    assert(strcmp((char *)out, "X") == 0);
    left[17] = 1; /* A missing next statement is not an extractable span. */
    assert(!extract(&state, 8448, 8704, 1, out, 254, 1));
    puts("PASS: null dictionary fences, missing statement and valid extraction");
    return 0;
}
