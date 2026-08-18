/* Where the standard voices live.
 *
 * Only the native build defines this. The differential build reads and
 * writes the original's own block, because the original still has readers
 * of its own in the object it sits in, and two blocks would be worse than
 * one.
 */

/* One language and dialect, sixteen records of eighty bytes each and a
   word in front, over eighteen families of two dialects. */
#define SV_FAMILY_BYTES 0x0a08
#define FAMILIES        18

char standardVoices[FAMILIES * SV_FAMILY_BYTES];
