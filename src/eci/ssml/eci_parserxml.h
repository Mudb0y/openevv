/* The XML parser the SSML reader drives.
 *
 * One of these owns the scanner's buffers and turns its tokens into calls
 * on three handlers. src/eci/ssml/eci_parserxml.c is the whole of it; this is what
 * the reader needs to hold one and register with it.
 */

#ifndef ECI_PARSERXML_H
#define ECI_PARSERXML_H

#include <stdint.h>

typedef void (*OpenTagHandlerFn)(void *data, const char *name,
                                 const char **atts);
typedef void (*CloseTagHandlerFn)(void *data, const char *name);
typedef void (*CharDataHandlerFn)(void *data, char *text, int32_t len);

typedef struct {
    OpenTagHandlerFn  openTag;
    CloseTagHandlerFn closeTag;
    CharDataHandlerFn charData;
    void             *data;
    char             *textBuffer;   /* a run of character data          */
    char             *sbcs;         /* that run, narrowed               */
    char             *mbcs;         /* and widened again                */
    char             *tagBuffer;    /* the names of the elements open   */
    char            **tagStack;     /* pointers into it                 */
    char            **atts;         /* the attributes of the tag in hand */
    int32_t           textRoom;
    int32_t           attsSlots;
    int32_t           tagSlots;
    int8_t            convertEscChars;
} Parserxml;

Parserxml *CreateParserXML(void);
void       DeleteParserXML(Parserxml *p);
THIS int8_t     px_parseText(Parserxml *p, const char *text, int32_t len,
                        int32_t unused, int8_t convert);
THIS void       px_setTagHandler(Parserxml *p, OpenTagHandlerFn open,
                            CloseTagHandlerFn close);
THIS void       px_setTextHandler(Parserxml *p, CharDataHandlerFn text);
THIS void       px_setData(Parserxml *p, void *data);

/* The scanner's own state, which the parser hands over before each
   document. src/eci/ssml/eci_xmltok.c is where it lives. */
extern char   *TextBuffer;
extern char   *pTextBuffer;
extern char   *pTextBufferEnd;
extern char   *TagBuffer;
extern char   *pTagBuffer;
extern char   *pTagBufferEnd;
extern char   *TagName;
extern char  **TagStack;
extern char  **Atts;
extern int32_t TagCount;
extern int32_t MaxTags;
extern int32_t NumAtts;
extern int32_t MaxAtts;
extern int8_t  ConvertEscChars;
extern char   *inBuf;
extern int32_t inBufPos;
extern int32_t inBufLen;
extern char   *xmltext;
extern int32_t xmlleng;
extern int32_t yy_start;

int32_t xmllex(void);
void    XML_ParserDelete(void);

#endif
