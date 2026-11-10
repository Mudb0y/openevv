/* Russian text as the pronunciations lang/ruru speaks. eci_russian.c says how.
   Built only when LANGS names lang/ruru. */

#ifndef ECI_RUSSIAN_H
#define ECI_RUSSIAN_H

#include <stddef.h>
#include <stdint.h>

/* The code points in[0..n) as Russian is said, each word a pronunciation
   annotation in the English module's alphabet: a buffer
   from malloc that the caller frees, its length in *out_n. Answers nought
   only when there is no memory. spelling may be nought. */
uint32_t *ru_transcribe(const uint32_t *in, size_t n, size_t *out_n,
                        int *spelling);

/* Whether the engine is spelling when the text starts, which ru_transcribe
   reads and leaves as the text left it. One per instance, by its address,
   and safe to call from several instances' threads at once;
   ru_spellingForget clears it when the instance stops or is made. */
int ru_spellingGet(const void *key);
void ru_spellingSet(const void *key, int spelling);
void ru_spellingForget(const void *key);

/* And how to give that buffer back. Not free(): the engine's own files have
   free() turned into the arena's by evv_arena.h, and this buffer is not the
   arena's. */
void ru_free(uint32_t *p);

#endif
