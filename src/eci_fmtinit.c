/* Registering the two sound formats this build has, once.
 *
 * The guard is a static of its own rather than the compiler's one-time
 * initialiser, so a failed attempt can be tried again: it is only set once
 * both formats have gone in.
 */

#include <stdint.h>

extern int32_t registerSoundFormat(void *fmt);

extern void *ealAudioSoundFormat;
extern void *PCM16MonoSoundFormat;

static int32_t alreadyRun;

int32_t initializeSoundFormats(void)
{
    if (alreadyRun)
        return 1;

    if (!registerSoundFormat(&ealAudioSoundFormat))
        return 0;
    if (!registerSoundFormat(&PCM16MonoSoundFormat))
        return 0;

    alreadyRun = 1;
    return 1;
}
