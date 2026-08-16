/* The Delta runtime's printing, reading and file layer, deliberately not
   transcribed.

   These thirteen functions exist for two purposes in the original: the
   Delta debugger's trace and display, and reading rule files. They rest on ANSI
   standard input and output, so they are portable in principle, but the
   targets this port is for have no filesystem worth the name and no reason
   to carry a debugger. They are stubbed here rather than left undefined, so
   that a rule which names one still links and still runs.

   Every one is variadic. The original's argument lists are not reproduced,
   because nothing here reads them, and a variadic stub is the one shape a
   caller can pass anything to without the call being undefined.

   Two deliberate divergences, both so that a rule keeps running rather than
   derailing on a machine that cannot do what it asked:

     - The two file opens do nothing and report nothing. The original
       backtracks when an open fails, which here would throw a rule out for
       asking a question the port does not answer. A rule that opens a file
       and reads from it will find it empty.

     - The reading side answers as though there were nothing to read.

   Three names the runtime calls are not here at all. gettok and
   print_prompt are rules the language supplies rather than runtime
   entries, and stubbing them would take the language's own answers away.
   vrd_tvar is the runtime's, but it is the live reader a sentence goes
   through two hundred times: it calls the language's gettok, checks for an
   interrupt, and puts what it read into the variable. It has still to be
   transcribed; until then the original's own is linked.

   If a target ever wants the trace, the printing half is the place to put
   it back: give vf_puts and vf_printf somewhere to write and transcribe the
   six io.obj entries above them. */

#include "delta.h"

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
void rdtokverr(delta_state *d, ...)       { (void)d; }
