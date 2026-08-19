/* The growable buffer everything above the Delta machine writes through.

   Written to the original's behaviour rather than its instructions, because
   what matters to the machine is which characters come out and in what
   order, not how the room for them was found. Two asymmetries are the
   original's own and are kept: adding a string shifts from one below the end
   while adding a buffer shifts from the end itself, and the two put the
   terminator back at different points. Nothing has been tidied, because a
   tidy version would be a different buffer. */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "eci_io.h"
#include "evv_arena.h"

/* Room is taken in sixteens. */
static uint32_t roundUp(uint32_t n)
{
    if (n == 0)
        return 16;
    return (((n - 1) >> 4) + 1) << 4;
}

static int resize(DynaBuf *b, uint32_t n)
{
    if (n == 0)
        return 0;
    if (b->base == 0) {
        b->base = malloc(n);
        if (b->base == 0)
            return 0;
    } else {
        char *p = realloc(b->base, n);

        if (p == 0)
            return 0;
        b->base = p;
    }
    b->room = n;
    return 1;
}

/* Say how much is in it, finding room for one more than that so a
   terminator always fits. */
static int setUsed(DynaBuf *b, uint32_t n)
{
    if (b->room < n + 1 && !resize(b, roundUp(n + 1)))
        return 0;
    b->used = n;
    return 1;
}

DynaBuf *dynaBufNew(uint32_t size)
{
    DynaBuf *b;

    if (size == 0)
        size = 15;
    b = malloc(sizeof *b);
    if (b == 0)
        return 0;
    b->base = 0;
    if (!resize(b, roundUp(size + 1))) {
        free(b);
        return 0;
    }
    b->base[0] = 0;
    b->used = 0;
    b->at = 0;
    return b;
}

int dynaBufDelete(DynaBuf *b)
{
    if (b != 0) {
        if (b->base != 0)
            free(b->base);
        free(b);
    }
    return 0;
}

DynaBuf *dynaBufReset(DynaBuf *b)
{
    b->used = 0;
    b->base[0] = 0;
    b->at = 0;
    return b;
}

int dynaBufAddChar(DynaBuf *b, char c, int insert)
{
    if ((insert || b->at == b->used) && !setUsed(b, b->used + 1))
        return 0;

    if (insert) {
        uint32_t i;

        for (i = b->used; i >= b->at + 1; i--)
            b->base[i] = b->base[i - 1];
    } else if (b->at == b->used - 1) {
        b->base[b->used] = 0;
    }

    b->base[b->at] = c;
    b->at++;
    return 1;
}

int dynaBufAddString(DynaBuf *b, const char *s, int insert)
{
    uint32_t n = (uint32_t)strlen(s);

    if (!insert) {
        /* Overwriting only has to find room for what runs past the end. */
        uint32_t spare = b->used - b->at;

        n = (spare >= n) ? 0 : n - spare;
    }

    if (n != 0) {
        uint32_t i;

        if (!setUsed(b, b->used + n))
            return 0;
        b->base[b->used] = 0;
        if (insert)
            for (i = b->used - 1; i >= b->at + n; i--)
                b->base[i] = b->base[i - n];
    }

    for (; *s != 0; s++) {
        b->base[b->at] = *s;
        b->at++;
    }
    return 1;
}

int dynaBufAddInt(DynaBuf *b, int32_t v, int insert)
{
    char spelt[16];

    sprintf(spelt, "%d", (int)v);
    return dynaBufAddString(b, spelt, insert);
}

int dynaBufAddDynaBuf(DynaBuf *b, const DynaBuf *src, int insert)
{
    uint32_t n;
    uint32_t i;

    if (insert) {
        n = src->used;
    } else {
        uint32_t spare = b->used - b->at;

        n = (spare >= src->used) ? 0 : src->used - spare;
    }

    if (n != 0) {
        if (!setUsed(b, b->used + n))
            return 0;
        if (insert)
            for (i = b->used; i >= b->at + n; i--)
                b->base[i] = b->base[i - n];
    }

    for (i = 0; i < src->used; i++) {
        b->base[b->at] = src->base[i];
        b->at++;
    }
    b->base[b->used] = 0;
    return 1;
}

/* Take characters out from the cursor. Asking for more than there are takes
   everything from the cursor on. */
void dynaBufDeleteChars(DynaBuf *b, uint32_t n)
{
    if (n >= b->used - b->at) {
        b->used = b->at;
        b->base[b->used] = 0;
        return;
    }
    if (n > 0) {
        uint32_t i;

        b->used -= n;
        for (i = b->at; i <= b->used; i++)
            b->base[i] = b->base[i + n];
    }
}

uint32_t dynaBufLength(const DynaBuf *b)
{
    return b->used;
}

uint32_t dynaBufMoveRel(DynaBuf *b, int32_t delta)
{
    if (delta < 0) {
        uint32_t back = (uint32_t)(-delta);

        b->at = (back >= b->at) ? 0 : b->at - back;
    } else {
        b->at += (uint32_t)delta;
        if (b->at > b->used)
            b->at = b->used;
    }
    return b->at;
}

/* A negative position means the end. */
uint32_t dynaBufMoveAbs(DynaBuf *b, int32_t pos)
{
    b->at = (pos < 0) ? b->used : (uint32_t)pos;
    return b->at;
}

int dynaBufAtEnd(const DynaBuf *b)
{
    return b->at == b->used;
}

/* The terminator counts as a character here, which is what lets a caller
   read one past the end and get nought. */
char dynaBufChar(const DynaBuf *b, int32_t i)
{
    if (i < 0 || (uint32_t)i > b->used)
        return 0;
    return b->base[i];
}

char dynaBufCurrentChar(DynaBuf *b, int advance)
{
    if (advance != 0 && b->at < b->used) {
        char c = b->base[b->at];

        b->at++;
        return c;
    }
    return b->base[b->at];
}

char *dynaBufContents(const DynaBuf *b)
{
    return b->base;
}

char *dynaBufExtract(DynaBuf *b, int32_t from, char *out, uint32_t max)
{
    uint32_t i = 0;

    if (from < 0 || (uint32_t)from > b->used)
        return 0;
    while (i < max && (uint32_t)from < b->used) {
        out[i] = b->base[from];
        from++;
        i++;
    }
    out[i] = 0;
    return out;
}
