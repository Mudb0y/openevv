/* A key in the stored dictionary: some bytes and how many.
 *
 * This is one of the five small classes the skip list is built out of, and it
 * exists because the thing being stored is not a C string. A user-dictionary
 * key is a run of bytes that may hold a nought in the middle, so the length
 * travels with it, and a nought is written one past the end anyway so that
 * anything wanting a string gets one.
 *
 * IBM's own file has three faults in it, all of the same kind and all left
 * out here. `operator=' frees the old bytes and then calls `set', which frees
 * them again; `load' does the same; and neither nulls the pointer in between.
 * Ours nulls it. Nothing in the engine calls `operator=' at all, and `load'
 * is only reached through a file that has just been opened, so neither has
 * ever fired -- but a double free is not a behaviour to reproduce faithfully.
 *
 * `dump' prints to standard output, which is what it does in the original,
 * and is left in because test/romprims.sh is what reads it: it is the only
 * way to see what a stored key holds without knowing the file format.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "eci_key.h"

extern void *cpp_new(uint32_t n);
extern void  cpp_delete(void *p);

/* The bytes are copied because the caller's are a working buffer. A length of
   nought or less means no bytes at all rather than an empty run. */
void key_set(Key *k, char *bytes, int32_t len)
{
    k->len = len;
    if (k->len <= 0) {
        k->bytes = 0;
        return;
    }

    if (k->bytes != 0) {
        cpp_delete(k->bytes);
        k->bytes = 0;
    }
    k->bytes = (char *)cpp_new((uint32_t)(len + 1));
    if (k->bytes != 0) {
        strncpy(k->bytes, bytes, (size_t)len);
        k->bytes[len] = 0;
    } else {
        k->len = 0;
    }
}

Key *key_ctor(Key *k, char *bytes, int32_t len)
{
    k->bytes = 0;
    k->len = 0;
    key_set(k, bytes, len);
    return k;
}

void key_dtor(Key *k)
{
    if (k->bytes != 0) {
        cpp_delete(k->bytes);
        k->bytes = 0;
    }
    k->len = 0;
}

void key_assign(Key *k, const Key *from)
{
    if (k == from)
        return;
    if (k->bytes != 0) {
        cpp_delete(k->bytes);
        k->bytes = 0;
    }
    key_set(k, from->bytes, from->len);
}

/* Which of two keys sorts first, over at most `len' bytes; nought for the
   length means the other key's own. Where the bytes agree as far as they are
   compared, the shorter key is the smaller. */
int32_t key_lessthan(Key *k, Key *other, int32_t len)
{
    int32_t most;
    int32_t order;

    if (len == 0)
        len = other->len;
    most = len > k->len ? k->len : len;

    order = strncmp(k->bytes, other->bytes, (size_t)most);
    if (order == 0) {
        if (len == 0)
            len = other->len;
        return k->len < len;
    }
    return order < 0;
}

/* Whether two keys are the same, which needs the lengths to agree first. */
int32_t key_match(Key *k, Key *other, int32_t len)
{
    if (len == 0)
        len = other->len;
    if (len != k->len)
        return 0;
    return strncmp(k->bytes, other->bytes, (size_t)len) == 0;
}

void key_save(Key *k, FILE *f)
{
    fwrite(&k->len, 4, 1, f);
    if (k->len > 0)
        fwrite(k->bytes, 1, (size_t)k->len, f);
}

int32_t key_load(Key *k, FILE *f)
{
    if (k->len > 0 && k->bytes != 0) {
        cpp_delete(k->bytes);
        k->bytes = 0;
    }

    fread(&k->len, 4, 1, f);
    if (k->len <= 0) {
        k->bytes = 0;
        return 1;
    }

    k->bytes = (char *)cpp_new((uint32_t)(k->len + 1));
    if (k->bytes == 0)
        return 0;
    fread(k->bytes, 1, (size_t)k->len, f);
    k->bytes[k->len] = 0;
    return 1;
}

void key_dump(Key *k)
{
    int32_t i;

    printf("Key: ");
    for (i = 0; i < k->len; i++)
        printf("%c", k->bytes[i]);
    printf("\n");
}
