/* Compare the statement table we lifted against the one it was lifted from.

   Everything in it is reached by pointer, so the comparison follows: the
   names as text, the fields one by one, the value names each field
   declares, what a fresh record holds, and what each reader and writer does
   to a record. */

#include <stdio.h>
#include <string.h>

#include "delta.h"

extern const delta_stmt ibm_vstmtbl[];

static int bad;

static void differs(const char *what, int i, int k)
{
    printf("  %s differs at type %d field %d\n", what, i, k);
    bad++;
}

static int same_text(const char *a, const char *b)
{
    if (a == 0 || b == 0)
        return a == b;
    return strcmp(a, b) == 0;
}

int main(void)
{
    int i, k, j;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("statement table: ours against the original\n");

    for (i = 0; i < 10; i++) {
        const delta_stmt *a = &vstmtbl[i];
        const delta_stmt *b = &ibm_vstmtbl[i];

        if (!same_text(a->name, b->name))
            differs("name", i, -1);
        if (a->nfields != b->nfields || a->length != b->length
            || a->stride != b->stride || a->varlen != b->varlen
            || a->whole_token != b->whole_token
            || a->unknown_18 != b->unknown_18
            || a->unknown_1c != b->unknown_1c
            || a->unknown_38 != b->unknown_38
            || a->unknown_3c != b->unknown_3c
            || a->marks[0] != b->marks[0] || a->marks[1] != b->marks[1]
            || a->walkable != b->walkable)
            differs("the numbers", i, -1);
        if ((a->deflt == 0) != (b->deflt == 0)
            || (a->deflt != 0
                && memcmp(a->deflt, b->deflt, (size_t)a->length) != 0))
            differs("what a fresh record holds", i, -1);
        if ((a->variants == 0) != (b->variants == 0))
            differs("whether it has variants", i, -1);
        else if (a->variants != 0) {
            int n;

            for (n = 0; n < 256; n++)
                if (a->variants[n] != b->variants[n]) {
                    printf("  variants of type %d differ at %d: %d vs %d\n",
                           i, n, a->variants[n], b->variants[n]);
                    bad++;
                    break;
                }
        }

        for (k = 0; k < a->nfields; k++) {
            const delta_fielddesc *x = &a->fields[k];
            const delta_fielddesc *y = &b->fields[k];
            unsigned char one[64], two[64];

            if (!same_text(x->name, y->name))
                differs("a field name", i, k);
            if (!same_text(x->format, y->format))
                differs("a field format", i, k);
            if (x->nvalues != y->nvalues || x->kind != y->kind
                || x->flag != y->flag || x->unknown_0c != y->unknown_0c)
                differs("a field's numbers", i, k);
            if ((x->values == 0) != (y->values == 0))
                differs("whether a field names its values", i, k);
            if (x->values != 0 && y->values != 0)
                for (j = 0; j < x->nvalues; j++)
                    if (!same_text(((const char *const *)x->values)[j],
                                   ((const char *const *)y->values)[j]))
                        differs("a value name", i, k);

            /* What the reader answers, as an offset into a record, and
               what the writer does to one. */
            if ((char *)a->get[k](one) - (char *)one
                != (char *)b->get[k](two) - (char *)two)
                differs("a reader", i, k);

            memset(one, 0, sizeof(one));
            memset(two, 0, sizeof(two));
            {
                static const unsigned char v[4] = { 0xa5, 0x5a, 0xc3, 0x3c };

                a->put[k](one, v);
                b->put[k](two, v);
            }
            if (memcmp(one, two, sizeof(one)) != 0)
                differs("a writer", i, k);
        }
    }

    printf("statement table: %d differences\n", bad);
    return bad != 0;
}
