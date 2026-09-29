/* The accent layer: sounds, durations and pitch a language module has not
 * got, laid over the ones it has.
 *
 * A language read by another front end reaches the engine as pronunciation
 * annotations in the phones of whichever module speaks it, and a module can
 * only say the phones it was written for. So the text may carry, beside each
 * annotation, what the phones were meant to be: a retroflex where the module
 * has a dental, a vowel it has no name for, a tone on every syllable. That is
 * markup, in braces, and evv_accent.c says what it looks like. It is taken
 * out of the text before the rules see any of it, so a module reads exactly
 * the annotations it always read, and what it was meant to say is kept to one
 * side until the phones come round to be synthesised.
 *
 * They come round one at a time: the rules name each phone as they hand its
 * stretch of sound to the synthesiser, which is where this stands. Every
 * frame of parameters on its way to the formant engine passes through
 * evv_accent_run, and a frame belonging to a phone that was meant to be
 * something else is changed into that: formants moved, voicing begun sooner
 * or later, a stretch lengthened, the pitch replaced by the contour of the
 * tone the syllable carries.
 *
 * None of it happens unless the text asks. Text with no markup in it is not
 * looked at, no frame is touched, and every language the engine speaks by
 * itself says what it said before. test/matrix.sh is what holds that.
 */

#ifndef EVV_ACCENT_H
#define EVV_ACCENT_H

#include <stdint.h>

#define EVV_ACCENT_PARMS 62

/* Text on its way in. Answers a copy with the markup taken out, which the
   caller gives back with evv_accent_release, or nought where there was none
   and the text is to be used as it stands. */
char *evv_accent_strip(void *machine, const char *text, uint32_t len,
                       uint32_t *out_len);
void  evv_accent_release(char *text);

/* What the rules are about to read: the voice settings are in it. */
void  evv_accent_sentence(void *machine, const char *text);

/* The input was thrown away, or the machine was. */
void  evv_accent_clear(void *machine);
void  evv_accent_free(void *machine);

/* A phone the rules have named, just before its stretch is synthesised. The
   record is the phone statement's own, class first. */
void  evv_accent_place(void *machine, const char *name,
                       const unsigned char *record, int length);

/* Whether any of this is in force for the machine. */
int   evv_accent_on(void *machine);

/* The frames that came of asking for the phone just named, which was asked
   for from `from' to `to' in the arrays' own milliseconds. The first of them
   belongs to the moment `first' and there is one every `step'; each is
   EVV_ACCENT_PARMS words, of which the first is how long it lasts. `last'
   says nothing more is coming. What is to sound is handed to `emit' a frame
   at a time, which answers nought to stop. Answers nought if it was
   stopped. */
typedef int (*evv_accent_emit)(void *context, const int32_t *frame);

int   evv_accent_run(void *machine, int32_t from, int32_t to, int32_t first,
                     int32_t step, const int32_t *frames, int count,
                     int last, evv_accent_emit emit, void *context);

/* How many milliseconds of sound had been asked for by the time the arrays
   had reached this moment: the same number unless a stretch was lengthened
   or shortened on the way. An index mark is timed by it. */
int32_t evv_accent_sounded(void *machine, int32_t array_ms);

/* How many milliseconds of frames have come and not yet been sent on, which
   is how far the sound is behind the arrays. */
int32_t evv_accent_held(void *machine);

#endif
