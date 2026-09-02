/* Ask IBM's own SSML reader what a document turns into.
 *
 * The SSML filter is compiled into every one of the nine language object
 * sets and nothing in those sets ever registers it: IBM shipped it as a DLL
 * of its own, so the object is there and unreferenced, and the two things
 * that would have found it -- autoLoadFilter and getINIValue -- are stubs in
 * this build. That is why the whole SSML block reported itself absent for as
 * long as the port has existed.
 *
 * But the entry point is in the binary. `ssmlFilterGetObject' is what a DLL
 * would have exported, and `eciRegisterFilter' takes exactly that, so a
 * program linked beside these objects can hand IBM its own filter and turn
 * it on. Then `eciGetFilteredText' hands back the annotations the reader
 * made, as text.
 *
 * That is a sharper oracle than the audio. What comes back is a string, so a
 * difference between IBM's reader and ours names itself -- a wrong
 * annotation, a missing space, a rate computed one way rather than another
 * -- instead of showing up as a different hash over forty thousand samples.
 * This is what the SSML transcription was written against.
 *
 * It reads one document per line from a file, or from its own list when
 * given none, and prints the filtered text for each. `<' and `>' would be
 * awkward on a command line, so a line of the file is taken as it stands.
 *
 * usage: ssmltry.exe [cases.txt]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

typedef void *ECIHand;
typedef void *ECIFilterHand;

typedef struct ECIFilterAttrib {
    char eciFilterName[80];
    long language;
} ECIFilterAttrib;

/* What a filter's own entry point looks like: it is asked for an interface
   by number and hands back a pointer. Seven is the filter object itself and
   eight is the function that says whether a piece of text is for it. */
typedef int (__stdcall *PGETFILTEROBJECT)(unsigned long which, void **out);

extern int __stdcall eciRegisterFilter(ECIHand h, unsigned int number,
                                       PGETFILTEROBJECT *entry,
                                       ECIFilterAttrib *attrib, int autoload);
extern ECIFilterHand __stdcall eciNewFilter(ECIHand h, unsigned int number,
                                            int global);
extern int __stdcall eciActivateFilter(ECIHand h, ECIFilterHand filter);
extern int __stdcall eciGetFilteredText(ECIHand h, ECIFilterHand filter,
                                        const void *in, const void **out);
extern ECIHand __stdcall eciNew(void);
extern ECIHand __stdcall eciNewEx(long language);
extern ECIHand __stdcall eciDelete(ECIHand h);
extern int __stdcall eciSetParam(ECIHand h, int which, int value);

/* IBM's own SSML filter, which nothing in its objects reaches.
 *
 * Its entry point is cdecl where the pointer registerFilter takes is
 * stdcall, so the two disagree about who cleans up the two arguments. The
 * eight bytes leak until registerFilter returns and its `leave' takes them
 * back, and nothing else in that function touches the stack pointer, so it
 * works by luck. That is IBM's own, and this cast is what says so. */
extern int ssmlFilterGetObject(unsigned long which, void **out);

/* The reader's own state, reached through the filter object, so that this
   can say what the manager did and did not set on it. The filter is
   twenty-four bytes with the state pointer twelve in, and the state carries
   the environment at 0x58 and the flag that says its three values were set
   by hand at 0x29. */
#define FILTER_STATE(f)  (*(void **)((char *)(f) + 0x0c))
#define STATE_ENV(s)     (*(void **)((char *)(s) + 0x58))

extern void __stdcall SetSSMLStateValues(void *state, int voice, int lang,
                                         int volume)
    __asm__("\"?SetSSMLStateValues@@YAXPAXHW4ECILanguageDialect@@H@Z\"");

void evvRunStaticInitialisers(void);

static const char *BUILTIN[] = {
    "<speak>Hello there.</speak>",
    "<speak><prosody rate=\"slow\">Slowly now.</prosody></speak>",
    "<speak><prosody rate=\"+25%\">Faster.</prosody></speak>",
    "<speak><prosody pitch=\"high\">High.</prosody></speak>",
    "<speak><prosody pitch=\"+2st\">Up two.</prosody></speak>",
    "<speak><prosody volume=\"loud\">Loud.</prosody></speak>",
    "<speak><prosody range=\"x-high\">Wide.</prosody></speak>",
    "<speak><emphasis level=\"strong\">This</emphasis> one.</speak>",
    "<speak><voice gender=\"female\">She said.</voice></speak>",
    "<speak><voice name=\"Grandma\">Once upon a time.</voice></speak>",
    "<speak><voice age=\"7\">A child.</voice></speak>",
    "<speak><say-as interpret-as=\"cardinal\">123</say-as></speak>",
    "<speak><say-as interpret-as=\"ordinal\">3</say-as></speak>",
    "<speak><say-as interpret-as=\"characters\">abc</say-as></speak>",
    "<speak><say-as interpret-as=\"digits\">123</say-as></speak>",
    "<speak><say-as interpret-as=\"telephone\">5551234</say-as></speak>",
    "<speak><say-as interpret-as=\"date\" format=\"mdy\">3/9/2026</say-as>"
        "</speak>",
    "<speak><say-as interpret-as=\"time\">14:30</say-as></speak>",
    "<speak><say-as interpret-as=\"currency\">USD1.50</say-as></speak>",
    "<speak><phoneme alphabet=\"ipa\" ph=\"h\xc9\x99\xcb\x88lo\xca\x8a\">"
        "hello</phoneme></speak>",
    "<speak><break time=\"250ms\"/>after a pause</speak>",
    "<speak><break strength=\"strong\"/>after a break</speak>",
    "<speak><mark name=\"here\"/>at the mark</speak>",
    "<speak><sub alias=\"World Wide Web\">WWW</sub></speak>",
    "<speak>a &amp; b &lt; c &gt; d &quot;e&quot; &apos;f&apos;</speak>",
    "<speak>&#65;&#x42;</speak>",
    "<speak xml:lang=\"en-GB\">In British.</speak>",
    "<speak><p>One paragraph.</p><s>One sentence.</s></speak>",
    "<speak><audio src=\"beep.wav\">fallback</audio></speak>",
    "<?xml version=\"1.0\"?><speak>With a declaration.</speak>",
    "<speak>unclosed",
    "<speak><prosody rate=\"nonsense\">Bad value.</prosody></speak>",
    "<speak><nosuchtag>Unknown element.</nosuchtag></speak>",
    "not markup at all"
};

static void one(ECIHand h, ECIFilterHand filter, const char *text)
{
    const char *out = NULL;
    int rc;

    rc = eciGetFilteredText(h, filter, text, (const void **)&out);
    printf("in  [%s]\n", text);
    printf("out %d [%s]\n", rc, out ? out : "(none)");
}

int main(int argc, char **argv)
{
    ECIFilterAttrib attrib;
    PGETFILTEROBJECT entry = (PGETFILTEROBJECT)ssmlFilterGetObject;
    ECIFilterHand filter;
    ECIHand h;
    int rc;

    evvRunStaticInitialisers();

    h = eciNew();
    if (h == NULL)
        h = eciNewEx(0x10000);
    if (h == NULL) {
        printf("ssmltry: no instance\n");
        return 1;
    }

    memset(&attrib, 0, sizeof attrib);
    strcpy(attrib.eciFilterName, "IBM SSML Filter");
    attrib.language = 0;

    rc = eciRegisterFilter(h, 0, &entry, &attrib, 1);
    printf("register %d name [%s] language %ld\n", rc, attrib.eciFilterName,
           attrib.language);

    filter = eciNewFilter(h, 0, 1);
    printf("newFilter %s\n", filter ? "made" : "refused");
    if (filter == NULL) {
        eciDelete(h);
        return 1;
    }

    rc = eciActivateFilter(h, filter);
    printf("activate %d\n", rc);
    printf("state %p env %p\n", FILTER_STATE(filter),
           STATE_ENV(FILTER_STATE(filter)));

    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb");
        char line[4096];

        if (f == NULL) {
            printf("ssmltry: cannot read %s\n", argv[1]);
            eciDelete(h);
            return 1;
        }
        while (fgets(line, sizeof line, f) != NULL) {
            size_t n = strlen(line);

            while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
                line[--n] = 0;
            if (n == 0 || line[0] == '#')
                continue;
            one(h, filter, line);
        }
        fclose(f);
    } else {
        size_t i;

        for (i = 0; i < sizeof BUILTIN / sizeof BUILTIN[0]; i++)
            one(h, filter, BUILTIN[i]);
    }

    eciDelete(h);
    return 0;
}
