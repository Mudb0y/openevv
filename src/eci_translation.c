/* What a stored key means: three runs of bytes and a part of speech.
 *
 * The three are what the key is read as, the word it stands for, and a third
 * string the dictionary API carries through. Each is copied on the way in, and
 * `ok' is set only when all three were copied, so a caller can tell a
 * half-built one from a whole one.
 *
 * The default constructor leaves `ok' unset in the original -- it writes the
 * part of speech and stops -- so a default-built Translation reads as whatever
 * was in that memory. Ours writes nought. Reproducing uninitialised memory is
 * neither possible nor a fidelity.
 */

#include <stdio.h>
#include <string.h>
#include "eci_key.h"

extern void *cpp_new(uint32_t n);
extern void  cpp_delete(void *p);

/* One run copied into memory of its own, nought-terminated one past the end.
   A length of nought or less gets nothing at all. */
char *tr_set(Translation *t, const char *from, int32_t len)
{
    char *to;

    (void)t;
    if (len <= 0)
        return 0;
    to = (char *)cpp_new((uint32_t)(len + 1));
    if (to != 0) {
        memcpy(to, from, (size_t)len);
        to[len] = 0;
    }
    return to;
}

Translation *tr_ctor(Translation *t, const char *value, int32_t valueLen,
                     const char *word, const char *extra, int32_t pos)
{
    memset(t, 0, sizeof *t);

    t->value = tr_set(t, value, valueLen);
    if (t->value == 0)
        return t;
    t->valueLen = valueLen;

    t->word = tr_set(t, word, (int32_t)strlen(word));
    if (t->word == 0)
        return t;
    t->wordLen = (int32_t)strlen(word);

    t->extra = tr_set(t, extra, (int32_t)strlen(extra));
    if (t->extra == 0)
        return t;
    t->extraLen = (int32_t)strlen(extra);

    t->pos = pos;
    t->ok = 1;
    return t;
}

/* The empty one, which the arrays of Translations are built with. The part of
   speech starts at one. */
Translation *tr_ctorEmpty(Translation *t)
{
    memset(t, 0, sizeof *t);
    t->pos = 1;
    return t;
}

void tr_dtor(Translation *t)
{
    if (t->value != 0) {
        cpp_delete(t->value);
        t->value = 0;
    }
    if (t->word != 0) {
        cpp_delete(t->word);
        t->word = 0;
    }
    if (t->extra != 0) {
        cpp_delete(t->extra);
        t->extra = 0;
    }
    t->extraLen = 0;
    t->wordLen = 0;
    t->valueLen = 0;
}

/* Copied deep: the three runs are taken again rather than shared, because the
   one being copied from may go. */
void tr_assign(Translation *t, const Translation *from)
{
    if (t == from)
        return;

    tr_dtor(t);
    t->value = tr_set(t, from->value, from->valueLen);
    t->valueLen = from->value != 0 ? from->valueLen : 0;
    t->word = tr_set(t, from->word, from->wordLen);
    t->wordLen = from->word != 0 ? from->wordLen : 0;
    t->extra = tr_set(t, from->extra, from->extraLen);
    t->extraLen = from->extra != 0 ? from->extraLen : 0;
    t->pos = from->pos;
    t->ok = from->ok;
}

/* Each run as its length and then its bytes, and the part of speech last. */
void tr_save(Translation *t, FILE *f)
{
    fwrite(&t->valueLen, 4, 1, f);
    if (t->valueLen > 0)
        fwrite(t->value, 1, (size_t)t->valueLen, f);
    fwrite(&t->wordLen, 4, 1, f);
    if (t->wordLen > 0)
        fwrite(t->word, 1, (size_t)t->wordLen, f);
    fwrite(&t->extraLen, 4, 1, f);
    if (t->extraLen > 0)
        fwrite(t->extra, 1, (size_t)t->extraLen, f);
    fwrite(&t->pos, 4, 1, f);
}

/* And back, with everything let go first. A run whose length is positive and
   whose memory could not be had is what makes this answer nought, and then
   nothing of it is kept. */
int32_t tr_load(Translation *t, FILE *f)
{
    int32_t failed = 0;

    if (t->valueLen > 0 && t->value != 0)
        cpp_delete(t->value);
    if (t->wordLen > 0 && t->word != 0)
        cpp_delete(t->word);
    if (t->extraLen > 0 && t->extra != 0)
        cpp_delete(t->extra);

    t->extra = 0;
    t->word = 0;
    t->value = 0;
    t->extraLen = 0;
    t->wordLen = 0;
    t->valueLen = 0;

    fread(&t->valueLen, 4, 1, f);
    if (t->valueLen > 0) {
        t->value = (char *)cpp_new((uint32_t)(t->valueLen + 1));
        if (t->value != 0) {
            fread(t->value, 1, (size_t)t->valueLen, f);
            t->value[t->valueLen] = 0;
        } else {
            failed = 1;
        }
    }

    fread(&t->wordLen, 4, 1, f);
    if (t->wordLen > 0) {
        t->word = (char *)cpp_new((uint32_t)(t->wordLen + 1));
        if (t->word != 0) {
            fread(t->word, 1, (size_t)t->wordLen, f);
            t->word[t->wordLen] = 0;
        } else {
            failed = 1;
        }
    }

    fread(&t->extraLen, 4, 1, f);
    if (t->extraLen > 0) {
        t->extra = (char *)cpp_new((uint32_t)(t->extraLen + 1));
        if (t->extra != 0) {
            fread(t->extra, 1, (size_t)t->extraLen, f);
            t->extra[t->extraLen] = 0;
        } else {
            failed = 1;
        }
    }

    fread(&t->pos, 4, 1, f);

    if (failed) {
        if (t->value != 0)
            cpp_delete(t->value);
        if (t->word != 0)
            cpp_delete(t->word);
        if (t->extra != 0)
            cpp_delete(t->extra);
        t->extraLen = 0;
        t->wordLen = 0;
        t->valueLen = 0;
        t->extra = 0;
        t->word = 0;
        t->value = 0;
        return 0;
    }
    return 1;
}

void tr_dump(Translation *t)
{
    int32_t i;

    printf("Translation: ");
    for (i = 0; i < t->valueLen; i++)
        printf("%c", t->value[i]);
    for (i = 0; i < t->wordLen; i++)
        printf("%c", t->word[i]);
    for (i = 0; i < t->extraLen; i++)
        printf("%c", t->extra[i]);
    printf("\nPart of speech: %d\n", (int)t->pos);
}
