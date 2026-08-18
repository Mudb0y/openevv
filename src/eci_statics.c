/* Storage the differential build still gets from the original.
 *
 * Three blocks that have to exist exactly once and that nothing else of
 * ours defines: the standard voice table, the flag the engine guards its
 * first start-up with, and the object whose construction fills the voice
 * table. In the differential build all three are still the original's,
 * because its own object holds them and its own code reaches them; this
 * file belongs to the native build alone.
 */

/* One language and dialect, sixteen records of eighty bytes each and a
   word in front, over eighteen families of two dialects. */
#define SV_FAMILY_BYTES 0x0a08
#define FAMILIES        18

/* A mutex, which is all the first-time flag is. */
#define MUTEX_BYTES 0x0c

char standardVoices[FAMILIES * SV_FAMILY_BYTES];
char protectFirstTime[MUTEX_BYTES];

/* Nothing but somewhere for the constructor to point at: it fills the
   table above and keeps nothing of its own. */
char initializeStandardVoices[4];
