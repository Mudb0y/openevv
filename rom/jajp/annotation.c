/* The annotations riding along with the text.
 *
 * A caller may put marks in what it sends -- a pause, an index, a phoneme
 * spelled out by hand -- and those marks are not Japanese and must not go
 * through the analyser. InputChar lifts them out of the text as it reads it
 * and leaves them here, each remembered with the position in the text it
 * belonged to; the output side asks for them back as it passes that position,
 * so what the engine finally speaks has them in the same places the caller
 * put them.
 *
 * It is a ring of 128, which is IBM's number and its only bound: a sentence
 * with more annotations than that in it overwrites the oldest without saying
 * so. The fields are named here rather than kept at IBM's offsets, because
 * nothing outside this file reads them -- unlike DictSearch, whose layout
 * rom/jajp/dictsearch.c has to share with the sweep.
 *
 * Held to IBM's answer by test/romprims.sh.
 */

#include <string.h>
#include "jprom.h"

/* What the engine's own annotations begin with. Read out of the object rather
   than decoded from the names they are stored under. */
static const char ANNO_TS[] = "`ts";     /* a phoneme spelled by hand */
static const char ANNO_UI[] = "`ui";     /* the caller's own index mark */
static const char ANNO_G[]  = "`g";
static const char ANNO_I[]  = "`i";
static const char ANNO_P[]  = "`p";      /* a pause */
static const char ANNO_ESC[] = " \\\\";
static const char ANNO_SP[]  = " ";

/* The two an annotation may be, and what anything else is. The table is two
   rows in IBM's object and its length is written into the code as two, so
   there is nothing to lift. */
#define ANNO_HAND       0        /* begins `ts */
#define ANNO_INDEX      1        /* begins `ui */
#define ANNO_OTHER      2

/* A fresh one, belonging to the analysis that made it. */
Annotation *an_ctor(Annotation *a, void *analysis)
{
    a->analysis = analysis;
    a->count = 0;
    a->head = 0;
    return a;
}

/* Which kind an annotation is, by what it begins with. */
int32_t an_GetRomHandAnnoType(Annotation *a, const char *s)
{
    static const struct {
        int32_t     type;
        const char *name;
    } KIND[2] = { { ANNO_HAND, ANNO_TS }, { ANNO_INDEX, ANNO_UI } };
    int32_t type = ANNO_OTHER;
    int     i;

    (void)a;
    for (i = 0; i < 2; i++)
        if (strncmp(s, KIND[i].name, strlen(KIND[i].name)) == 0) {
            type = KIND[i].type;
            break;
        }
    return type;
}

/* Keep one, with the place in the text it belonged to.
 *
 * Answers nought when there was no room for a copy of it, and one otherwise.
 * The ring is not checked for being full: a hundred and twenty-ninth
 * annotation overwrites the first, which is IBM's and is left alone. */
int32_t an_Save(Annotation *a, char *text, int16_t len, int16_t at)
{
    uint8_t slot = (uint8_t)((a->head + a->count) % ANNO_N);

    a->at[slot] = at;
    a->text[slot] = (char *)cpp_new((uint32_t)(len + 1));
    if (a->text[slot] == NULL)
        return 0;

    strncpy(a->text[slot], text, (size_t)len);
    a->text[slot][len] = 0;
    a->type[slot] = an_GetRomHandAnnoType(a, text);
    a->count++;
    return 1;
}

/* The last annotation of one kind that belongs before `before'.
 *
 * The ring is walked backwards from the newest, so the nearest one wins.
 * Answers null where there is none. */
const char *an_GetLastAnno(Annotation *a, int16_t before, int32_t type)
{
    int32_t last = (a->head + a->count) % ANNO_N - 1;
    int32_t first = a->head % ANNO_N;

    for (; first <= last; last--) {
        if (a->type[last] != type)
            continue;
        if (a->at[last] >= before)
            continue;
        return a->text[last];
    }
    return NULL;
}

/* Give up the oldest. */
void an_Remove(Annotation *a)
{
    cpp_delete(a->text[a->head]);
    a->text[a->head] = NULL;
    a->at[a->head] = -1;
    a->type[a->head] = ANNO_OTHER;
    a->head = (uint8_t)((a->head + 1) % ANNO_N);
    a->count--;
}

/* Give up every one that belongs after `after'.
 *
 * A slip of IBM's is kept here and in Flush below: the kind is cleared at the
 * head of the ring rather than at the slot being given up. Where the slot is
 * the head the two are the same, which is why Remove above is right and these
 * two are not; nothing has been seen to depend on it, and changing it would be
 * a difference from IBM rather than a fix. */
void an_RemoveAfter(Annotation *a, int16_t after)
{
    int16_t gone = 0;
    int32_t i;

    for (i = 0; i < a->count; i++) {
        uint8_t slot = (uint8_t)((a->head + i) % ANNO_N);

        if (a->at[slot] <= after)
            continue;
        cpp_delete(a->text[slot]);
        a->text[slot] = NULL;
        a->at[slot] = -1;
        a->type[a->head] = ANNO_OTHER;
        gone++;
    }
    a->count = (uint8_t)(a->count - gone);
}

/* Write every annotation into the output and empty the ring.
 *
 * `escape' says to put the engine's own escape in front of anything that is
 * not one of its three known marks, and `dropPause' says to throw a pause
 * away rather than pass it on. Answers nought where the output would not take
 * what it was given, and one otherwise. */
int32_t an_Flush(Annotation *a, int32_t escape, DynaBuf *out, int32_t dropPause)
{
    int32_t i;

    for (i = 0; i < a->count; i++) {
        uint8_t slot = (uint8_t)((a->head + i) % ANNO_N);

        if (escape == 0
            || strncmp(a->text[slot], ANNO_G, 2) == 0
            || strncmp(a->text[slot], ANNO_I, 2) == 0
            || strncmp(a->text[slot], ANNO_UI, 3) == 0) {
            if (!dynaBufAddString(out, ANNO_ESC, 1))
                return 0;
        }

        if (dropPause == 0 || strncmp(a->text[slot], ANNO_P, 2) != 0) {
            if (!dynaBufAddString(out, a->text[slot], 1))
                return 0;
            if (!dynaBufAddString(out, ANNO_SP, 1))
                return 0;
        }

        cpp_delete(a->text[slot]);
        a->text[slot] = NULL;
        a->at[slot] = -1;
        a->type[a->head] = ANNO_OTHER;
    }

    a->head = 0;
    a->count = 0;
    return 1;
}
