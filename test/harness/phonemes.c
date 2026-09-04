/* What the language decided a word was made of, as phonemes.
 *
 * `eciGeneratePhonemes' asks the engine to say what it would say, in the
 * engine's own alphabet, instead of saying it. That is a sharper thing to
 * check than the audio: a wrong letter-to-sound answer names itself here,
 * where in a wave file it is a different hash over forty thousand samples.
 *
 * This is our side. `reference/phontry.c' is the same driver against IBM's
 * own objects and prints the same shape, so the two outputs are held
 * against each other line for line by test/harness/phonemes.sh.
 *
 * The text comes from a file, one case to a line, rather than from the
 * command line: an argument handed to a Windows binary under Wine is
 * recoded on the way in, and a case with an accented letter in it then
 * reaches the two engines as different bytes and looks like a difference
 * that is not one.
 *
 * usage: phonemes <cases.txt>
 */

#include <stdio.h>
#include <string.h>

#include <eci.h>

/* Room for what one case comes to, and for one line of it. */
#define PHON_ROOM 8192
#define LINE_ROOM 4096

static char phon[PHON_ROOM];
static char said[PHON_ROOM * 4];

static int ECICALL on_message(ECIHand h, ECIMessage msg, int param, void *data)
{
    (void)h;
    (void)data;
    if (msg == eciPhonemeBuffer)
        strncat(said, phon, (size_t)param);
    return eciDataProcessed;
}

int main(int argc, char **argv)
{
    unsigned int langs[32];
    int          n = 0;
    ECIHand      h;
    FILE        *f;
    char         line[LINE_ROOM];

    if (argc != 2) {
        fprintf(stderr, "usage: phonemes <cases.txt>\n");
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (f == 0) {
        fprintf(stderr, "phonemes: cannot read %s\n", argv[1]);
        return 1;
    }

    if (eciGetAvailableLanguages(0, &n) || n < 1) {
        fprintf(stderr, "phonemes: no language in this build\n");
        return 1;
    }
    if (n > 32)
        n = 32;
    eciGetAvailableLanguages(langs, &n);

    while (fgets(line, sizeof line, f) != 0) {
        size_t len = strlen(line);

        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = 0;
        if (len == 0)
            continue;

        /* An instance a case, because the engine's second utterance is not
           its first and this is meant to be compared. */
        h = eciNewEx((int)langs[0]);
        if (h == NULL_ECI_HAND) {
            fprintf(stderr, "phonemes: no instance\n");
            return 1;
        }

        eciRegisterCallback(h, on_message, 0);
        eciSetParam(h, eciWantPhonemeIndices, 1);
        eciSetParam(h, eciSynthMode, 1);
        eciAddText(h, line);

        said[0] = 0;
        memset(phon, 0, sizeof phon);
        eciGeneratePhonemes(h, sizeof phon, phon);
        printf("[%s]\n", said);

        eciDelete(h);
    }

    fclose(f);
    return 0;
}
