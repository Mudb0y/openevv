/* What the rules report back through.
 *
 * Six primitives the language calls when something has happened that the
 * caller asked to hear about: a word started, a phoneme was placed, the
 * voice changed, an annotation went by. Each is the same shape -- look in
 * the block the machine keeps for ECI, and if a callback was registered
 * there, call it with the value and the caller's own data word.
 *
 * The registering side of the same table is in eci_engsyn.c, which is why
 * the offsets are written the same way here rather than as a struct: the
 * block belongs to the layers above and they reach into it by offset.
 */

#include <stdint.h>
#include <string.h>
#include "delta.h"
#include "evv_arena.h"
#include "eci_eloqc.h"


/* Not a callback: the flag that says phoneme indices are wanted at all. */

#define CB_WORD   0x14
#define CB_ANNO   0x2c
#define CB_SPR    0x34
#define CB_VOICE  0x3c

/* Every one of them keeps the caller's own data word in the next slot. */
#define CB_DATA(off) ((off) + 4)

/* A token carries its value as an int16 two bytes in. */
#define TOKEN_VALUE(t) ((int32_t) * (const int16_t *)((const char *)(t) + 2))

typedef void (*WordFn)(int32_t value, void *data);
typedef void (*AnnoFn)(int32_t a, int32_t b, void *data);
typedef void (*SprFn)(void *data);
typedef int32_t (*VoiceFn)(int32_t value, void *a, void *b, void *c,
                           void *e, void *f, void *data);

extern int insertPhoneme(void *d, int32_t name, int32_t ms);

/* How long a phoneme name may be, which is four characters packed into a
   word rather than a string. */
#define PHONEME_NAME_MAX 4

/* The two values a phoneme is placed with are a proportion and a length,
   and the product is in thousandths. */
#define PHONEME_SCALE 1000

int32_t synthesizingWord(delta_state *d, const void *tok)
{
    if (ELOQ_CB(d, CB_WORD) != 0)
        ((WordFn)ELOQ_CB(d, CB_WORD))(TOKEN_VALUE(tok),
                                      ELOQ_CB(d, CB_DATA(CB_WORD)));
    return 0;
}

int32_t annoCallback(delta_state *d, const void *a, const void *b)
{
    if (ELOQ_CB(d, CB_ANNO) != 0)
        ((AnnoFn)ELOQ_CB(d, CB_ANNO))(TOKEN_VALUE(a), TOKEN_VALUE(b),
                                      ELOQ_CB(d, CB_DATA(CB_ANNO)));
    return 0;
}

int32_t esprCallback(delta_state *d)
{
    if (ELOQ_CB(d, CB_SPR) != 0)
        ((SprFn)ELOQ_CB(d, CB_SPR))(ELOQ_CB(d, CB_DATA(CB_SPR)));
    return 0;
}

/* The five that follow the value are pointers into tokens, each moved on
   to where the value sits, so that the callback can change them.

   With no callback registered this answers with the address of the block
   it just looked in. That is not an answer, it is what the original leaves
   in the register on the way out, and it is reproduced rather than tidied
   because nothing establishes that no rule reads it. */
int32_t voiceChangeCallback(delta_state *d, const void *value, void *b,
                            void *c, void *e, void *f, void *g)
{
    if (ELOQ_CB(d, CB_VOICE) == 0)
        return EVV_REF(ELOQC(d));

    return ((VoiceFn)ELOQ_CB(d, CB_VOICE))(TOKEN_VALUE(value),
                                           (char *)b + 2, (char *)c + 2,
                                           (char *)e + 2, (char *)f + 2,
                                           (char *)g + 2,
                                           ELOQ_CB(d, CB_DATA(CB_VOICE)));
}

/* One phoneme, named and timed, on its way to whoever asked for phoneme
   indices. The name is however disptok spells the token, cut to four
   characters and packed into a word; a name longer than that is refused
   rather than truncated.

   disptok is the debugger's own display and this port leaves it empty, so
   the name comes out blank until it is written. The buffer is cleared
   first, which the original does not do, because reading an untouched one
   is the difference between a blank name and whatever was on the stack. */
int32_t placePhoneme(delta_state *d, const void *tok, const void *a,
                     const void *b)
{
    char     name[0x38];
    int32_t  stream;
    int8_t   len;
    int32_t  packed;
    uint32_t proportion, length;

    if (!ELOQ_WANT_PHONEMES(d))
        return 0;

    stream = *(const int16_t *)tok;
    if (stream < 0 || stream >= (int32_t)d->nstmts)
        return 1;

    memset(name, 0, sizeof name);
    disptok(d, (const char *)tok + 4, stream, 0, name);

    len = (int8_t)strlen(name);
    if (len > PHONEME_NAME_MAX)
        return 1;
    while (len < PHONEME_NAME_MAX) {
        name[len] = 0;
        len++;
    }
    packed = *(int32_t *)name;

    proportion = (uint32_t)TOKEN_VALUE(a);
    length     = (uint32_t)TOKEN_VALUE(b);
    proportion = proportion * length / PHONEME_SCALE;

    insertPhoneme(d, packed, (int32_t)proportion);
    return 0;
}

int32_t placePhonemeText(delta_state *d, const void *tok, const void *a,
                         const void *b)
{
    return placePhoneme(d, tok, a, b);
}
