/* Ask IBM's own Japanese engine to speak Japanese script.
 *
 * Its speak.c takes the first language it is offered and never touches the
 * codeset, and Japanese written in Japanese produces nothing that way. The
 * language identifier carries a codeset in bits eight to fifteen -- which is
 * what isUnicodeCodeSet tests against 0x800 -- so this asks the same engine
 * the same thing several ways and says which of them makes a sound.
 *
 * The samples are counted through a callback rather than written to a file:
 * the engine will not take a sample buffer until it has somewhere to report
 * to, and the filename path is not the one speak.c walks or the one that
 * works. Getting that wrong cost two runs that looked like the engine failing
 * on Japanese when it was the harness failing on everything.
 *
 * What it found, which is the point of keeping it. IBM's Japanese engine does
 * speak Japanese script, and the thing that decides whether it does is how the
 * instance was made:
 *
 *     romaji,         eciNew()             18,293 samples
 *     shift-jis kana, eciNew()                  0 samples
 *     shift-jis kana, eciNewEx(0x80000)    13,266 samples
 *     ucs-2 kana,     eciNewEx(0x80800)    13,266 samples
 *
 * So eciNew is not the same as eciNewEx with the only language there is, which
 * is why reference/speak.c -- which tries eciNew first and only falls back --
 * produces nothing for Japanese and looked for a while like an engine that
 * could not do it. Setting the codeset parameter afterwards is refused and
 * does not help; the language given at creation is what carries it.
 *
 * Kana and romaji do not give the same samples, which is the romanizer doing
 * its work: it is not passing letters through. That difference is the target
 * anything transcribed from jpnrom and its neighbours has to hit.
 *
 * usage: jptry.exe
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

typedef void *ECIHand;

enum { FRAME = 1024 };
enum ECIMessage { eciWaveformBuffer, eciPhonemeBuffer, eciIndexReply };
enum ECICallbackReturn { eciDataNotProcessed, eciDataProcessed, eciDataAbort };

typedef enum ECICallbackReturn (__stdcall *ECICallback)(ECIHand,
                                                       enum ECIMessage,
                                                       long, void *);

ECIHand __stdcall eciNew(void);
ECIHand __stdcall eciNewEx(unsigned);
int     __stdcall eciDelete(ECIHand);
int     __stdcall eciAddText(ECIHand, const char *);
int     __stdcall eciSynthesize(ECIHand);
int     __stdcall eciSpeaking(ECIHand);
int     __stdcall eciSetParam(ECIHand, int, int);
int     __stdcall eciSetOutputBuffer(ECIHand, int, void *);
void    __stdcall eciRegisterCallback(ECIHand, ECICallback, void *);
int     __stdcall eciGetAvailableLanguages(unsigned *, int *);
void evvRunStaticInitialisers(void);

static short frame[FRAME];
static long said;

static enum ECICallbackReturn __stdcall on_message(ECIHand h,
                                                   enum ECIMessage msg,
                                                   long param, void *data)
{
    (void)h; (void)data;
    if (msg == eciWaveformBuffer)
        said += param;
    return eciDataProcessed;
}

/* Shift-JIS and UCS-2 for konnichiwa, and the romaji for comparison. */
static const unsigned char sjis[] = {
    0x82, 0xb1, 0x82, 0xf1, 0x82, 0xc9, 0x82, 0xbf, 0x82, 0xcd, 0x00
};
static const unsigned char ucs2[] = {
    0x53, 0x30, 0x93, 0x30, 0x6b, 0x30, 0x61, 0x30, 0x6f, 0x30, 0x00, 0x00
};
static const char roma[] = "konnichiwa";

static void one(const char *what, unsigned lang, int codeset, const void *text)
{
    ECIHand h = lang ? eciNewEx(lang) : eciNew();
    int spins = 0;
    long was;

    if (h == NULL) {
        printf("  %-32s no instance\n", what);
        return;
    }
    eciRegisterCallback(h, on_message, NULL);
    if (!eciSetOutputBuffer(h, FRAME, frame)) {
        printf("  %-32s no buffer\n", what);
        eciDelete(h);
        return;
    }
    if (codeset >= 0 && eciSetParam(h, 9, codeset) < 0)
        printf("  %-32s (codeset refused)\n", what);

    was = said;
    if (!eciAddText(h, (const char *)text)) {
        printf("  %-32s text refused\n", what);
        eciDelete(h);
        return;
    }
    if (!eciSynthesize(h)) {
        printf("  %-32s synthesize refused\n", what);
        eciDelete(h);
        return;
    }
    while (eciSpeaking(h) && spins++ < 2000)
        Sleep(10);
    eciDelete(h);
    printf("  %-32s %ld samples\n", what, said - was);
}

int main(void)
{
    unsigned langs[32];
    int i, n = 32;

    setvbuf(stdout, NULL, _IONBF, 0);
    evvRunStaticInitialisers();

    if (eciGetAvailableLanguages(langs, &n) == 0)
        for (i = 0; i < n && i < 32; i++)
            printf("  offered language 0x%x\n", langs[i]);

    one("romaji, default codeset", 0, -1, roma);
    one("shift-jis, default codeset", 0, -1, sjis);
    one("shift-jis, codeset 0", 0, 0, sjis);
    one("shift-jis, language 0x80000", 0x80000, -1, sjis);
    one("ucs-2, language with 0x800", 0x80800, -1, ucs2);
    one("ucs-2, codeset 0x800", 0, 0x800, ucs2);

    return 0;
}
