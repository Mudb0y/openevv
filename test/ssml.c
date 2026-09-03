/* The SSML reader, ours against IBM's.
 *
 * One file compiled twice: by the top-level `make ssml' against our engine,
 * and by `make -C reference ssml' against IBM's objects. Both print the
 * annotations a document turns into, one line in and one line out, and
 * test/ssml.sh diffs them.
 *
 * What makes this possible is that IBM's SSML filter is in every one of its
 * language object sets and nothing in them registers it -- it shipped as a
 * DLL of its own, and the two things that would have found it are stubs. So
 * a program linked beside those objects can hand the engine its own filter
 * through `eciRegisterFilter' and then ask what it made of a string. The
 * answer is text, so a difference names itself: a wrong annotation, a
 * missing space, a rate worked out one way rather than another, instead of
 * a different hash over forty thousand samples.
 *
 * The cases are in here rather than in a file so that the two sides cannot
 * drift apart; a file may be named on the command line instead, one
 * document a line.
 *
 * usage: ssml [cases.txt]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "evv_abi.h"

#if defined(EVV_SSML_OURS)
#define ECI(name) /* our own name */
#define ENTRY STDCALL
#define ENTRY_NAME(name) /* ours is stdcall, as the type says */
#else
#define ECI(name) MANGLED(name)
#define ENTRY
/* IBM's entry point is compiled cdecl where the pointer it is registered as
   is stdcall, so the two disagree about who takes the arguments back off
   the stack. The eight bytes leak until `registerFilter' returns and its
   own frame pointer recovers them, and nothing in between touches the
   stack pointer, which is why the original gets away with it. */
#define ENTRY_NAME(name) MANGLED(name)
#endif

typedef struct ECIFilterAttrib {
    char    eciFilterName[80];
    int32_t language;
} ECIFilterAttrib;

typedef int STDCALL (*GetFilterObjectFn)(uint32_t which, void **out);

extern void *STDCALL eo_new(void) ECI("_eciNew@0");
extern void *STDCALL eo_newEx(int32_t language) ECI("_eciNewEx@4");
extern int32_t STDCALL es_delete(void *h) ECI("_eciDelete@4");
extern int32_t STDCALL es_registerFilter(void *h, uint32_t id, void *entry,
                                         void *attrib, int32_t autoload)
    ECI("_eciRegisterFilter@20");
extern int32_t STDCALL es_unregisterFilter(void *h, uint32_t id, void *attrib)
    ECI("_eciUnregisterFilter@12");
extern void *STDCALL es_newFilter(void *h, int32_t id, int32_t global)
    ECI("_eciNewFilter@12");
extern int32_t STDCALL es_activateFilter(void *h, void *filter)
    ECI("_eciActivateFilter@8");
extern int32_t STDCALL es_deactivateFilter(void *h, void *filter)
    ECI("_eciDeactivateFilter@8");
extern int32_t STDCALL es_deleteFilter(void *h, void *filter)
    ECI("_eciDeleteFilter@8");
extern int32_t STDCALL es_getFilteredText(void *h, void *filter,
                                          const void *in, const void **out)
    ECI("_eciGetFilteredText@16");

/* Ours is `ssml_getFilterObject' -- see src/eci_filter.h -- and IBM's is
   the published name, under which its object defines it. */
#if defined(EVV_SSML_OURS)
extern int ENTRY ssml_getFilterObject(uint32_t which, void **out);
#define GET_FILTER_OBJECT ssml_getFilterObject
#else
extern int ENTRY ssmlFilterGetObject(uint32_t which, void **out)
    ENTRY_NAME("_ssmlFilterGetObject");
#define GET_FILTER_OBJECT ssmlFilterGetObject
#endif

void evvRunStaticInitialisers(void);
void evv_port_start(void);

/* US English, which is the language every one of these is read in. */
#define LANGUAGE_ENUS 0x10000

/* The number the filter is registered under. Any of twenty would do. */
#define FILTER_ID 0

/* Registering it against no language at all, so that it applies whatever
   the engine is set to. */
#define FILTER_GLOBAL 1

static const char *CASES[] = {
    /* the document itself */
    "<speak version=\"1.0\" xml:lang=\"en-US\">Hello there.</speak>",
    "<?xml version=\"1.0\"?><speak version=\"1.0\" xml:lang=\"en-US\">"
        "With a declaration.</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"></speak>",
    "<speak version=\"1.0\">No language given.</speak>",
    "<speak xml:lang=\"en-US\">No version given.</speak>",
    "<speak version=\"2.0\" xml:lang=\"en-US\">Wrong version.</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-GB\">Another dialect.</speak>",
    "<speak version=\"1.0\" xml:lang=\"de-DE\">Another language.</speak>",
    "<speak version=\"1.0\" xml:lang=\"zz-ZZ\">No such language.</speak>",
    "<speak version=\"1.0\" xml:lang=\"e\">Too short.</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US-x\">Too long.</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">Unclosed.",
    "not markup at all",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><nosuchtag>Unknown."
        "</nosuchtag></speak>",

    /* rate, three ways of saying it and the ones that are refused */
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody rate=\"x-slow\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody rate=\"slow\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody rate=\"medium\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody rate=\"fast\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody rate=\"x-fast\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody rate=\"default\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody rate=\"200\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody rate=\"70\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody rate=\"1297\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody rate=\"69\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody rate=\"1298\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody rate=\"+25%\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody rate=\"-25%\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody rate=\"nonsense\">"
        "One.</prosody></speak>",

    /* pitch */
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody pitch=\"x-low\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody pitch=\"low\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody pitch=\"medium\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody pitch=\"high\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody pitch=\"x-high\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody pitch=\"250Hz\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody pitch=\"40Hz\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody pitch=\"422Hz\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody pitch=\"39Hz\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody pitch=\"+2st\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody pitch=\"-2st\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody pitch=\"+10%\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody pitch=\"-10%\">"
        "One.</prosody></speak>",

    /* range */
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody range=\"x-low\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody range=\"high\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody range=\"x-high\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody range=\"100Hz\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody range=\"+3st\">"
        "One.</prosody></speak>",

    /* volume, which is the one worked out against what the engine is set to */
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody volume=\"silent\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody volume=\"x-soft\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody volume=\"soft\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody volume=\"medium\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody volume=\"loud\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody volume=\"x-loud\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody volume=\"default\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody volume=\"50\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody volume=\"0\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody volume=\"100\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody volume=\"101\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody volume=\"+10%\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody volume=\"-10%\">"
        "One.</prosody></speak>",

    /* the two the engine has nothing to say with, and an empty one */
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody duration=\"2s\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody contour=\"(0%,+20)\">"
        "One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody>"
        "One.</prosody></speak>",

    /* several at once, and nested, which is where putting a setting back
       has to write out every enclosing one again */
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody rate=\"slow\" "
        "pitch=\"high\" volume=\"loud\">One.</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody rate=\"slow\">a"
        "<prosody rate=\"fast\">b</prosody>c</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody pitch=\"high\">a"
        "<prosody pitch=\"low\">b</prosody>c</prosody></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody volume=\"50\">a"
        "<prosody volume=\"80\">b</prosody>c</prosody></speak>",

    /* emphasis */
    "<speak version=\"1.0\" xml:lang=\"en-US\"><emphasis level=\"none\">"
        "One</emphasis> two.</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><emphasis level=\"reduced\">"
        "One</emphasis> two.</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><emphasis level=\"moderate\">"
        "One</emphasis> two.</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><emphasis level=\"strong\">"
        "One</emphasis> two.</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><emphasis>"
        "One</emphasis> two.</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><emphasis level=\"nonsense\">"
        "One</emphasis> two.</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><emphasis level=\"strong\">a"
        "<emphasis level=\"strong\">b</emphasis>c</emphasis></speak>",

    /* the voice, by every combination of the four attributes */
    "<speak version=\"1.0\" xml:lang=\"en-US\"><voice gender=\"male\">"
        "One.</voice></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><voice gender=\"female\">"
        "One.</voice></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><voice gender=\"neutral\">"
        "One.</voice></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><voice gender=\"nonsense\">"
        "One.</voice></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><voice age=\"7\">"
        "One.</voice></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><voice age=\"30\">"
        "One.</voice></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><voice age=\"70\">"
        "One.</voice></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><voice age=\"14\">"
        "One.</voice></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><voice age=\"15\">"
        "One.</voice></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><voice age=\"59\">"
        "One.</voice></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><voice age=\"60\">"
        "One.</voice></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><voice variant=\"1\">"
        "One.</voice></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><voice variant=\"2\">"
        "One.</voice></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><voice variant=\"3\">"
        "One.</voice></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<voice gender=\"female\" age=\"30\">One.</voice></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<voice gender=\"male\" age=\"7\">One.</voice></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<voice gender=\"female\" variant=\"1\">One.</voice></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<voice age=\"30\" variant=\"1\">One.</voice></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<voice gender=\"female\" age=\"30\" variant=\"1\">"
        "One.</voice></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><voice name=\"Grandma\">"
        "One.</voice></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><voice name=\"No Such Name\">"
        "One.</voice></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><voice>"
        "One.</voice></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><voice xml:lang=\"en-US\">"
        "One.</voice></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><voice xml:lang=\"de-DE\">"
        "One.</voice></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><voice gender=\"female\">a"
        "<voice gender=\"male\">b</voice>c</voice></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prosody rate=\"slow\">a"
        "<voice gender=\"female\">b</voice>c</prosody></speak>",

    /* say-as: every kind, and the ones that are not a kind */
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<say-as interpret-as=\"cardinal\">123</say-as></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<say-as interpret-as=\"ordinal\">3</say-as></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<say-as interpret-as=\"number\">123</say-as></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<say-as interpret-as=\"number\" format=\"ordinal\">3"
        "</say-as></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<say-as interpret-as=\"number\" format=\"cardinal\">123"
        "</say-as></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<say-as interpret-as=\"number\" format=\"telephone\">5551234"
        "</say-as></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<say-as interpret-as=\"number\" format=\"telephone\" "
        "detail=\"punctuation\">555-1234</say-as></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<say-as interpret-as=\"letters\">abc</say-as></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<say-as interpret-as=\"digits\">123</say-as></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<say-as interpret-as=\"vxml:digits\">123</say-as></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<say-as interpret-as=\"letters\">a A - and &amp; z</say-as></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<say-as interpret-as=\"date\" format=\"mdy\">3/9/2026"
        "</say-as></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<say-as interpret-as=\"date\" format=\"dmy\">9/3/2026"
        "</say-as></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<say-as interpret-as=\"date\" format=\"ymd\">2026/3/9"
        "</say-as></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<say-as interpret-as=\"date\">3/9/2026</say-as></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<say-as interpret-as=\"vxml:date\">20260309</say-as></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<say-as interpret-as=\"vxml:date\">????0309</say-as></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<say-as interpret-as=\"vxml:date\">2026????</say-as></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<say-as interpret-as=\"vxml:time\">1430</say-as></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<say-as interpret-as=\"vxml:currency\">USD1.50</say-as></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<say-as interpret-as=\"vxml:boolean\">true</say-as></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<say-as interpret-as=\"vxml:boolean\">FALSE</say-as></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<say-as interpret-as=\"vxml:phone\">555-1234</say-as></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<say-as interpret-as=\"vxml:number\">123</say-as></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<say-as interpret-as=\"nonsense\">123</say-as></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<say-as>123</say-as></speak>",
    /* Roman numerals, which is what the number reader does to its text */
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<say-as interpret-as=\"cardinal\">XIV</say-as></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<say-as interpret-as=\"cardinal\">MCMXCIX</say-as></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<say-as interpret-as=\"ordinal\">IV</say-as></speak>",

    /* substitution, and the phonemes */
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<sub alias=\"World Wide Web\">WWW</sub></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><sub>WWW</sub></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<phoneme ph=\"h.E.l.oU\">hello</phoneme></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<phoneme alphabet=\"ibm\" ph=\"h.E.l.oU\">hello</phoneme></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<phoneme alphabet=\"ipa\" ph=\"h\xc9\x99\xcb\x88lo\xca\x8a\">"
        "hello</phoneme></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<phoneme alphabet=\"ipa\" ph=\"t\xc3\xa6st\">test</phoneme></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<phoneme alphabet=\"ipa\" ph=\"h\xc9\x99 lo\xca\x8a\">"
        "two words</phoneme></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<phoneme alphabet=\"nonsense\" ph=\"x\">y</phoneme></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<phoneme alphabet=\"ipa\">no ph</phoneme></speak>",

    /* pauses, marks and audio */
    "<speak version=\"1.0\" xml:lang=\"en-US\">a<break/>b</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">a<break time=\"250ms\"/>"
        "b</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">a<break time=\"2s\"/>"
        "b</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">a<break time=\"0ms\"/>"
        "b</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">a<break time=\"300000ms\"/>"
        "b</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">a<break time=\"300001ms\"/>"
        "b</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">a<break strength=\"none\"/>"
        "b</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">a<break strength=\"x-weak\"/>"
        "b</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">a<break strength=\"weak\"/>"
        "b</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">a<break strength=\"medium\"/>"
        "b</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">a<break strength=\"strong\"/>"
        "b</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "a<break strength=\"x-strong\"/>b</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">a<mark name=\"here\"/>"
        "b</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">a<mark/>b</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<audio src=\"beep.wav\">fallback</audio></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<audio src=\"\">fallback</audio></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<audio>no source</audio></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<audio src=\"a.wav\"><desc>a beep</desc>fallback</audio></speak>",

    /* structure, which is where a stop is put in if there was not one */
    "<speak version=\"1.0\" xml:lang=\"en-US\"><p>One.</p><p>Two.</p>"
        "</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><p>One</p><p>Two</p></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><s>One</s><s>Two</s></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><paragraph>One</paragraph>"
        "</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><sentence>One</sentence>"
        "</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><p xml:lang=\"de-DE\">Eins"
        "</p></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><p></p></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><metadata>hidden</metadata>"
        "shown</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><prompt>One.</prompt>"
        "</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<prompt xml:lang=\"de-DE\">Eins.</prompt></speak>",

    /* the text itself: entities, references and white space */
    "<speak version=\"1.0\" xml:lang=\"en-US\">a &amp; b &lt; c &gt; d "
        "&quot;e&quot; &apos;f&apos;</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">&#65;&#x42;</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">&#65;</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">  spaced  out  </speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">two\tlines</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">an accent: caf\xc3\xa9"
        "</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">"
        "<say-as interpret-as=\"letters\">caf\xc3\xa9</say-as></speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\"><!-- a comment -->text"
        "</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">a<![CDATA[<b>]]>c</speak>",
    "<speak version=\"1.0\" xml:lang=\"en-US\">already `vs50 annotated"
        "</speak>",

    /* and everything at once */
    "<speak version=\"1.0\" xml:lang=\"en-US\"><p>"
        "<prosody rate=\"slow\" pitch=\"high\">"
        "<emphasis level=\"strong\">Now</emphasis> then, "
        "<say-as interpret-as=\"cardinal\">42</say-as> of them"
        "<break time=\"500ms\"/> at <say-as interpret-as=\"vxml:time\">1430"
        "</say-as>.</prosody></p><p><voice gender=\"female\">"
        "And she said <sub alias=\"nothing at all\">nowt</sub>."
        "</voice></p></speak>"
};

/* The document is copied into writable memory before it is handed over,
   which is not tidiness: IBM's reader terminates the digits of a numeric
   character reference by writing a NUL over the semicolon that ends them,
   in the caller's own string. Handed a literal it faults there. Ours
   copies the digits out instead -- see src/eci_mbconvert.c -- and does not
   care either way, so the copy is what lets both sides read the same
   document. */
static void one(void *h, void *filter, const char *text)
{
    const void *out = NULL;
    char *writable = malloc(strlen(text) + 1);
    int32_t rc;

    if (writable == NULL) {
        printf("ssml: out of room\n");
        return;
    }
    strcpy(writable, text);

    printf("in  [%s]\n", text);
    rc = es_getFilteredText(h, filter, writable, &out);
    printf("out %d [%s]\n", (int)rc, out ? (const char *)out : "(none)");
    free(writable);
}

int main(int argc, char **argv)
{
    ECIFilterAttrib attrib;
    GetFilterObjectFn entry = (GetFilterObjectFn)GET_FILTER_OBJECT;
    void *filter;
    void *h;
    int32_t rc;

    evv_port_start();
    evvRunStaticInitialisers();

    h = eo_new();
    if (h == NULL)
        h = eo_newEx(LANGUAGE_ENUS);
    if (h == NULL) {
        printf("ssml: no instance\n");
        return 1;
    }

    memset(&attrib, 0, sizeof attrib);
    rc = es_registerFilter(h, FILTER_ID, &entry, &attrib, 1);
    printf("register %d name [%s] language %d\n", (int)rc,
           attrib.eciFilterName, (int)attrib.language);

    filter = es_newFilter(h, FILTER_ID, FILTER_GLOBAL);
    printf("newFilter %s\n", filter ? "made" : "refused");
    if (filter == NULL) {
        es_delete(h);
        return 1;
    }

    rc = es_activateFilter(h, filter);
    printf("activate %d\n", (int)rc);

    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb");
        char line[8192];

        if (f == NULL) {
            printf("ssml: cannot read %s\n", argv[1]);
            es_delete(h);
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

        for (i = 0; i < sizeof CASES / sizeof CASES[0]; i++)
            one(h, filter, CASES[i]);
    }

    /* Taking it all down again, which is the other half of what the
       manager does and is worth exercising rather than leaving to the
       instance's own teardown. */
    rc = es_deactivateFilter(h, filter);
    printf("deactivate %d\n", (int)rc);
    rc = es_deleteFilter(h, filter);
    printf("deleteFilter %d\n", (int)rc);
    rc = es_unregisterFilter(h, FILTER_ID, &attrib);
    printf("unregister %d\n", (int)rc);

    es_delete(h);
    return 0;
}
