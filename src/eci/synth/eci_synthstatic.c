/* What the synthesis thread keeps for all of itself at once.
 *
 * Four things belong to the class rather than to any one thread: how many
 * of them are about, the phoneme table and the sound manager they share,
 * and the lock that makes sure only one of them does the shared setting-up.
 *
 * There is nothing to them but their size and their names; everything that
 * reads or writes them lives elsewhere. They are here because a class's own
 * variables have to be defined exactly once somewhere, and this is that
 * once.
 */

#include <stdint.h>
#include "eci_synththread.h"
#include "evv_abi.h"

void   *st_soundManager;
void   *st_phonemes;
int32_t st_nRefPointers;
uint8_t st_protectInitialization[0xc];

ALIAS("?m_soundManager@SynthThread@@0PAVSoundManager@@A", "st_soundManager");
ALIAS("?m_phonemes@SynthThread@@0PAVPhonemes@@A", "st_phonemes");
ALIAS("?nRefPointers@SynthThread@@0HA", "st_nRefPointers");
ALIAS("?m_protectInitialization@SynthThread@@0VMutex@@A",
      "st_protectInitialization");
