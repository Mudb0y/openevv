/* The odd jobs the language asks for on the way up, and one on the way
 * back down.
 *
 * Setting up the parameter streams the synthesiser reads, telling the
 * dictionary which stream it works in, saying which statement types are
 * allowed to run out of order, and undoing a rule that failed.
 */

#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "delta.h"
#include "eci_eloqc.h"

/* The block the machine keeps for ECI, and the flag in it that says the
   parameter streams have already been made. */

/* A token carries its value as an int16 two bytes in, and says what kind
   of value that is in the two bytes before. */
#define TOKEN_KIND(t)  (*(const int16_t *)(t))
#define TOKEN_VALUE(t) (*(const int16_t *)((const char *)(t) + 2))

/* The kind an indirect token carries, where the value is in the second
   half rather than the first. */
#define KIND_INDIRECT (-4)

/* The end of the list of fields that decide the nonsequential flags. */
#define FIELD_LIST_END (-1)

/* Every parameter stream the synthesiser reads, in the order the language
   numbers them. */
static const char *const stream_names[] = {
    "F0",  "F1",  "F2",  "F3",  "F4",  "F5",
    "B1",  "B2",  "B3",  "B4",  "B5",
    "FNP", "FNZ", "FTP", "FTZ",
    "TL",  "OQ",  "FL",  "DI",
    "AV",  "AH",  "AF",
    "A1F", "A2F", "A3F", "A4F", "A5F",
    "AB"
};

#define STREAM_COUNT ((int16_t)(sizeof stream_names / sizeof stream_names[0]))

extern int32_t createStreamArrays(void *d, int16_t count);
extern int32_t initStreamArray(void *d, int16_t which, const char *name);
extern int32_t setUserDictInputStream(delta_state *d, const char *name);

/* Made once. Asking twice is not an error; the second answer is the same
   as the first. */
int32_t initStreamArrays(delta_state *d)
{
    int16_t i;

    if (ELOQ_STREAMS(d))
        return 1;
    ELOQ_STREAMS(d) = 1;

    createStreamArrays(d, STREAM_COUNT);
    for (i = 0; i < STREAM_COUNT; i++)
        if (!initStreamArray(d, i, stream_names[i]))
            return 0;

    return 1;
}

/* Which stream the user dictionary rewrites in, and the three volume
   numbers handed back through the tokens the rule passed. */
int32_t init_user_dicts(delta_state *d, void *a, void *b, void *c)
{
    setUserDictInputStream(d, "inp");

    *(int16_t *)((char *)a + 2) = 0;
    *(int16_t *)((char *)b + 2) = 1;
    *(int16_t *)((char *)c + 2) = 2;
    return 0;
}

/* The list of fields that decide the nonsequential flags, which is kept
   as the field numbers themselves followed by a negative. */
void clearnonseqIndex(delta_state *d)
{
    EVV_AT(int8_t *, EVV_AT(delta_stack *, d->stack)->nsq_fields)[0] = FIELD_LIST_END;
}

void setnonseqIndex(delta_state *d, int8_t field)
{
    int32_t i = 0;

    while (EVV_AT(int8_t *, EVV_AT(delta_stack *, d->stack)->nsq_fields)[i] != FIELD_LIST_END) {
        if (EVV_AT(int8_t *, EVV_AT(delta_stack *, d->stack)->nsq_fields)[i] == field)
            return;
        i++;
    }

    EVV_AT(int8_t *, EVV_AT(delta_stack *, d->stack)->nsq_fields)[i] = field;
    EVV_AT(int8_t *, EVV_AT(delta_stack *, d->stack)->nsq_fields)[i + 1] = FIELD_LIST_END;
}

/* Say which statement types may run out of order. Everything is cleared
   first, so this says what the whole set is rather than adding to it. The
   count comes in a token and the types follow it one to an argument. */
int32_t setNonSequential(delta_state *d, const void *count_tok, ...)
{
    va_list      ap;
    const void  *one;
    int32_t      i, left;

    for (i = 0; i < (int32_t)d->nstmts; i++)
        EVV_AT(int8_t *, EVV_AT(delta_vars *, d->vars)->nsq_marks)[i] = 0;
    clearnonseqIndex(d);

    left = TOKEN_VALUE(count_tok);
    if (left <= 0)
        return 0;

    /* The original walks the stack itself, which is the same thing on the
       machine it was built for and nothing at all on the others. */
    va_start(ap, count_tok);
    one = EVV_AT(const void *, va_arg(ap, int32_t));

    while (left != 0 && one != 0) {
        int32_t stm;

        if (TOKEN_KIND(one) == KIND_INDIRECT)
            stm = TOKEN_VALUE(one);
        else
            stm = TOKEN_KIND(one);

        if (stm >= 0 && stm < (int32_t)d->nstmts) {
            EVV_AT(int8_t *, EVV_AT(delta_vars *, d->vars)->nsq_marks)[stm] = 1;
            setnonseqIndex(d, (int8_t)stm);
        }

        one = EVV_AT(const void *, va_arg(ap, int32_t));
        left--;
    }

    va_end(ap);
    return 0;
}

/* Undo the last rule, and answer what the unwinding said. The machine can
   be told to skip one of these, which is what the flag is for: it is spent
   rather than tested, and the answer that time is the flag itself. */
int32_t backtrack_function(delta_state *d, int32_t *slot)
{
    int32_t rc;

    if (d->unknown_3c != 0) {
        rc = d->unknown_3c;
        d->unknown_3c = 0;
        return rc;
    }

    rc = vback(d, *slot);
    *slot = 0;
    return rc;
}
