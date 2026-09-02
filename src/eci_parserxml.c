/* The shell around the scanner: buffers, handlers, and one document.
 *
 * The scanner in src/eci_xmltok.c is flex's, and flex keeps its state in
 * file-scope variables. So something has to own the buffers those variables
 * point at, hand them over before each document and take back what the
 * scanner grew, and turn the tokens it answers with into calls on the three
 * handlers a caller registered. That is this.
 *
 * A document arrives as UTF-8 and is narrowed first, because the scanner
 * reads one byte at a time; see src/eci_mbconvert.c for what narrowing is.
 * Character data is widened again on the way out, so a handler sees the
 * bytes the document had.
 *
 * Four buffers and two arrays. The text buffer is where a run of character
 * data accumulates; two more are the scratch a run is copied into and
 * widened out of; the tag buffer holds the names of the elements that are
 * open, with an array of pointers into it as the stack; and one more array
 * holds the attributes of the tag being read. Any of the six may run out,
 * and when one does the scanner says which and is called again -- so the
 * text of one document may be scanned more than once and still comes out
 * once.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "evv_abi.h"
#include "eci_ssml.h"
#include "eci_parserxml.h"
#include "eci_xmltok.h"

extern void *cpp_new(uint32_t n) MANGLED("??2@YAPAXI@Z");
extern void  cpp_delete(void *p) MANGLED("??3@YAXPAX@Z");

/* What each buffer starts at. The tag stack is a hundred deep and a tag
   may carry ten attributes before either has to grow. */
#define TEXT_ROOM     0x400
#define ATTS_SLOTS    0xa
#define TAG_SLOTS     0x64

/* What the scanner answers. */
#define TOK_OPEN_TAG   1
#define TOK_CLOSE_TAG  2
#define TOK_CHAR_DATA  3
#define TOK_END        4
#define TOK_BAD_TAG    5
#define TOK_NO_MEMORY  6
#define TOK_MISMATCH   7
#define TOK_GROW_TEXT  8
#define TOK_GROW_TAG   9
#define TOK_GROW_STACK 10
#define TOK_GROW_ATTS  11

/* Narrowing can turn one byte into five, and there is a nought after. */
#define NARROW_FACTOR  5

/* ---- handing the buffers to the scanner and back --------------------- */

/* Answers minus one when one of the four is missing, which is the only way
   this can fail. */
THIS int32_t px_setFlexVariable(Parserxml *p)
{
    TextBuffer = p->textBuffer;
    TagBuffer  = p->tagBuffer;
    TagStack   = p->tagStack;
    Atts       = p->atts;
    ConvertEscChars = p->convertEscChars;

    if (TextBuffer == 0 || TagBuffer == 0 || TagStack == 0 || Atts == 0)
        return -1;

    pTextBuffer    = TextBuffer;
    pTextBufferEnd = TextBuffer + p->textRoom;
    pTagBuffer     = TagBuffer;
    pTagBufferEnd  = TagBuffer + p->textRoom;
    TagCount = 0;
    MaxTags  = p->tagSlots;
    NumAtts  = 0;
    MaxAtts  = p->attsSlots;
    return 0;
}

/* ---- growing them ---------------------------------------------------- */

/* Twice the room, with the old contents copied over as far as the old room
   went rather than as far as the scanner had written -- which is the
   original's own arrangement, and is why the two scratch buffers beside it
   are thrown away and made again rather than copied. */
THIS int32_t px_reallocTextBuffer(Parserxml *p)
{
    int32_t want = p->textRoom * 2;
    char   *fresh = malloc((size_t)want + 1);

    free(p->sbcs);
    free(p->mbcs);
    p->sbcs = malloc((size_t)want + 1);
    p->mbcs = malloc((size_t)want + 1);

    if (fresh == 0 || p->sbcs == 0 || p->mbcs == 0)
        return 0;

    strncpy(fresh, p->textBuffer, (size_t)p->textRoom);
    free(p->textBuffer);
    p->textBuffer = fresh;

    TextBuffer     = p->textBuffer;
    pTextBuffer    = TextBuffer + p->textRoom;
    pTextBufferEnd = TextBuffer + want;
    p->textRoom = want;
    return 1;
}

/* The tag buffer grows by twice what has been written into it, which is
   not the same thing as twice its size; the original measures from the
   write position and this does too. */
THIS int32_t px_reallocTagBuffer(Parserxml *p)
{
    int32_t used = (int32_t)(pTagBuffer - TagBuffer);
    int32_t want = used * 2;
    char   *fresh = malloc((size_t)want + 1);

    if (fresh == 0)
        return 0;

    strncpy(fresh, TagBuffer, (size_t)used);
    pTagBuffer    = fresh + used;
    pTagBufferEnd = fresh + want;
    free(p->tagBuffer);
    TagBuffer = fresh;
    p->tagBuffer = TagBuffer;
    return 1;
}

THIS int32_t px_reallocTagStack(Parserxml *p)
{
    int32_t want = p->tagSlots * 2;
    char  **fresh = malloc((size_t)want * sizeof *fresh);
    int32_t i;

    if (fresh == 0)
        return 0;

    for (i = 0; i < TagCount; i++)
        fresh[i] = TagStack[i];

    p->tagSlots = want;
    MaxTags = want;
    free(p->tagStack);
    TagStack = fresh;
    p->tagStack = TagStack;
    return 1;
}

THIS int32_t px_reallocAtts(Parserxml *p)
{
    int32_t want = p->attsSlots * 2;
    char  **fresh = malloc((size_t)want * sizeof *fresh);
    int32_t i;

    if (fresh == 0)
        return 0;

    for (i = 0; i < NumAtts; i++)
        fresh[i] = Atts[i];

    p->attsSlots = want;
    MaxAtts = want;
    free(Atts);
    p->atts = fresh;
    Atts = fresh;
    return 1;
}

/* ---- the three handlers ---------------------------------------------- */

THIS void px_setTagHandler(Parserxml *p, OpenTagHandlerFn open,
                           CloseTagHandlerFn close)
{
    p->openTag = open;
    p->closeTag = close;
}

THIS void px_setTextHandler(Parserxml *p, CharDataHandlerFn text)
{
    p->charData = text;
}

THIS void px_setData(Parserxml *p, void *data)
{
    p->data = data;
}

/* ---- one document ---------------------------------------------------- */

/* Answers whether the whole of it was read. The third argument is not used
   and is the original's; the fourth says whether the five named entities
   become the characters they stand for. */
THIS int8_t px_parseText(Parserxml *p, const char *text, int32_t len,
                         int32_t unused, int8_t convert)
{
    int32_t done = 0;
    int32_t token = -1;
    int8_t  ok;
    char   *narrow;
    int32_t narrowed;

    (void)unused;

    narrow = malloc((size_t)len * NARROW_FACTOR + 1);
    if (narrow == 0)
        return 0;

    narrowed = Mbcs2Sbcs((char *)text, narrow);
    if (narrowed == 0) {
        free(narrow);
        return 0;
    }

    p->convertEscChars = convert;
    if (px_setFlexVariable(p) == -1) {
        free(narrow);
        return 0;
    }

    /* A document longer than the text buffer gets one the right size
       outright rather than doubling its way there. */
    if (narrowed > p->textRoom) {
        free(p->textBuffer);
        free(p->sbcs);
        free(p->mbcs);
        p->textBuffer = malloc((size_t)narrowed + 1);
        p->sbcs = malloc((size_t)narrowed + 1);
        p->mbcs = malloc((size_t)narrowed + 1);

        if (p->textBuffer == 0 || p->sbcs == 0 || p->mbcs == 0) {
            free(narrow);
            return 0;
        }

        TextBuffer     = p->textBuffer;
        pTextBuffer    = TextBuffer;
        pTextBufferEnd = TextBuffer + narrowed;
        p->textRoom = narrowed;
    }

    inBuf = narrow;
    inBufPos = 0;
    inBufLen = narrowed;

    ok = 1;
    yy_start = 1;

    while (!done) {
        /* The attributes and the text run start again for every token but
           the one that says the attribute list has to grow, since that one
           is about to be scanned over again. */
        if (token != TOK_GROW_ATTS) {
            NumAtts = 0;
            TextBuffer[0] = 0;
        }

        token = xmllex();

        switch (token) {
        case TOK_OPEN_TAG:
            if (p->openTag != 0)
                p->openTag(p->data, TagName, (const char **)Atts);
            break;

        case TOK_CLOSE_TAG:
            if (p->closeTag != 0)
                p->closeTag(p->data, TagName);
            break;

        case TOK_CHAR_DATA:
            if (p->charData != 0) {
                int32_t n = (int32_t)(pTextBuffer - TextBuffer);
                int32_t wide;

                strncpy(p->sbcs, TextBuffer, (size_t)n);
                p->sbcs[n] = 0;
                wide = Sbcs2Mbcs(p->sbcs, p->mbcs);
                if (wide == -1) {
                    ok = 0;
                    done = 1;
                    break;
                }
                p->charData(p->data, p->mbcs, wide);
            }
            pTextBuffer = TextBuffer;
            break;

        /* The end of the input. A document that still has elements open is
           not a document. */
        case TOK_END:
            if (TagCount != 0)
                ok = 0;
            done = 1;
            break;

        case TOK_BAD_TAG:
        case TOK_NO_MEMORY:
        case TOK_MISMATCH:
            ok = 0;
            done = 1;
            break;

        case TOK_GROW_TEXT:
            if (!px_reallocTextBuffer(p)) {
                ok = 0;
                done = 1;
            }
            break;

        case TOK_GROW_TAG:
            if (!px_reallocTagBuffer(p)) {
                ok = 0;
                done = 1;
            }
            break;

        case TOK_GROW_STACK:
            if (!px_reallocTagStack(p)) {
                ok = 0;
                done = 1;
            }
            break;

        case TOK_GROW_ATTS:
            if (!px_reallocAtts(p)) {
                ok = 0;
                done = 1;
            }
            break;

        default:
            ok = 0;
            done = 1;
            break;
        }
    }

    free(narrow);
    return ok;
}

/* ---- making one and dropping one ------------------------------------- */

THIS void px_ctor(Parserxml *p)
{
    p->openTag  = 0;
    p->closeTag = 0;
    p->charData = 0;
    p->data     = 0;
    p->textRoom = TEXT_ROOM;
    p->attsSlots = ATTS_SLOTS;
    p->tagSlots  = TAG_SLOTS;
    p->convertEscChars = 1;

    p->textBuffer = malloc((size_t)p->textRoom);
    p->sbcs       = malloc((size_t)p->textRoom);
    p->mbcs       = malloc((size_t)p->textRoom);
    p->tagBuffer  = malloc((size_t)p->textRoom);
    p->tagStack   = malloc((size_t)p->tagSlots * sizeof *p->tagStack);
    p->atts       = malloc((size_t)p->attsSlots * sizeof *p->atts);
}

THIS void px_dtor(Parserxml *p)
{
    XML_ParserDelete();

    if (p->textBuffer != 0) {
        free(p->textBuffer);
        p->textBuffer = 0;
    }
    if (p->sbcs != 0) {
        free(p->sbcs);
        p->sbcs = 0;
    }
    if (p->mbcs != 0) {
        free(p->mbcs);
        p->mbcs = 0;
    }
    if (p->tagBuffer != 0) {
        free(p->tagBuffer);
        p->tagBuffer = 0;
    }
    if (p->tagStack != 0) {
        free(p->tagStack);
        p->tagStack = 0;
    }
    if (p->atts != 0) {
        free(p->atts);
        p->atts = 0;
    }
}

Parserxml *CreateParserXML(void)
{
    Parserxml *p = cpp_new(sizeof *p);

    if (p == 0)
        return 0;
    px_ctor(p);
    return p;
}

void DeleteParserXML(Parserxml *p)
{
    if (p == 0)
        return;
    px_dtor(p);
    cpp_delete(p);
}

ALIAS("??0Parserxml@@QAE@XZ", "px_ctor");
ALIAS("??1Parserxml@@UAE@XZ", "px_dtor");
ALIAS("_CreateParserXML", "CreateParserXML");
ALIAS("_DeleteParserXML", "DeleteParserXML");
ALIAS("?ParseText@Parserxml@@UAE_NPBDHH_N@Z", "px_parseText");
ALIAS("?SetFlexVariable@Parserxml@@QAEHXZ", "px_setFlexVariable");
ALIAS("?ReallocTextBuffer@Parserxml@@QAEHXZ", "px_reallocTextBuffer");
ALIAS("?ReallocTagBuffer@Parserxml@@QAEHXZ", "px_reallocTagBuffer");
ALIAS("?ReallocTagStack@Parserxml@@QAEHXZ", "px_reallocTagStack");
ALIAS("?ReallocAtts@Parserxml@@QAEHXZ", "px_reallocAtts");
ALIAS("?SetTagHandler@Parserxml@@UAEXP6AXPAXPBDPAPBD@ZP6AX01@Z@Z",
      "px_setTagHandler");
ALIAS("?SetTextHandler@Parserxml@@UAEXP6AXPAXPADH@Z@Z", "px_setTextHandler");
ALIAS("?SetData@Parserxml@@UAEXPAX@Z", "px_setData");
