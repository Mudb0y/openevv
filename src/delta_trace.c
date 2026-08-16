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

   Three deliberate divergences, all so that a rule keeps running rather
   than derailing on a machine that cannot do what it asked:

     - A bad token is not reported, and the read is not tried again. The
       original reports it and reads on, which here would turn for ever on
       a token that never parses.

     - Nothing reads a backslash escape, so a token containing one takes
       whatever getbksl answers, which is nothing.

     - The debugger's trace and display do nothing at all.

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
void disptok(delta_state *d, ...)       { (void)d; }
void lithex(delta_state *d, ...)        { (void)d; }

/* Three of them are not stubs, because a run uses them. Opening a stream
   is how the engine reaches its own input and output, which are not files
   at all on the way in; and reading a variable releases the field it was
   asked about whether or not there was anything to read. The file layer
   underneath is still the platform's. */
int open_input(delta_state *d, int32_t which)
{
    int r = logicalFileOpen(d, logicalFileName(d, which, 0));

    if (r == 0)
        forceErrorBacktrack(d);
    return r;
}

int open_output(delta_state *d, int32_t which)
{
    int r = logicalFileOpen(d, logicalFileName(d, which, 1));

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
    uint8_t st = ((const unsigned char *)v)[4];
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
            d->owner[0x14] = 0;
            memset(d->owner + 0x1a8, 0, 4);
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

int  getbksl(delta_state *d, ...)       { (void)d; return 0; }

void readErrorReport(delta_state *d, ...) { (void)d; }
void var_rderr(delta_state *d, ...)       { (void)d; }
/* Nothing here reports a bad token, and answering that the read should be
   tried again would turn for ever on one that never parses, so it says to
   give up. That is a divergence: the original reports it and reads on. */
int  rdtokverr(delta_state *d, int32_t f, uint8_t st, const char *buf)
{
    (void)d;
    (void)f;
    (void)st;
    (void)buf;
    return 1;
}
