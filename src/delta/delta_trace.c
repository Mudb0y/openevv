/* The Delta runtime's printing, reading and file layer.

   Two things live here. The printing half is stubbed: it exists in the
   original for the Delta debugger's trace and display, it rests on ANSI
   standard input and output, and the targets this port is for have no
   reason to carry a debugger. Those stubs are variadic, because the
   original's argument lists are not reproduced and nothing here reads them,
   and a variadic stub is the one shape a caller can pass anything to
   without the call being undefined.

   The reading half is transcribed, because a sentence goes through it two
   hundred times. Opening a stream, reading a variable off one, taking a
   token off one: none of those is a file operation in any useful sense on
   the way in, whatever their names suggest.

   One divergence, and it is the printing again: the debugger's trace and
   display do nothing at all, and neither does the line an error report
   would have written.

   What a report decides is another matter, and that is not a divergence.
   Both reporters ask whether the console the Delta debugger reads from is
   open, by looking for a physical file called cmdwin or one called pgmwin.
   No object in the library ever creates either, so neither is ever found,
   and both answer that the read should be given up rather than tried
   again. That is what the two below answer, which is what the original
   answers here; a target that ever grows a console is where the rest of
   them would go back in.

   Two names the runtime calls are not here: gettok and print_prompt are
   rules the language supplies rather than runtime entries, and stubbing
   them would take the language's own answers away. The reader's own
   tokeniser was also called gettok, privately, and is below under a name
   that does not collide with the language's.

   If a target ever wants the trace, the printing half is the place to put
   it back: give vf_puts and vf_printf somewhere to write and transcribe the
   six io.obj entries above them. */

#include <stdlib.h>
#include <string.h>

#include "delta.h"

/* The character a backslash stands for.

   Three digits at most is not what this does: it reads octal digits until
   one is not, and hex the same way after an x, putting the character that
   ended it back. Everything else is the C escapes, and a backslash before
   anything else is that thing. */
int8_t getbksl(delta_state *d, int32_t f)
{
    int32_t c = vf_getc(d, f);
    int32_t v = 0;

    if (c >= '0' && c <= '7') {
        while (c >= '0' && c <= '7') {
            v = v * 8 + (c - '0');
            c = vf_getc(d, f);
        }
        vf_ungetc(d, f);
        return (int8_t)v;
    }

    if (c == 'x' || c == 'X') {
        for (;;) {
            int32_t adjust;

            c = vf_getc(d, f);
            if (c >= '0' && c <= '9')
                adjust = -'0';
            else if (c >= 'a' && c <= 'f')
                adjust = -('a' - 10);
            else if (c >= 'A' && c <= 'F')
                adjust = -('A' - 10);
            else
                break;
            v = v * 16 + c + adjust;
        }
        vf_ungetc(d, f);
        return (int8_t)v;
    }

    switch (c) {
    case 'a':  v = 7;  break;
    case 'b':  v = 8;  break;
    case 'f':  v = 12; break;
    case 'n':  v = 10; break;
    case 'r':  v = 13; break;
    case 't':  v = 9;  break;
    case 'v':  v = 11; break;
    default:   v = c;  break;
    }
    return (int8_t)v;
}

/* One run of characters off the stream, with no notion of quoting or of a
   statement type: spaces before it are skipped, a space after it ends it,
   and so does the end of the line. Answers the character it stopped on, or
   nought when there is nothing left. */
static int8_t getnum(delta_state *d, int32_t f, char *buf)
{
    int started = 0;

    for (;;) {
        int32_t c = vf_getc(d, f);

        if (c == 10) {
            *buf = 0;
            return (int8_t)c;
        }
        if (c == -1 || c == 0) {
            *buf = 0;
            return 0;
        }
        if (c == ' ') {
            if (!started)
                continue;
            *buf = 0;
            return (int8_t)c;
        }
        *buf++ = (char)c;
        started = 1;
    }
}

/* One token off the stream.

   The original calls this gettok, privately, and the language happens to
   have a rule of the same name; they are different things and the reader
   means this one. It reads until the token ends: at the end of a line, at a
   space unless the statement takes a whole line as one token, or at the
   marks the statement type brackets a quoted token with. Answers the
   character it stopped on, or nought when there is nothing left.

   The buffer it fills is the caller's, and it is left holding the last
   character written so that the answer can be read back out of it. */
static int8_t read_token(delta_state *d, uint8_t st, int32_t f, char *buf)
{
    const delta_stmt *e = &vstmtbl[st];
    int16_t kind = STMTYP((int8_t)st);
    int whole = (e->whole_token == 1);
    int numeric = (kind == DK_SHORT2 || kind == DK_LONG);
    int quoted = 0;
    int started = 0;

    for (;;) {
        int32_t ch;
        int plain = 1;

        ch = vf_getc(d, f);
        if (ch == '\\') {
            ch = (int8_t)getbksl(d, f);
            plain = 0;
        }

        if (plain && ch == 10) {
            if (quoted) {
                if (!whole)
                    return 0;
                *buf++ = (char)e->marks[0];
                *buf = 0;
                return buf[-1];
            }
            *buf = 0;
            if (!started)
                return (int8_t)ch;
            vf_ungetc(d, f);
            return buf[-1];
        }

        if (plain && (ch == -1 || ch == 0)) {
            *buf = 0;
            return 0;
        }

        if (plain && ch == (int8_t)e->marks[0]) {
            if (!quoted) {
                quoted = 1;
                continue;
            }
            if (ch != (int8_t)e->marks[1])
                return 0;
            *buf = 0;
            return buf[-1];
        }

        if (plain && ch == (int8_t)e->marks[1]) {
            if (!quoted)
                return 0;
            *buf = 0;
            return buf[-1];
        }

        if (plain && ch == ' ' && !whole && !quoted) {
            if (!started)
                continue;
            *buf = 0;
            return (int8_t)ch;
        }

        *buf++ = (char)ch;
        if (numeric || !whole) {
            started = 1;
            continue;
        }
        if (quoted)
            continue;
        *buf = 0;
        return buf[-1];
    }
}

void print_lit(delta_state *d, ...)     { (void)d; }
void print_var(delta_state *d, ...)     { (void)d; }
void print_stream(delta_state *d, ...)  { (void)d; }
void vprt_var(delta_state *d, ...)      { (void)d; }
void vprt_strm(delta_state *d, ...)     { (void)d; }

/* One field of one token, under the name the language gives that value.
   IBM's own was its debugger's display and this port left it empty, which
   left every phoneme the engine reports nameless -- so nothing could read
   what a language had decided a word was made of. It is the statement table
   that knows: a field declares the names its values may take, and
   field_value in eci_access.c already turns one into the other for the
   dictionary's sake.

   The pointer arrives four bytes into the token, which is how the original's
   caller hands it over; field_value counts eight from the token itself. */
void disptok(delta_state *d, const void *at, int32_t stream, int32_t field,
             char *out)
{
    extern char *field_value(int8_t f, void *tok, int32_t fld);

    *out = 0;
    if (at == 0 || stream < 0 || stream >= (int32_t)d->nstmts)
        return;
    if (field < 0 || field >= vstmtbl[stream].nfields)
        return;
    strcpy(out, field_value((int8_t)stream, (char *)at - 4, field));
}

/* Spell a token so that it can be shown: the printable characters as they
   are, everything else as an escape. Only a report ever asks, so it leaves
   the answer empty. */
void lithex(const char *in, char *out, int32_t max)
{
    (void)in;
    (void)max;
    *out = 0;
}

/* Three of them are not stubs, because a run uses them. Opening a stream
   is how the engine reaches its own input and output, which are not files
   at all on the way in; and reading a variable releases the field it was
   asked about whether or not there was anything to read. The file layer
   underneath is still the platform's. */
int open_input(delta_state *d, int32_t which)
{
    int r = logicalFileOpen(d, logicalFileName(d, which), 0);

    if (r == 0)
        forceErrorBacktrack(d);
    return r;
}

int open_output(delta_state *d, int32_t which)
{
    int r = logicalFileOpen(d, logicalFileName(d, which), 1);

    if (r == 0)
        forceErrorBacktrack(d);
    return r;
}

/* Read one token into a variable.

   The token itself comes from the language's own gettok, which is a rule
   rather than anything in here. What is done with it depends on what the
   variable is: a name is looked up among the ones its field declares and
   kept as its number, and a number is parsed. A token that fits none of
   those is reported and the read is tried again.

   Answers whether it gave up: nothing more to read, or an interrupt, or a
   report that said to stop. */
int vrd_tvar(delta_state *d, int32_t f, const delta_operand *v)
{
    char buf[40];
    uint8_t st = (uint8_t)v->kind;
    const delta_fielddesc *fd = &vstmtbl[st].fields[0];
    const char *const *names = (const char *const *)fd->values;
    void *where = 0;
    int32_t lval = 0;
    int32_t ival = 0;
    int16_t sval = 0;
    uint8_t bval = 0;
    int8_t c = 0;
    int again = 1;

    while (again) {
        again = 0;
        c = read_token(d, st, f, buf);

        if (c == 0 || checkInterrupt(d)) {
            EVV_AT(delta_owner *, d->owner)->unknown_14 = 0;
            EVV_AT(delta_owner *, d->owner)->unknown_1a8 = 0;
            return 1;
        }
        if (c == 10) {
            again = 1;
            continue;
        }

        switch (STMTYP((int8_t)st)) {
        case DK_UBYTE:
            for (bval = 0; bval < fd->nvalues; bval++)
                if (strcmp(buf, names[bval]) == 0)
                    break;
            if (bval != fd->nvalues) {
                where = &bval;
                break;
            }
            if (rdtokverr(d, f, st, buf))
                return 1;
            again = 1;
            break;

        case DK_SHORT:
            for (ival = 0; ival < fd->nvalues; ival++)
                if (strcmp(buf, names[ival]) == 0)
                    break;
            if (ival != fd->nvalues) {
                where = &ival;
                break;
            }
            if (rdtokverr(d, f, st, buf))
                return 1;
            again = 1;
            break;

        case DK_LONG:
            where = &lval;
            if (chk_itok(buf)) {
                lval = atol(buf);
                break;
            }
            if (rdtokverr(d, f, st, buf))
                return 1;
            again = 1;
            break;

        case DK_SHORT2:
            where = &sval;
            if (chk_itok(buf)) {
                sval = (int16_t)atoi(buf);
                break;
            }
            if (rdtokverr(d, f, st, buf))
                return 1;
            again = 1;
            break;

        default:
            break;
        }
    }

    vinitflds(d, st, ((void *const *)v)[0], where);

    /* The line the token was on ends here unless something else is on it. */
    if (c != 10 && vf_getc(d, f) != 10)
        vf_ungetc(d, f);
    return 0;
}

int read_tvar(delta_state *d, int8_t f, delta_loc *field)
{
    delta_operand v;
    int r;

    vinitloc_new(d, &v, field);
    r = vrd_tvar(d, f, &v) ? 1 : 0;
    reset_field(field);
    return r;
}

/* Say what went wrong. The original writes the line to whichever output
   stream goes with the input it was reading; the printing half is stubbed,
   so nothing is written. */
void readErrorReport(delta_state *d, ...) { (void)d; }

/* Whether to give up on a read rather than try it again.

   The original tries again only if the Delta debugger's console is there
   to type the answer into: it looks for a physical file called cmdwin, and
   failing that one called pgmwin, and if it finds one it rubs the bad
   token out of the input buffer, prints the complaint and asks the caller
   to read on. Nothing in the library ever creates either file, so the
   search fails and the answer is to give up, which is all these two do. */
int var_rderr(delta_state *d, int32_t f, const char *buf)
{
    (void)d;
    (void)f;
    (void)buf;
    return 1;
}

/* The same, and it drops everything the stack has above the block a read
   was collecting into before it answers. That only happens on the console
   side, which is not reached here. */
static int dlt_rderr(delta_state *d, int32_t f, const char *buf)
{
    return var_rderr(d, f, buf);
}

int rdtokverr(delta_state *d, int32_t f, uint8_t st, const char *buf)
{
    char spelt[75];

    lithex(buf, spelt, (int32_t)sizeof spelt);
    readErrorReport(d, f, "DELTIO",
                    "\"%s\" is not a token name in stream %s",
                    spelt, vstmtbl[st].name);
    return var_rderr(d, f, spelt) ? 1 : 0;
}

/* Find a rule activation on the stack by the number it goes under.

   Unwinding is threaded twice over: each return record points at the
   activation that made it and each activation points at the record beneath
   it, so following the two alternately walks the stack outwards. What comes
   back is not the activation whose number matched but the one a step further
   out, because a rule names the activation it would return into rather than
   its own.

   Nothing checks that the step further out exists. The original does not
   either, and by the time this is called the activation asked for is one the
   caller has already been handed a number for, so there is always something
   under it. */
void *vonstack(delta_state *d, int32_t ctx)
{
    void **p = (void **)EVV_AT(uint8_t *, EVV_AT(delta_vars *, d->vars)->back);

    while (p) {
        void **act = (void **)p[1];
        int found = *(const int32_t *)act == ctx;

        p = (void **)act[7];
        if (found)
            return p[1];
    }
    return 0;
}

/* Where a variable lives.

   A reference with the top bit set names one of the language's own, which
   are kept in a table after everything else the variables block holds. The
   rest name a variable of whichever rule activation was asked for: the one
   running unless the caller says otherwise, and that one is reached
   through the record an unwind would return to rather than by searching
   the stack for it. */
void *varloc(delta_state *d, uint8_t hi, uint8_t lo, int32_t ctx)
{
    int32_t ref = ((int32_t)hi << 8) | lo;
    int32_t idx = ref & 0x3fff;
    void *act;

    if (ref & 0x8000)
        return *(void *const *)((uint8_t *)EVV_AT(delta_vars *, d->vars) + 0x11e4 + 4 * idx);

    if (ctx == 0)
        ctx = EVV_AT(delta_vars *, d->vars)->running;
    if (ctx == EVV_AT(delta_vars *, d->vars)->running)
        act = ((void *const *)EVV_AT(uint8_t *, EVV_AT(delta_vars *, d->vars)->back))[1];
    else
        act = vonstack(d, ctx);
    if (act == 0)
        return 0;
    return ((void *const *)((void *const *)act)[2])[idx];
}

/* Read a number off a stream into a variable.

   Unlike the token reader this takes whatever is between the spaces and
   asks only that it spell a whole number. Nothing to read is not the end
   of the story: the original prompts and reads on if there is a console,
   which here there is not, so it gives up. */
int vrd_nvar(delta_state *d, int32_t f, const delta_operand *v)
{
    char buf[44];
    int8_t c = 0;
    int again = 1;

    while (again) {
        again = 0;
        c = getnum(d, f, buf);

        if (c == 0) {
            EVV_AT(delta_owner *, d->owner)->unknown_14 = 0;
            EVV_AT(delta_owner *, d->owner)->unknown_1a8 = 0;
            again = 1;
            if (var_rderr(d, f, buf))
                return 1;
            continue;
        }

        switch (v->kind) {
        case DK_LONG:
            if (chk_itok(buf)) {
                *(int32_t *)v->ptr = atol(buf);
                break;
            }
            readErrorReport(d, f, "DELTIO",
                            "\"%s\" is not an long integer (long)", buf);
            if (var_rderr(d, f, buf))
                return 1;
            again = 1;
            break;

        case DK_SHORT2:
            if (chk_itok(buf)) {
                *(int16_t *)v->ptr = (int16_t)atoi(buf);
                break;
            }
            readErrorReport(d, f, "DELTIO", "\"%s\" is not an integer", buf);
            if (var_rderr(d, f, buf))
                return 1;
            again = 1;
            break;

        default:
            break;
        }
    }

    /* The line the number was on ends here unless something else is on it. */
    if (c != 10 && vf_getc(d, f) != 10)
        vf_ungetc(d, f);
    return 0;
}

/* Read a whole line of one statement type's values off a stream.

   Everything read goes on the Delta stack, under a block of its own so
   that the caller can drop the lot in one go, and each value is pushed as
   it is parsed. The line ends the read; nothing to read ends it as well,
   and so does an interrupt.

   The original carries a flag saying not to push what it just read.
   Nothing ever sets it, so everything read is pushed. */
int vrd_delta(delta_state *d, int32_t f, uint8_t st)
{
    char buf[40];
    delta_operand v;
    delta_frame *block;
    int32_t lval = 0;
    int32_t ival = 0;
    int16_t sval = 0;
    uint8_t bval = 0;
    const delta_fielddesc *fd = &vstmtbl[st].fields[0];

    v.ptr = 0;
    v.kind = STMTYP((int8_t)st);
    v.flag = fd->flag;

    EVV_AT(delta_stack *, d->stack)->top -= EVV_AT(delta_stack *, d->stack)->size_b8;
    block = (delta_frame *)EVV_AT(uint8_t *, EVV_AT(delta_stack *, d->stack)->top);
    EVV_AT(delta_stack *, d->stack)->limit -= EVV_AT(delta_stack *, d->stack)->size_b8;
    block->kind = 5;
    block->value = EVV_REF(getDeltaStackVBot(d));
    setDeltaStackVBot(d, block);

    for (;;) {
        int8_t c = read_token(d, st, f, buf);

        if (c == 10)
            return 0;
        if (checkInterrupt(d))
            return 1;
        if (c == 0) {
            EVV_AT(delta_owner *, d->owner)->unknown_14 = 0;
            EVV_AT(delta_owner *, d->owner)->unknown_1a8 = 0;
            return 1;
        }

        switch (STMTYP((int8_t)st)) {
        case DK_UBYTE:
            v.ptr = &bval;
            for (bval = 0; bval < fd->nvalues; bval++)
                if (strcmp(buf, ((const char *const *)fd->values)[bval]) == 0)
                    break;
            if (bval == fd->nvalues) {
                readErrorReport(d, f, "DELTIO", "\"%s\" is not a token name",
                                buf);
                if (dlt_rderr(d, f, buf))
                    return 1;
            }
            break;

        case DK_SHORT:
            v.ptr = &ival;
            for (ival = 0; ival < fd->nvalues; ival++)
                if (strcmp(buf, ((const char *const *)fd->values)[ival]) == 0)
                    break;
            if (ival == fd->nvalues) {
                readErrorReport(d, f, "DELTIO", "\"%s\" is not a token name",
                                buf);
                v.ptr = 0;
                if (dlt_rderr(d, f, buf))
                    return 1;
            }
            break;

        case DK_LONG:
            v.ptr = &lval;
            if (chk_itok(buf)) {
                lval = atol(buf);
                break;
            }
            readErrorReport(d, f, "DELTIO", "\"%s\" is not a token name", buf);
            if (dlt_rderr(d, f, buf))
                return 1;
            break;

        case DK_SHORT2:
            v.ptr = &sval;
            if (chk_itok(buf)) {
                sval = (int16_t)atoi(buf);
                break;
            }
            readErrorReport(d, f, "DELTIO", "\"%s\" is not a token name", buf);
            if (dlt_rderr(d, f, buf))
                return 1;
            break;

        default:
            break;
        }

        vpush_var(d, &v);
    }
}

/* Writing the machine out and reading it back.

   Nothing in the engine calls any of this; it is the Delta debugger's save
   and restore, and it is here because a target that wants to keep a machine
   between runs is what it is for. The numbers go out big end first, however
   the target orders its own.

   Four of them are empty in the original and empty here: what to do about a
   read that came up short, a message to go with it, a limit the format
   cannot express, and a write that failed. They are the places a target
   would put its own answer. */
void svgeterr(delta_state *d, int32_t which)  { (void)d; (void)which; }
void svgetmsg(delta_state *d)                 { (void)d; }
void svgetimp(delta_state *d, int32_t which)  { (void)d; (void)which; }
void svputerr(delta_state *d)                 { (void)d; }

int32_t svgetl(delta_state *d)
{
    uint8_t b[4];

    if (delta_save_read(d, b, 4) != 4)
        svgeterr(d, 1);
    return ((int32_t)b[0] << 24) | ((int32_t)b[1] << 16)
         | ((int32_t)b[2] << 8) | b[3];
}

int svgeti(delta_state *d)
{
    uint8_t b[2];
    int16_t v;

    if (delta_save_read(d, b, 2) != 2)
        svgeterr(d, 1);
    v = (int16_t)b[0];
    v = (int16_t)((v << 8) | b[1]);
    return v;
}

int8_t svgetc(delta_state *d)
{
    int8_t c;

    if (delta_save_read(d, &c, 1) != 1)
        svgeterr(d, 2);
    return c;
}

uint8_t svgetu(delta_state *d)
{
    uint8_t c;

    if (delta_save_read(d, &c, 1) != 1)
        svgeterr(d, 3);
    return c;
}

/* One name off the stream, up to its terminator. The hundred it has room
   for is a limit it reports rather than one it keeps to, which is the
   original's own behaviour and worth knowing before a target trusts it. */
char *svgets(delta_state *d)
{
    char *buf = EVV_AT(delta_stack *, d->stack)->save_name;
    int i;

    for (i = 0;; i++) {
        if (i >= (int)sizeof EVV_AT(delta_stack *, d->stack)->save_name)
            svgetimp(d, 1);
        if (delta_save_read(d, buf + i, 1) != 1)
            svgeterr(d, 4);
        if (buf[i] == 0)
            break;
    }
    return buf;
}

void svputl(delta_state *d, int32_t v)
{
    uint8_t b[4];

    b[0] = (uint8_t)((v >> 24) & 0xff);
    b[1] = (uint8_t)((v >> 16) & 0xff);
    b[2] = (uint8_t)((v >> 8) & 0xff);
    b[3] = (uint8_t)(v & 0xff);
    if (delta_save_write(d, b, 4) != 4)
        svputerr(d);
}

void svputi(delta_state *d, int32_t v)
{
    uint8_t b[2];

    b[0] = (uint8_t)((v >> 8) & 0xff);
    b[1] = (uint8_t)(v & 0xff);
    if (delta_save_write(d, b, 2) != 2)
        svputerr(d);
}

void svputc(delta_state *d, int8_t c)
{
    if (delta_save_write(d, &c, 1) != 1)
        svputerr(d);
}

void svputu(delta_state *d, uint8_t c)
{
    if (delta_save_write(d, &c, 1) != 1)
        svputerr(d);
}

void svputs(delta_state *d, const char *s)
{
    int32_t n = (int32_t)strlen(s) + 1;

    if (delta_save_write(d, s, n) != n)
        svputerr(d);
}

/* The global variables pointing at a node. Empty in the original, and the
   local ones below are what it was going to be. */
void svputgptrs(delta_state *d) { (void)d; }

/* Every variable of the running rule that points at this node, written out
   as names with a separator before the first and an empty name after the
   last. Only the sync-valued ones count, and only those the rule has not
   marked as its own. */
void svputlptrs(delta_state *d, int32_t node, int8_t sep)
{
    const delta_actdesc *desc;
    void *const *slots;
    void *act;
    int wrote = 0;
    int i;

    if (EVV_AT(uint8_t *, EVV_AT(delta_vars *, d->vars)->back) == 0)
        return;
    act = ((void *const *)EVV_AT(uint8_t *, EVV_AT(delta_vars *, d->vars)->back))[1];
    if (act == 0)
        return;
    desc = (const delta_actdesc *)EVV_AT(delta_vars *, d->vars)->running;
    if (desc == 0)
        return;

    slots = (void *const *)((void *const *)act)[2];
    for (i = 0; i < desc->nlocals; i++) {
        const delta_varinfo *v = &desc->locals[i];

        if (v->kind != DK_SYNC || (v->flag & 0x80))
            continue;
        if (*(const int32_t *)slots[i] != node)
            continue;
        if (!wrote) {
            svputc(d, sep);
            wrote = 1;
        }
        svputs(d, v->name);
    }
    if (wrote)
        svputs(d, "");
}

/* The sync node with this number, looked for from one end of the spine.
   Not finding it is a limit rather than an error, because a saved machine
   may name a node this one does not have. */
int32_t findsync(delta_state *d, int32_t n, int8_t dir)
{
    int32_t want = n * 4;
    int32_t s = EVV_AT(delta_stack *, d->stack)->spine_l;

    while (s != 0) {
        if ((*(const int32_t *)(size_t)s & ~3) == want)
            break;
        s = VRSYNC(d, (const int32_t *)(size_t)s, dir);
    }
    if (s == 0)
        svgetimp(d, 2);
    return s;
}

/* Write the machine out as a Delta command script, and read one back.

   The writer is not a serialiser: it prints the commands that would build
   the spine again, which is why every line of it goes through the printing
   half. With that half stubbed there is nothing for it to write, so it is
   stubbed too rather than transcribed into a routine that walks the spine
   and says nothing. The other two are empty in the original as well: the
   reader was never written. */
int vsvdelta(delta_state *d, uint8_t stream)
{
    (void)stream;
    if (EVV_AT(delta_stack *, d->stack)->sync_size == 0)
        return 0;
    return 0;
}

void vsv2delta(delta_state *d)  { (void)d; }
int  vrsdelta2(delta_state *d)  { (void)d; return 0; }
