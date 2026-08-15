/* The Delta runtime's printing, reading and file layer, deliberately not
   transcribed.

   These sixteen functions exist for two purposes in the original: the Delta
   debugger's trace and display, and reading rule files. They rest on ANSI
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

   If a target ever wants the trace, the printing half is the place to put
   it back: give vf_puts and vf_printf somewhere to write and transcribe the
   six io.obj entries above them. */

#include "delta.h"

void print_lit(delta_state *d, ...)     { (void)d; }
void print_var(delta_state *d, ...)     { (void)d; }
void print_stream(delta_state *d, ...)  { (void)d; }
void print_prompt(delta_state *d, ...)  { (void)d; }
void vprt_var(delta_state *d, ...)      { (void)d; }
void vprt_strm(delta_state *d, ...)     { (void)d; }
void disptok(delta_state *d, ...)       { (void)d; }
void lithex(delta_state *d, ...)        { (void)d; }

void open_input(delta_state *d, ...)    { (void)d; }
void open_output(delta_state *d, ...)   { (void)d; }

int  read_tvar(delta_state *d, ...)     { (void)d; return 0; }
int  vrd_tvar(delta_state *d, ...)      { (void)d; return 0; }
int  gettok(delta_state *d, ...)        { (void)d; return 0; }
int  getbksl(delta_state *d, ...)       { (void)d; return 0; }

void readErrorReport(delta_state *d, ...) { (void)d; }
void var_rderr(delta_state *d, ...)       { (void)d; }
void rdtokverr(delta_state *d, ...)       { (void)d; }
