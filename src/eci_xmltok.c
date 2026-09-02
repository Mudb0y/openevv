/* The XML scanner: flex's skeleton, and IBM's forty-eight rules.
 *
 * `xmltok.obj' is generated code. IBM wrote a lex grammar, flex 2.5.4 built
 * an automaton out of it, and the object holds the automaton and the
 * skeleton that walks it. The grammar itself was never shipped.
 *
 * So the two halves are separated here. `src/eci_xmltok_tables.c' is the
 * automaton, lifted out of the object by `tools/lift-xmltok.py' -- eight
 * tables, nine start conditions, forty-eight rules, a hundred and sixty-six
 * states. This file is everything else: flex's own skeleton, written out by
 * hand because it is flex's and the same in every scanner it has ever
 * generated, and the action for each of the forty-eight rules, which is
 * IBM's and is transcribed.
 *
 * Taking the automaton rather than guessing the grammar is what makes the
 * tokenisation the same by construction. A hand-written scanner would have
 * had to be right about every corner -- a stray quote in an attribute, an
 * entity inside a quoted value, whitespace where none is expected -- and
 * would only ever have been shown right on the corners somebody thought of.
 *
 * What the rules do, in outline. Text between tags is accumulated a
 * character at a time into a buffer and handed over as one run when a tag
 * starts; the five named entities are turned into their characters, or left
 * as they were written if `ConvertEscChars' is off. A tag pushes its name on
 * a stack, hands its name and attributes over, and a close tag pops the
 * stack and refuses a name that does not match. `/>' is handled by handing
 * over the open tag and then pushing `/>' back into the input so that the
 * close rule fires on it next.
 *
 * Running out of room in a buffer is not an error. The rule pushes what it
 * was about to write back into the input, returns a token that says which
 * buffer to grow, and Parserxml::ParseText grows it and calls the scanner
 * again -- so the same text is scanned twice and comes out once.
 *
 * One divergence, and the reason is that this is a library. IBM's
 * `yy_fatal_error' prints to stderr and calls exit, which is flex's own
 * default and is what a standalone scanner wants; a speech engine may not
 * take the caller's process down over a sixteen-kilobyte tag name. It
 * records the message instead and the scanner reports out of memory, which
 * ParseText already knows how to fail on.
 *
 * The scanner's state is one set of file-scope variables, as flex's always
 * is, so one document is read at a time. SSMLFilter holds a mutex across
 * the whole of ParseText for exactly that reason, and that is the
 * original's arrangement rather than something added here.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "evv_abi.h"
#include "eci_ssml.h"
#include "eci_xmltok.h"

extern int ralStrIcmp(int n, const char *a, const char *b);

/* ---- what the automaton was built with -------------------------------- */

/* The state at which a transition has to go through the equivalence-class
   map a second time, and the base value that means a state has jammed.
   Both are flex's own numbers for this automaton and both are read off the
   original's code rather than worked out. */
#define YY_JAM_STATE   166
#define YY_JAM_BASE    246

/* How many rules there are, plus the two flex adds: one for the end of a
   buffer and one per start condition for the end of the input. */
#define YY_NUM_RULES   48
#define YY_END_OF_BUFFER (YY_NUM_RULES + 1)

/* A start condition is held as 1 + 2*n. Nine of them; the second is
   declared in the grammar and never entered, which is the original's and
   is left alone. */
#define SC(n)          (1 + 2 * (n))
#define SC_TEXT        SC(0)   /* between tags, nothing accumulated       */
#define SC_UNUSED      SC(1)   /* declared and never begun                */
#define SC_IN_TAG      SC(2)   /* inside a tag, past its name             */
#define SC_AFTER_EQ    SC(3)   /* inside a tag, past an attribute's `='   */
#define SC_IN_TEXT     SC(4)   /* accumulating a run of character data    */
#define SC_CLOSE_BACK  SC(5)   /* `/>' pushed back, close rule to come    */
#define SC_RESUME_A    SC(6)   /* text handed over; rescan what stopped it */
#define SC_RESUME_B    SC(7)   /* the same, from another rule             */
#define SC_RESUME_C    SC(8)   /* and the same again                      */

/* The size of the buffer the scanner reads through, and the most it asks
   the input for at once. Both are the original's. */
#define YY_BUF_SIZE    0x4000
#define YY_READ_MAX    0x2000

/* What xmllex answers. ParseText knows these numbers. */
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

/* What AddAtt and PushTag answer when they cannot. */
#define ADD_FULL       (-1)
#define ADD_NO_ROOM    (-2)

/* ---- the scanner's own state ------------------------------------------ */

/* flex's buffer. The field order is the original's, because the whole
   skeleton reads it. */
typedef struct yy_buffer_state {
    FILE  *yy_input_file;
    char  *yy_ch_buf;
    char  *yy_buf_pos;
    int32_t yy_buf_size;
    int32_t yy_n_chars;
    int32_t yy_is_our_buffer;
    int32_t yy_is_interactive;
    int32_t yy_at_bol;
    int32_t yy_fill_buffer;
    int32_t yy_buffer_status;
} YY_BUFFER_STATE;

#define YY_BUFFER_NEW    0
#define YY_BUFFER_NORMAL 1
#define YY_BUFFER_EOF    2

/* What Parserxml hands over before every parse. */
char   *TextBuffer;
char   *pTextBuffer;
char   *pTextBufferEnd;
char   *TagBuffer;
char   *pTagBuffer;
char   *pTagBufferEnd;
char   *TagName;
char  **TagStack;
char  **Atts;
int32_t TagCount;
int32_t MaxTags;
int32_t NumAtts;
int32_t MaxAtts;
int8_t  ConvertEscChars = 1;

/* The text the input comes from, rather than a file. */
char   *inBuf;
int32_t inBufPos;
int32_t inBufLen;

/* And what the caller reads back. */
char   *xmltext;
int32_t xmlleng;
FILE   *xmlin;
FILE   *xmlout;

static YY_BUFFER_STATE *yy_current_buffer;
static char    yy_hold_char;
static int32_t yy_n_chars;
static char   *yy_c_buf_p;
/* One, so that the first call sets the scanner up. The original keeps
   this and ConvertEscChars in its data section for the same reason. */
static int32_t yy_init = 1;

/* The start condition. Not static, alone among the scanner's own state,
   because Parserxml::ParseText sets it before every document; the two were
   one file in the original and are two here. */
int32_t yy_start;
static int32_t yy_did_buffer_switch_on_eof;

/* The REJECT machinery. Every state the walk passed through is remembered,
   so that a rule whose match is thrown away can be replaced by the next
   longest one; `yy_lp' walks the list of rules a state accepts. */
static int32_t yy_state_buf[YY_BUF_SIZE + 2];
static int32_t *yy_state_ptr;
static char   *yy_full_match;
static int32_t *yy_full_state;
static int32_t yy_lp;
static int32_t yy_full_lp;
static int32_t yy_looking_for_trail_begin;

/* Set instead of calling exit; see the head of this file. */
static int32_t yy_fatal;

static void yy_fatal_error(const char *message)
{
    fprintf(stderr, "%s\n", message);
    yy_fatal = 1;
}

static void *yy_flex_alloc(uint32_t n)
{
    return malloc(n);
}

static void yy_flex_free(void *p)
{
    free(p);
}

/* ---- the buffers the actions write into ------------------------------- */

int32_t AddChar2Buffer(char c)
{
    if (pTextBuffer == pTextBufferEnd)
        return -1;
    *pTextBuffer++ = c;
    return 0;
}

/* A string and the nought after it, so what comes back can be kept. */
char *AddStr2Buffer(char *s)
{
    char *where;

    if (pTextBuffer + strlen(s) + 1 > pTextBufferEnd)
        return 0;
    strcpy(pTextBuffer, s);
    where = pTextBuffer;
    pTextBuffer += strlen(s) + 1;
    return where;
}

/* A string without the nought, so the next one runs on from it. */
char *AddStr2Buffer2(char *s)
{
    char *where;

    if (pTextBuffer + strlen(s) + 1 > pTextBufferEnd)
        return 0;
    strcpy(pTextBuffer, s);
    where = pTextBuffer;
    pTextBuffer += strlen(s);
    return where;
}

/* An attribute name or value. A null argument is a value that was not
   there, which the list carries as a null of its own. */
int32_t AddAtt(char *s)
{
    char *kept = 0;

    if (s != 0) {
        kept = AddStr2Buffer(s);
        if (kept == 0)
            return ADD_NO_ROOM;
    }

    if (NumAtts == MaxAtts)
        return ADD_FULL;

    Atts[NumAtts] = kept;
    NumAtts++;
    return 0;
}

int32_t PushTag(char *name)
{
    if (pTagBuffer + strlen(name) + 1 > pTagBufferEnd)
        return ADD_FULL;
    strcpy(pTagBuffer, name);

    if (TagCount == MaxTags)
        return ADD_NO_ROOM;

    TagStack[TagCount] = pTagBuffer;
    TagCount++;
    pTagBuffer += strlen(name) + 1;
    return 0;
}

/* A close tag has to name what is on top, ignoring case. */
int32_t PopTag(char *name)
{
    if (TagCount <= 0 || ralStrIcmp(0, name, TagStack[TagCount - 1]) != 0)
        return -1;

    pTagBuffer = TagStack[TagCount - 1];
    TagCount--;
    return 0;
}

/* ---- flex's buffer handling ------------------------------------------- */

static void xml_load_buffer_state(void)
{
    yy_n_chars = yy_current_buffer->yy_n_chars;
    xmltext = yy_c_buf_p = yy_current_buffer->yy_buf_pos;
    xmlin = yy_current_buffer->yy_input_file;
    yy_hold_char = *yy_c_buf_p;
}

void xml_flush_buffer(YY_BUFFER_STATE *b)
{
    b->yy_n_chars = 0;
    b->yy_ch_buf[0] = 0;
    b->yy_ch_buf[1] = 0;
    b->yy_buf_pos = b->yy_ch_buf;
    b->yy_at_bol = 1;
    b->yy_buffer_status = YY_BUFFER_NEW;

    if (b == yy_current_buffer)
        xml_load_buffer_state();
}

void xml_init_buffer(YY_BUFFER_STATE *b, FILE *file)
{
    xml_flush_buffer(b);
    b->yy_input_file = file;
    b->yy_fill_buffer = 1;
    b->yy_is_interactive = 0;
}

YY_BUFFER_STATE *xml_create_buffer(FILE *file, int32_t size)
{
    YY_BUFFER_STATE *b = yy_flex_alloc(sizeof *b);

    if (b == 0)
        yy_fatal_error("out of dynamic memory in yy_create_buffer()");
    if (b == 0)
        return 0;

    b->yy_buf_size = size;
    b->yy_ch_buf = yy_flex_alloc((uint32_t)b->yy_buf_size + 2);
    if (b->yy_ch_buf == 0) {
        yy_fatal_error("out of dynamic memory in yy_create_buffer()");
        return b;
    }
    b->yy_is_our_buffer = 1;

    xml_init_buffer(b, file);
    return b;
}

void xml_delete_buffer(YY_BUFFER_STATE *b)
{
    if (b == 0)
        return;
    if (b == yy_current_buffer)
        yy_current_buffer = 0;
    if (b->yy_is_our_buffer)
        yy_flex_free(b->yy_ch_buf);
    yy_flex_free(b);
}

void xml_switch_to_buffer(YY_BUFFER_STATE *b)
{
    if (yy_current_buffer == b)
        return;

    if (yy_current_buffer != 0) {
        *yy_c_buf_p = yy_hold_char;
        yy_current_buffer->yy_buf_pos = yy_c_buf_p;
        yy_current_buffer->yy_n_chars = yy_n_chars;
    }

    yy_current_buffer = b;
    xml_load_buffer_state();
    yy_did_buffer_switch_on_eof = 1;
}

void xmlrestart(FILE *file)
{
    if (yy_current_buffer == 0)
        yy_current_buffer = xml_create_buffer(xmlin, YY_BUF_SIZE);
    xml_init_buffer(yy_current_buffer, file);
    xml_load_buffer_state();
}

YY_BUFFER_STATE *xml_scan_buffer(char *base, uint32_t size)
{
    YY_BUFFER_STATE *b;

    if (size < 2 || base[size - 2] != 0 || base[size - 1] != 0)
        return 0;

    b = yy_flex_alloc(sizeof *b);
    if (b == 0) {
        yy_fatal_error("out of dynamic memory in yy_scan_buffer()");
        return 0;
    }

    b->yy_buf_size = (int32_t)size - 2;
    b->yy_buf_pos = b->yy_ch_buf = base;
    b->yy_is_our_buffer = 0;
    b->yy_input_file = 0;
    b->yy_n_chars = b->yy_buf_size;
    b->yy_is_interactive = 0;
    b->yy_at_bol = 1;
    b->yy_fill_buffer = 0;
    b->yy_buffer_status = YY_BUFFER_NEW;

    xml_switch_to_buffer(b);
    return b;
}

YY_BUFFER_STATE *xml_scan_bytes(const char *bytes, int32_t len)
{
    YY_BUFFER_STATE *b;
    char    *buf;
    uint32_t n = (uint32_t)len + 2;
    int32_t  i;

    buf = yy_flex_alloc(n);
    if (buf == 0) {
        yy_fatal_error("out of dynamic memory in yy_scan_bytes()");
        return 0;
    }

    for (i = 0; i < len; i++)
        buf[i] = bytes[i];
    buf[len] = buf[len + 1] = 0;

    b = xml_scan_buffer(buf, n);
    if (b == 0)
        yy_fatal_error("bad buffer in yy_scan_bytes()");
    else
        b->yy_is_our_buffer = 1;
    return b;
}

YY_BUFFER_STATE *xml_scan_string(const char *s)
{
    return xml_scan_bytes(s, (int32_t)strlen(s));
}

/* What the parser calls between documents: the buffer goes and the scanner
   starts again from the top on the next call. */
void XML_ParserDelete(void)
{
    if (yy_current_buffer == 0)
        return;
    xml_delete_buffer(yy_current_buffer);
    yy_current_buffer = 0;
    yy_init = 1;
}

int32_t xmlwrap(void)
{
    return 1;
}

/* ---- pushing a character back into the input -------------------------- */

/* flex's own unput. Where there is no room in front of the read position
   the whole of what is left is shifted to the end of the buffer first. */
static void yyunput(int32_t c, char *yy_bp)
{
    char *yy_cp = yy_c_buf_p;

    *yy_cp = yy_hold_char;

    if (yy_cp < yy_current_buffer->yy_ch_buf + 2) {
        int32_t number_to_move = yy_n_chars + 2;
        char   *dest = &yy_current_buffer->yy_ch_buf[
                            yy_current_buffer->yy_buf_size + 2];
        char   *source = &yy_current_buffer->yy_ch_buf[number_to_move];

        while (source > yy_current_buffer->yy_ch_buf)
            *--dest = *--source;

        yy_cp += (int32_t)(dest - source);
        yy_bp += (int32_t)(dest - source);
        yy_n_chars = yy_current_buffer->yy_buf_size;

        if (yy_cp < yy_current_buffer->yy_ch_buf + 2)
            yy_fatal_error("flex scanner push-back overflow");
    }

    *--yy_cp = (char)c;

    /* The length of the match is deliberately left alone. Several of the
       actions push a whole match back one character at a time and index
       what they are pushing by that length, so a push that shortened it
       would drop the last character of every one. */
    xmltext = yy_bp;
    yy_hold_char = *yy_cp;
    yy_c_buf_p = yy_cp;
}

/* ---- and filling it ---------------------------------------------------- */

#define EOB_ACT_CONTINUE_SCAN     0
#define EOB_ACT_END_OF_FILE       1
#define EOB_ACT_LAST_MATCH        2

static int32_t yy_get_next_buffer(void)
{
    char   *dest = yy_current_buffer->yy_ch_buf;
    char   *source = xmltext;
    int32_t number_to_move;
    int32_t i;
    int32_t ret_val;

    if (yy_c_buf_p > &yy_current_buffer->yy_ch_buf[yy_n_chars + 1])
        yy_fatal_error(
            "fatal flex scanner internal error--end of buffer missed");

    if (!yy_current_buffer->yy_fill_buffer) {
        if (yy_c_buf_p - xmltext == 1)
            return EOB_ACT_END_OF_FILE;
        return EOB_ACT_LAST_MATCH;
    }

    number_to_move = (int32_t)(yy_c_buf_p - xmltext) - 1;

    for (i = 0; i < number_to_move; i++)
        *dest++ = *source++;

    if (yy_current_buffer->yy_buffer_status == YY_BUFFER_EOF) {
        yy_n_chars = 0;
    } else {
        int32_t num_to_read = yy_current_buffer->yy_buf_size
                              - number_to_move - 1;
        int32_t left;

        /* IBM's own loop here waits for room that nothing in this scanner
           ever makes, because the buffer is never enlarged. What used to
           follow was flex's exit; see the head of this file. */
        if (num_to_read <= 0)
            yy_fatal_error(
                "input buffer overflow, can't enlarge buffer because "
                "scanner uses REJECT");

        if (num_to_read > YY_READ_MAX)
            num_to_read = YY_READ_MAX;

        left = inBufLen - inBufPos;
        if (left >= num_to_read) {
            memcpy(&yy_current_buffer->yy_ch_buf[number_to_move],
                   inBuf + inBufPos, (size_t)num_to_read);
            yy_n_chars = num_to_read;
            inBufPos += num_to_read;
        } else if (left > 0) {
            memcpy(&yy_current_buffer->yy_ch_buf[number_to_move],
                   inBuf + inBufPos, (size_t)left);
            yy_n_chars = left;
            inBufPos = inBufLen;
        } else {
            yy_n_chars = 0;
        }
    }

    if (yy_n_chars == 0) {
        if (number_to_move == 0) {
            ret_val = EOB_ACT_END_OF_FILE;
            xmlrestart(xmlin);
        } else {
            ret_val = EOB_ACT_LAST_MATCH;
            yy_current_buffer->yy_buffer_status = YY_BUFFER_EOF;
        }
    } else {
        ret_val = EOB_ACT_CONTINUE_SCAN;
    }

    yy_n_chars += number_to_move;
    yy_current_buffer->yy_ch_buf[yy_n_chars] = 0;
    yy_current_buffer->yy_ch_buf[yy_n_chars + 1] = 0;
    xmltext = yy_current_buffer->yy_ch_buf;

    return ret_val;
}

/* ---- walking the automaton -------------------------------------------- */

static int32_t yy_get_previous_state(void)
{
    int32_t yy_current_state = yy_start;
    char   *yy_cp;

    yy_state_ptr = yy_state_buf;
    *yy_state_ptr++ = yy_current_state;

    for (yy_cp = xmltext; yy_cp < yy_c_buf_p; yy_cp++) {
        uint8_t yy_c = *yy_cp ? (uint8_t)yy_ec[(uint8_t)*yy_cp] : 1;

        while (yy_chk[yy_base[yy_current_state] + yy_c] != yy_current_state) {
            yy_current_state = yy_def[yy_current_state];
            if (yy_current_state >= YY_JAM_STATE)
                yy_c = (uint8_t)yy_meta[yy_c];
        }
        yy_current_state = yy_nxt[yy_base[yy_current_state] + yy_c];
        *yy_state_ptr++ = yy_current_state;
    }

    return yy_current_state;
}

static int32_t yy_try_NUL_trans(int32_t yy_current_state)
{
    uint8_t yy_c = 1;

    while (yy_chk[yy_base[yy_current_state] + yy_c] != yy_current_state) {
        yy_current_state = yy_def[yy_current_state];
        if (yy_current_state >= YY_JAM_STATE)
            yy_c = (uint8_t)yy_meta[yy_c];
    }
    yy_current_state = yy_nxt[yy_base[yy_current_state] + yy_c];
    *yy_state_ptr++ = yy_current_state;

    return yy_current_state == YY_JAM_STATE ? 0 : yy_current_state;
}

/* ---- the entity rules ------------------------------------------------- */

/* Every one of the twenty named-entity rules is the same shape: put the
   character the entity means into the text buffer, and if there is no room
   push the entity's own spelling back into the input and ask for a bigger
   buffer. With conversion off the spelling goes into the buffer as it was
   written instead.
 *
 * The quoted forms are the ones that matched inside an attribute value, so
 * the quotes go back too. Which spelling is pushed back is the rule's own,
 * and the pairs of rules that differ only in the start condition they leave
 * are the original's; both are here. */
static int32_t entity(char meant, const char *spelling, const char *literal,
                      int32_t *token)
{
    if (ConvertEscChars) {
        if (AddChar2Buffer(meant) == -1) {
            size_t i = strlen(spelling);

            while (i > 0)
                yyunput((uint8_t)spelling[--i], xmltext);
            *token = TOK_GROW_TEXT;
            return 1;
        }
        return 0;
    }

    if (AddStr2Buffer2((char *)literal) == 0) {
        *token = TOK_NO_MEMORY;
        return 1;
    }
    return 0;
}

/* ---- the scanner ------------------------------------------------------- */

int32_t xmllex(void)
{
    int32_t yy_current_state;
    char   *yy_cp;
    char   *yy_bp;
    int32_t yy_act;

    if (yy_init) {
        yy_init = 0;
        NumAtts = 0;
        TextBuffer[0] = 0;

        if (!yy_start)
            yy_start = SC_TEXT;
        if (!xmlin)
            xmlin = stdin;
        if (!xmlout)
            xmlout = stdout;
        if (!yy_current_buffer)
            yy_current_buffer = xml_create_buffer(xmlin, YY_BUF_SIZE);
        xml_load_buffer_state();
    }

    for (;;) {
        yy_cp = yy_c_buf_p;
        *yy_cp = yy_hold_char;
        yy_bp = yy_cp;
        yy_current_state = yy_start;

        yy_state_ptr = yy_state_buf;
        *yy_state_ptr++ = yy_current_state;

    yy_match:
        for (;;) {
            uint8_t yy_c = (uint8_t)yy_ec[(uint8_t)*yy_cp];

            while (yy_chk[yy_base[yy_current_state] + yy_c]
                   != yy_current_state) {
                yy_current_state = yy_def[yy_current_state];
                if (yy_current_state >= YY_JAM_STATE)
                    yy_c = (uint8_t)yy_meta[yy_c];
            }
            yy_current_state = yy_nxt[yy_base[yy_current_state] + yy_c];
            *yy_state_ptr++ = yy_current_state;
            yy_cp++;

            if (yy_base[yy_current_state] == YY_JAM_BASE)
                break;
        }

    yy_find_action:
        yy_current_state = *--yy_state_ptr;
        yy_lp = yy_accept[yy_current_state];

        /* Which of the rules this state accepts is the one to run. A rule
           marked 0x2000 is the head of a trailing context and the one
           marked 0x4000 is its tail, so the head is remembered and the
           search carries on until the tail turns up; anything unmarked is
           a plain rule and wins at once. When a state accepts nothing the
           match is shortened by a character and the state before it is
           asked instead, which is what makes REJECT possible and is why
           every state the walk passed through was written down. */
        for (;;) {
            if (yy_lp != 0 && yy_lp < yy_accept[yy_current_state + 1]) {
                yy_act = yy_acclist[yy_lp];
                if ((yy_act & 0x4000) != 0 || yy_looking_for_trail_begin) {
                    if (yy_act == yy_looking_for_trail_begin) {
                        yy_looking_for_trail_begin = 0;
                        yy_act &= ~0x4000;
                        break;
                    }
                } else if ((yy_act & 0x2000) != 0) {
                    yy_looking_for_trail_begin = yy_act & ~0x2000;
                    yy_looking_for_trail_begin |= 0x4000;
                } else {
                    yy_full_match = yy_cp;
                    yy_full_state = yy_state_ptr;
                    yy_full_lp = yy_lp;
                    break;
                }
                yy_lp++;
                continue;
            }
            yy_cp--;
            yy_current_state = *--yy_state_ptr;
            yy_lp = yy_accept[yy_current_state];
        }

        xmltext = yy_bp;
        xmlleng = (int32_t)(yy_cp - yy_bp);
        yy_hold_char = *yy_cp;
        *yy_cp = 0;
        yy_c_buf_p = yy_cp;

    do_action:
        if (yy_fatal)
            return TOK_NO_MEMORY;

        switch (yy_act) {

        /* ---- 1 to 3: a run of text ended by something ----------------- */

        case 1:
        case 2:
            yy_start = SC_RESUME_A;
            return TOK_CHAR_DATA;

        case 3:
            yy_start = SC_TEXT;
            break;

        /* ---- 4 to 23: the five named entities ------------------------- */

        case 4: {
            int32_t token;

            pTextBuffer = TextBuffer;
            if (entity('>', ";tg&", "&gt;", &token))
                return token;
            yy_start = SC_IN_TEXT;
            break;
        }

        case 5: {
            int32_t token;

            pTextBuffer = TextBuffer;
            if (entity('>', "\";tg&\"", "\"&gt;\"", &token))
                return token;
            break;
        }

        case 6: {
            int32_t token;

            if (entity('>', ";tg&", "&gt;", &token))
                return token;
            break;
        }

        case 7: {
            int32_t token;

            if (entity('>', "\";tg&\"", "\"&gt;\"", &token))
                return token;
            break;
        }

        case 8: {
            int32_t token;

            pTextBuffer = TextBuffer;
            if (entity('&', ";pma&", "&amp;", &token))
                return token;
            yy_start = SC_IN_TEXT;
            break;
        }

        case 9: {
            int32_t token;

            pTextBuffer = TextBuffer;
            if (entity('&', "\";pma&\"", "\"&amp;\"", &token))
                return token;
            yy_start = SC_IN_TEXT;
            break;
        }

        case 10: {
            int32_t token;

            if (entity('&', "\";pma&\"", "\"&amp;\"", &token))
                return token;
            break;
        }

        case 11: {
            int32_t token;

            if (entity('&', ";pma&", "&amp;", &token))
                return token;
            break;
        }

        case 12: {
            int32_t token;

            pTextBuffer = TextBuffer;
            if (entity('<', ";tl&", "&lt;", &token))
                return token;
            yy_start = SC_IN_TEXT;
            break;
        }

        case 13: {
            int32_t token;

            pTextBuffer = TextBuffer;
            if (entity('<', "\";tl&\"", "\"&lt;\"", &token))
                return token;
            yy_start = SC_IN_TEXT;
            break;
        }

        case 14: {
            int32_t token;

            if (entity('<', ";tl&", "&lt;", &token))
                return token;
            break;
        }

        case 15: {
            int32_t token;

            if (entity('<', "\";tl&\"", "\"&lt;\"", &token))
                return token;
            break;
        }

        case 16: {
            int32_t token;

            pTextBuffer = TextBuffer;
            if (entity('\'', ";sopa&", "&apos;", &token))
                return token;
            yy_start = SC_IN_TEXT;
            break;
        }

        case 17: {
            int32_t token;

            pTextBuffer = TextBuffer;
            if (entity('\'', "\";sopa&\"", "\"&apos;\"", &token))
                return token;
            yy_start = SC_IN_TEXT;
            break;
        }

        case 18: {
            int32_t token;

            if (entity('\'', ";sopa&", "&apos;", &token))
                return token;
            break;
        }

        case 19: {
            int32_t token;

            if (entity('\'', "\";sopa&\"", "\"&apos;\"", &token))
                return token;
            break;
        }

        /* The literal the original writes for this one is `&quote;' where
           every other place spells it `&quot;'. It is a slip and it is
           kept: with conversion off, an unconverted quote entity in text
           comes out with an e in it. */
        case 20: {
            int32_t token;

            pTextBuffer = TextBuffer;
            if (entity('"', ";touq&", "&quote;", &token))
                return token;
            yy_start = SC_IN_TEXT;
            break;
        }

        case 21: {
            int32_t token;

            if (entity('"', ";touq&", "&quot;", &token))
                return token;
            break;
        }

        case 22: {
            int32_t token;

            if (entity('"', "\";touq&\"", "\"&quot;\"", &token))
                return token;
            break;
        }

        case 23: {
            int32_t token;

            pTextBuffer = TextBuffer;
            if (entity('"', "\";touq&\"", "\"&quot;\"", &token))
                return token;
            yy_start = SC_IN_TEXT;
            break;
        }

        /* ---- 24 to 32: what is skipped, and the three resumptions ----- */

        case 24:
            break;

        case 25:
        case 26:
            yy_start = SC_RESUME_C;
            return TOK_CHAR_DATA;

        case 27:
            yy_start = SC_TEXT;
            break;

        case 28:
            break;

        case 29:
            yy_start = SC_RESUME_B;
            return TOK_CHAR_DATA;

        case 30:
        case 31:
            yy_start = SC_TEXT;
            break;

        case 32:
            break;

        /* ---- 33 to 44: the tags --------------------------------------- */

        /* A close tag written out in full: the name runs from the third
           character to the one before the last. */
        case 33:
            xmltext[xmlleng - 1] = 0;
            TagName = AddStr2Buffer(xmltext + 2);
            if (TagName == 0)
                return TOK_NO_MEMORY;
            if (PopTag(TagName) == -1)
                return TOK_MISMATCH;
            return TOK_CLOSE_TAG;

        /* An open tag's name. What follows it is scanned inside the tag. */
        case 34:
            TagName = AddStr2Buffer(xmltext + 1);
            if (TagName == 0)
                return TOK_NO_MEMORY;
            {
                int32_t rc = PushTag(TagName);
                int32_t i;

                if (rc == ADD_FULL) {
                    for (i = xmlleng - 1; i >= 0; i--)
                        yyunput(xmltext[i], xmltext);
                    return TOK_GROW_TAG;
                }
                if (rc == ADD_NO_ROOM) {
                    for (i = xmlleng - 1; i >= 0; i--)
                        yyunput(xmltext[i], xmltext);
                    return TOK_GROW_STACK;
                }
            }
            yy_start = SC_IN_TAG;
            break;

        /* The `>' that ends an open tag. The attribute list is closed with
           a null and the tag is handed over. */
        case 35: {
            int32_t rc = AddAtt(0);

            if (rc == ADD_FULL) {
                yyunput('>', xmltext);
                return TOK_GROW_ATTS;
            }
            if (rc == ADD_NO_ROOM)
                return TOK_NO_MEMORY;
            yy_start = SC_TEXT;
            return TOK_OPEN_TAG;
        }

        /* And `/>'. The open tag is handed over and the two characters go
           back into the input, so that the close rule fires on them next
           in a start condition of their own. */
        case 36: {
            int32_t rc = AddAtt(0);

            if (rc == ADD_FULL) {
                yyunput('>', xmltext);
                yyunput('/', xmltext);
                return TOK_GROW_ATTS;
            }
            if (rc == ADD_NO_ROOM)
                return TOK_NO_MEMORY;
            yyunput('>', xmltext);
            yyunput('/', xmltext);
            yy_start = SC_CLOSE_BACK;
            return TOK_OPEN_TAG;
        }

        case 37:
            break;

        /* An attribute name followed by `='. Two rules with one action;
           they differ in what may sit between the name and the sign. */
        case 38:
        case 39: {
            int32_t rc;
            int32_t i;

            xmltext[xmlleng - 1] = 0;
            rc = AddAtt(xmltext);
            if (rc == ADD_FULL) {
                yyunput('=', xmltext);
                for (i = xmlleng - 2; i >= 0; i--)
                    yyunput(xmltext[i], xmltext);
                return TOK_GROW_ATTS;
            }
            if (rc == ADD_NO_ROOM)
                return TOK_NO_MEMORY;
            yy_start = SC_AFTER_EQ;
            break;
        }

        case 40:
            return TOK_BAD_TAG;

        case 41:
            break;

        /* A quoted attribute value: the quotes are dropped. */
        case 42: {
            int32_t rc;
            int32_t i;

            xmltext[xmlleng - 1] = 0;
            rc = AddAtt(xmltext + 1);
            if (rc == ADD_FULL) {
                yyunput('"', xmltext);
                for (i = xmlleng - 2; i >= 0; i--)
                    yyunput(xmltext[i], xmltext);
                return TOK_GROW_ATTS;
            }
            if (rc == ADD_NO_ROOM)
                return TOK_NO_MEMORY;
            yy_start = SC_IN_TAG;
            break;
        }

        case 43:
            break;

        /* The `>' of the pushed-back `/>': the tag that was just opened is
           closed again, by the name on top of the stack. */
        case 44:
            TagName = AddStr2Buffer(TagStack[TagCount - 1]);
            if (TagName == 0)
                return TOK_NO_MEMORY;
            if (PopTag(TagName) == -1)
                return TOK_MISMATCH;
            yy_start = SC_TEXT;
            return TOK_CLOSE_TAG;

        /* ---- 45 to 48: a character of text at a time ------------------ */

        case 45:
            pTextBuffer = TextBuffer;
            if (AddChar2Buffer(xmltext[0]) == -1) {
                yyunput(xmltext[0], xmltext);
                return TOK_GROW_TEXT;
            }
            yy_start = SC_IN_TEXT;
            break;

        /* Something that is not text: keep one character of the match,
           push it back, and hand over what has been accumulated. */
        case 46:
            *yy_cp = yy_hold_char;
            yy_cp = yy_bp + 1;
            yy_c_buf_p = yy_cp;
            xmltext = yy_bp;
            xmlleng = (int32_t)(yy_cp - yy_bp);
            yy_hold_char = *yy_cp;
            *yy_cp = 0;
            yy_c_buf_p = yy_cp;
            yyunput(xmltext[0], xmltext);
            yy_start = SC_TEXT;
            return TOK_CHAR_DATA;

        case 47:
            if (AddChar2Buffer(xmltext[0]) == -1) {
                yyunput(xmltext[0], xmltext);
                return TOK_GROW_TEXT;
            }
            break;

        case 48:
            break;

        /* ---- the end of the input, and the end of a buffer ------------ */

        case YY_END_OF_BUFFER: {
            int32_t yy_amount_of_matched_text =
                (int32_t)(yy_cp - xmltext) - 1;

            *yy_cp = yy_hold_char;

            if (yy_current_buffer->yy_buffer_status == YY_BUFFER_NEW) {
                yy_n_chars = yy_current_buffer->yy_n_chars;
                yy_current_buffer->yy_input_file = xmlin;
                yy_current_buffer->yy_buffer_status = YY_BUFFER_NORMAL;
            }

            /* A nought that was in the text rather than the one that marks
               the end of what has been read. The walk is done again up to
               it and the automaton is asked whether it has a transition on
               it; if it has, the nought is consumed like any character. */
            if (yy_c_buf_p <= &yy_current_buffer->yy_ch_buf[yy_n_chars]) {
                int32_t yy_next_state;

                yy_c_buf_p = xmltext + yy_amount_of_matched_text;
                yy_current_state = yy_get_previous_state();
                yy_next_state = yy_try_NUL_trans(yy_current_state);
                yy_bp = xmltext;

                if (yy_next_state != 0) {
                    yy_cp = ++yy_c_buf_p;
                    yy_current_state = yy_next_state;
                    goto yy_match;
                }
                yy_cp = yy_c_buf_p;
                goto yy_find_action;
            }

            switch (yy_get_next_buffer()) {
            case EOB_ACT_END_OF_FILE:
                yy_did_buffer_switch_on_eof = 0;
                if (xmlwrap()) {
                    yy_c_buf_p = xmltext;
                    yy_act = YY_END_OF_BUFFER + 1 + (yy_start - 1) / 2;
                    goto do_action;
                }
                if (!yy_did_buffer_switch_on_eof)
                    xmlrestart(xmlin);
                break;

            case EOB_ACT_CONTINUE_SCAN:
                yy_c_buf_p = xmltext + yy_amount_of_matched_text;
                yy_current_state = yy_get_previous_state();
                yy_cp = yy_c_buf_p;
                yy_bp = xmltext;
                goto yy_match;

            case EOB_ACT_LAST_MATCH:
                yy_c_buf_p = &yy_current_buffer->yy_ch_buf[yy_n_chars];
                yy_current_state = yy_get_previous_state();
                yy_cp = yy_c_buf_p;
                yy_bp = xmltext;
                goto yy_find_action;
            }
            break;
        }

        default:
            /* The end of the input, one case per start condition. A run of
               text that was still being accumulated is handed over first;
               anything else is the end. */
            if ((yy_start - 1) / 2 == 4) {
                yy_start = SC_TEXT;
                return TOK_CHAR_DATA;
            }
            return TOK_END;
        }
    }
}

ALIAS("?AddChar2Buffer@@YAHD@Z", "AddChar2Buffer");
ALIAS("?AddStr2Buffer@@YAPADPAD@Z", "AddStr2Buffer");
ALIAS("?AddStr2Buffer2@@YAPADPAD@Z", "AddStr2Buffer2");
ALIAS("?AddAtt@@YAHPAD@Z", "AddAtt");
ALIAS("?PushTag@@YAHPAD@Z", "PushTag");
ALIAS("?PopTag@@YAHPAD@Z", "PopTag");
ALIAS("?xmllex@@YAHXZ", "xmllex");
ALIAS("?xmlrestart@@YAXPAU_iobuf@@@Z", "xmlrestart");
ALIAS("?xml_switch_to_buffer@@YAXPAUyy_buffer_state@@@Z",
      "xml_switch_to_buffer");
ALIAS("?xml_create_buffer@@YAPAUyy_buffer_state@@PAU_iobuf@@H@Z",
      "xml_create_buffer");
ALIAS("?xml_delete_buffer@@YAXPAUyy_buffer_state@@@Z", "xml_delete_buffer");
ALIAS("?xml_init_buffer@@YAXPAUyy_buffer_state@@PAU_iobuf@@@Z",
      "xml_init_buffer");
ALIAS("?xml_flush_buffer@@YAXPAUyy_buffer_state@@@Z", "xml_flush_buffer");
ALIAS("?xml_scan_buffer@@YAPAUyy_buffer_state@@PADI@Z", "xml_scan_buffer");
ALIAS("?xml_scan_string@@YAPAUyy_buffer_state@@PBD@Z", "xml_scan_string");
ALIAS("?xml_scan_bytes@@YAPAUyy_buffer_state@@PBDH@Z", "xml_scan_bytes");
ALIAS("?XML_ParserDelete@@YAXXZ", "XML_ParserDelete");
ALIAS("_xmlwrap", "xmlwrap");
