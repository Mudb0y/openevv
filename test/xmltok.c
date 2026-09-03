/* The XML scanner, ours against IBM's, one document at a time.
 *
 * The scanner is flex's automaton with IBM's forty-eight actions on top,
 * and what has to be right about it is the token stream: which runs of
 * text come out as character data, where a tag begins and ends, what its
 * attributes are called and what they are worth, and which documents are
 * refused. None of that is visible in the audio, and only some of it is
 * visible in the annotations the SSML reader makes -- a document the
 * scanner tokenises differently may still come out sounding the same.
 *
 * So this drives the parser directly and prints every call the three
 * handlers get. The same file is compiled twice: once against our engine
 * and once against IBM's objects, where the parser's methods are mangled
 * C++ names and are reached by name rather than through its table.
 * `test/xmltok.sh' diffs the two.
 *
 * Every document is copied into a buffer of its own before being parsed,
 * and that is not tidiness. IBM's narrowing pass terminates the digits of a
 * numeric character reference by writing a nought over the semicolon in the
 * text it was given, so a document handed over as a string literal faults
 * on `&#65;'. src/eci/ssml/eci_mbconvert.c does not, and says why; passing a copy is
 * what lets the two be compared past that point.
 *
 * The documents are chosen to reach the corners the automaton was taken
 * whole for: entities in text and inside quoted attribute values, an
 * attribute with no value, `/>' which is handled by pushing the two
 * characters back into the input, a close tag whose name does not match,
 * text long enough to make the text buffer grow twice, more attributes
 * than a tag has room for, and elements nested deeper than the tag stack
 * starts out.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "evv_abi.h"

#if defined(EVV_XMLTOK_OURS)
#define XT(name) /* our own name */
#else
#define XT(name) MANGLED(name)
#endif

typedef void (*OpenFn)(void *data, const char *name, const char **atts);
typedef void (*CloseFn)(void *data, const char *name);
typedef void (*TextFn)(void *data, char *text, int32_t len);

extern void *CreateParserXML(void) XT("_CreateParserXML");
extern void  DeleteParserXML(void *p) XT("_DeleteParserXML");
extern THIS int8_t px_parseText(void *p, const char *text, int32_t len,
                                int32_t unused, int8_t convert)
    XT("?ParseText@Parserxml@@UAE_NPBDHH_N@Z");
extern THIS void px_setTagHandler(void *p, OpenFn open, CloseFn close)
    XT("?SetTagHandler@Parserxml@@UAEXP6AXPAXPBDPAPBD@ZP6AX01@Z@Z");
extern THIS void px_setTextHandler(void *p, TextFn text)
    XT("?SetTextHandler@Parserxml@@UAEXP6AXPAXPADH@Z@Z");
extern THIS void px_setData(void *p, void *data)
    XT("?SetData@Parserxml@@UAEXPAX@Z");

/* Every handler call is one line, so a difference names itself. */
static void onOpen(void *data, const char *name, const char **atts)
{
    int i = 0;

    printf("  open [%s]", name ? name : "(none)");
    if (atts != NULL) {
        while (atts[i] != NULL) {
            printf(" [%s]", atts[i]);
            i++;
        }
    }
    printf(" (%d)\n", i);
    (void)data;
}

static void onClose(void *data, const char *name)
{
    printf("  close [%s]\n", name ? name : "(none)");
    (void)data;
}

static void onText(void *data, char *text, int32_t len)
{
    printf("  text %d [%s]\n", (int)len, text ? text : "(none)");
    (void)data;
}

/* A document long enough to make the text buffer grow. Built rather than
   written out, because it has to be over a thousand characters. */
static char *longDocument(int32_t runs)
{
    static char big[8192];
    int32_t i;

    strcpy(big, "<speak>");
    for (i = 0; i < runs; i++)
        strcat(big, "abcdefghij");
    strcat(big, "</speak>");
    return big;
}

/* A tag with more attributes than the list starts with room for. */
static char *manyAttributes(int32_t n)
{
    static char big[4096];
    int32_t i;

    strcpy(big, "<speak");
    for (i = 0; i < n; i++) {
        char one[32];

        sprintf(one, " a%d=\"v%d\"", (int)i, (int)i);
        strcat(big, one);
    }
    strcat(big, ">x</speak>");
    return big;
}

/* Elements nested deeper than the tag stack starts out. */
static char *deepNesting(int32_t depth)
{
    static char big[8192];
    int32_t i;

    big[0] = 0;
    for (i = 0; i < depth; i++)
        strcat(big, "<p>");
    strcat(big, "x");
    for (i = 0; i < depth; i++)
        strcat(big, "</p>");
    return big;
}

/* A copy of one, because the narrowing pass writes into what it is given. */
static char *mutable(const char *text)
{
    static char buf[8192];

    strcpy(buf, text);
    return buf;
}

static const char *CASES[] = {
    "<speak>Hello there.</speak>",
    "<speak/>",
    "<speak></speak>",
    "<speak >spaced</speak >",
    "<SPEAK>upper</SPEAK>",
    "<speak version=\"1.0\">attributed</speak>",
    "<speak version='1.0'>single quoted</speak>",
    "<speak a=\"1\" b=\"2\" c=\"3\">three</speak>",
    "<speak novalue>bare attribute</speak>",
    "<speak a=\"\">empty value</speak>",
    "<speak>a &amp; b</speak>",
    "<speak>a &lt; b &gt; c</speak>",
    "<speak>&quot;quoted&quot; and &apos;single&apos;</speak>",
    "<speak a=\"&amp;\">entity in a value</speak>",
    "<speak a=\"&lt;&gt;\">two in a value</speak>",
    "<speak a=\"say &quot;hi&quot;\">quote in a value</speak>",
    "<speak>&#65;&#66;</speak>",
    "<speak>&#x41;&#x42;</speak>",
    "<speak>&unknown;</speak>",
    "<speak>bare & ampersand</speak>",
    "<speak><p>one</p><p>two</p></speak>",
    "<speak><break/>after</speak>",
    "<speak><break time=\"250ms\"/>after</speak>",
    "<speak><p>mismatched</q></speak>",
    "<speak>unclosed",
    "unopened</speak>",
    "<speak>trailing</speak>after",
    "before<speak>leading</speak>",
    "<?xml version=\"1.0\"?><speak>declared</speak>",
    "<!-- a comment --><speak>commented</speak>",
    "<speak><!-- inside --></speak>",
    "<speak><![CDATA[raw < text]]></speak>",
    "<speak>tab\there</speak>",
    "<speak>newline\nhere</speak>",
    "<speak>Polish \xc4\x85 and \xc5\xbc.</speak>",
    "<speak>\xe4\xb8\xad\xe6\x96\x87</speak>",
    "not markup at all",
    "",
    "<",
    ">",
    "<>",
    "</>",
    "<speak a=b>unquoted value</speak>",
    "<speak\n  a=\"1\"\n  b=\"2\">newlines in the tag</speak>",
    "<speak xml:lang=\"en-US\">a colon in a name</speak>",
    "<speak say-as=\"x\">a dash in a name</speak>"
};

int main(void)
{
    void  *p;
    size_t i;

    p = CreateParserXML();
    if (p == NULL) {
        printf("xmltok: no parser\n");
        return 1;
    }

    px_setTagHandler(p, onOpen, onClose);
    px_setTextHandler(p, onText);
    px_setData(p, NULL);

    for (i = 0; i < sizeof CASES / sizeof CASES[0]; i++) {
        int8_t ok;

        printf("in [%s]\n", CASES[i]);
        ok = px_parseText(p, mutable(CASES[i]), (int32_t)strlen(CASES[i]), 0, 1);
        printf("ok %d\n", (int)ok);
    }

    /* And the same list again with entity conversion off, which is the
       other half of twenty of the forty-eight rules. */
    for (i = 0; i < sizeof CASES / sizeof CASES[0]; i++) {
        int8_t ok;

        printf("raw [%s]\n", CASES[i]);
        ok = px_parseText(p, mutable(CASES[i]), (int32_t)strlen(CASES[i]), 0, 0);
        printf("ok %d\n", (int)ok);
    }

    /* The four buffers, each made to grow. */
    {
        char *doc = longDocument(300);
        int8_t ok;

        printf("in [long %d]\n", (int)strlen(doc));
        ok = px_parseText(p, doc, (int32_t)strlen(doc), 0, 1);
        printf("ok %d\n", (int)ok);
    }
    {
        char *doc = manyAttributes(40);
        int8_t ok;

        printf("in [atts %d]\n", (int)strlen(doc));
        ok = px_parseText(p, doc, (int32_t)strlen(doc), 0, 1);
        printf("ok %d\n", (int)ok);
    }
    {
        char *doc = deepNesting(150);
        int8_t ok;

        printf("in [deep %d]\n", (int)strlen(doc));
        ok = px_parseText(p, doc, (int32_t)strlen(doc), 0, 1);
        printf("ok %d\n", (int)ok);
    }

    /* And once more from the top, to show that a parser that has grown its
       buffers still reads a short document the same way. */
    for (i = 0; i < sizeof CASES / sizeof CASES[0]; i++) {
        int8_t ok;

        printf("again [%s]\n", CASES[i]);
        ok = px_parseText(p, mutable(CASES[i]), (int32_t)strlen(CASES[i]), 0, 1);
        printf("ok %d\n", (int)ok);
    }

    DeleteParserXML(p);
    return 0;
}
