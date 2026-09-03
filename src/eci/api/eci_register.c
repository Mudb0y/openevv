/* Registering the engine with the machine it is installed on.
 *
 * Deliberately not reproduced. The original writes an eci.ini file next to
 * itself saying where it is, what version it is, and which voices and
 * phonemes it offers, so that something else can read it later. All four
 * of these are reached only from reg(), an entry point an installer calls
 * once; nothing in a speaking engine calls it, nothing reads back what it
 * writes, and the settings the engine actually runs on are the built-in
 * ones in eci_ini_enus.c rather than anything in that file.
 *
 * The rules name all four in their table of primitives, so the table needs
 * an address for each. These are those addresses and nothing more. A port
 * whose targets include a games console and a music player has nowhere to
 * write such a file and no installer to write it.
 *
 * They answer the way the original answers on a machine where it has not
 * been told an install directory, which is every machine that has not run
 * its installer: refused. The fourth is nought because the original's own
 * is an empty function that answers nought.
 */

#include <stdint.h>
#include "delta.h"

/* What the original answers when it cannot write the file. */
#define REFUSED (-1)

int32_t registerLanguage(delta_state *d, ...)
{
    (void)d;
    return REFUSED;
}

int32_t registerPhoneme(delta_state *d, ...)
{
    (void)d;
    return REFUSED;
}

int32_t registerVoice(delta_state *d, ...)
{
    (void)d;
    return REFUSED;
}

int32_t registerSAPIVoice(delta_state *d, ...)
{
    (void)d;
    return 0;
}
