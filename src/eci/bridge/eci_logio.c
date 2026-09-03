/* Logical files: how the Delta machine reaches the outside world.

   The machine never names a file. It names a stream — pgmin, cmdout, the
   one ECI puts text into — and this table says what that stream is made of:
   a list of places to read from, a list of places to write to, and a buffer
   holding the line last read. A physical file is five functions and three
   words of state, so a stream can be a disk file, a block of memory, the
   terminal, or nothing at all without the machine knowing the difference.

   Written to the original's behaviour rather than its instructions. The
   parts that decide what the machine sees are exact: which characters
   vf_getc hands back, when a line is refilled, when one input file gives way
   to the next. The bookkeeping under that is ours. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "delta.h"
#include "eci_io.h"

typedef struct InFile InFile;
struct InFile {
    char        *name;   /* +0x00 */
    PhysicalFile f;      /* +0x04 */
    InFile      *next;   /* +0x24 */
};

typedef struct OutFile OutFile;
struct OutFile {
    char        *name;   /* +0x00 */
    int32_t      opened; /* +0x04 */
    int32_t      pad_08;
    PhysicalFile f;      /* +0x0c */
    OutFile     *next;   /* +0x2c, every output file the machine has */
};

/* A stream writes to a file through one of these, so that two streams can
   share one file. */
typedef struct OutElem OutElem;
struct OutElem {
    int32_t   flush;   /* +0x00 */
    OutFile  *file;    /* +0x04 */
    OutElem  *next;    /* +0x08 */
};

/* Not open, open for reading, open for writing. */
#define LF_CLOSED 0
#define LF_INPUT  1
#define LF_OUTPUT 2

typedef struct {
    char     name[0x14];  /* +0x00 */
    int32_t  state;       /* +0x14 */
    InFile  *in_head;     /* +0x18 */
    InFile  *in_cur;      /* +0x1c */
    OutElem *out_head;    /* +0x20 */
    DynaBuf *line;        /* +0x24, what was last read */
    int32_t  complained;  /* +0x28, so a closed stream says so once */
} LogicalFile;

typedef void (*ReportFn)(delta_state *d, const char *tag, const char *fmt, ...);

typedef struct {
    int8_t       pgmin;      /* +0x00 */
    int8_t       cmdin;      /* +0x01 */
    int8_t       prompt;     /* +0x02 */
    int8_t       prmout;     /* +0x03 */
    int8_t       cmdout;     /* +0x04 */
    int8_t       pgmout;     /* +0x05 */
    int16_t      pad_06;
    LogicalFile *files;      /* +0x08 */
    int32_t      room;       /* +0x0c, how many there can be */
    int32_t      builtin;    /* +0x10, how many the runtime made itself */
    OutFile     *out_files;  /* +0x14, every output file, in one list */
    ReportFn     report;     /* +0x18 */
    int32_t      interrupt;  /* +0x1c */
    PhysicalFile disk;       /* +0x20 */
    PhysicalFile dyna;       /* +0x40 */
    PhysicalFile term;       /* +0x60 */
    PhysicalFile none;       /* +0x80 */
    PhysicalFile err;        /* +0xa0 */
    /* Where the runtime reports an error it cannot handle itself. Set once
       by dtSetErrorCallback, which refuses to set it twice, so it has to
       start at nothing. */
    void        *error_cb;   /* +0xc0 */
} LogIO;

#define LOGIO(d)  EVV_AT(LogIO *, (d)->logio)

/* One callback and no replacing it: a second caller is refused rather than
   taking the first one's place. It is set from here rather than by whoever
   asks, because the field is where this file's compiler put it and not where
   the original had it. */
int32_t logicalIOSetErrorCallback(delta_state *d, void *fn)
{
    LogIO *g = LOGIO(d);

    if (g == 0 || g->error_cb != 0)
        return 0;
    g->error_cb = fn;
    return 1;
}
#define LF(d, n)  (&LOGIO(d)->files[(int)(int8_t)(n)])

/* ---- the physical file classes ---------------------------------------- */

/* Put a space before the end of a line if there is not one there already.
   Both of these leave the cursor where they found it. */
static void addSpace(DynaBuf *b)
{
    uint32_t was = dynaBufMoveRel(b, 0);
    uint32_t back = dynaBufMoveRel(b, -1);

    if (dynaBufCurrentChar(b, 0) == ' ') {
        if (was != back)
            dynaBufMoveRel(b, 1);
        return;
    }
    if (was != back)
        dynaBufMoveRel(b, 1);
    dynaBufAddChar(b, ' ', 0);
}

/* The same, for a line that has just been read whole: answers whether it
   ended in a newline, and makes sure a space comes before it. */
static int addNLspace(DynaBuf *b)
{
    uint32_t was = dynaBufMoveRel(b, -1);
    uint32_t back;

    if (dynaBufCurrentChar(b, 0) != '\n') {
        dynaBufMoveRel(b, 1);
        return 0;
    }
    back = dynaBufMoveRel(b, -1);
    if (dynaBufCurrentChar(b, 0) == ' ') {
        if (was != back)
            dynaBufMoveRel(b, 1);
        return 1;
    }
    if (was != back)
        dynaBufMoveRel(b, 1);
    dynaBufAddChar(b, ' ', 0);
    dynaBufAddChar(b, '\n', 0);
    return 1;
}

static int diskFileOpen(delta_state *d, PhysicalFile *p, int mode)
{
    p->d = d;
    if (p->handle == 0) {
        const char *how = "";

        if (mode == 0)
            how = "r";
        else if (mode == 1)
            how = "w";
        else if (mode == 2)
            how = "a";
        p->handle = fopen(p->name, how);
    }
    return p->handle != 0;
}

static int diskFileRead(PhysicalFile *p, DynaBuf *b, const char *prompt)
{
    char line[64];

    (void)prompt;
    if (p->handle == 0 || feof((FILE *)p->handle))
        return 0;

    line[0] = 0;
    for (;;) {
        if (feof((FILE *)p->handle))
            return 1;
        if (fgets(line, (int)sizeof line, (FILE *)p->handle) == 0
            && !feof((FILE *)p->handle))
            return 0;
        if (!dynaBufAddString(b, line, 0))
            return 0;
        if (addNLspace(b))
            return 1;
    }
}

static int diskFileWrite(PhysicalFile *p, const char *s, int flush)
{
    if (p->handle == 0 || s == 0)
        return 0;
    if (fputs(s, (FILE *)p->handle) == EOF)
        return 0;
    if (!flush)
        return 1;
    return fflush((FILE *)p->handle) != EOF;
}

/* An interrupt counts as the end of every stream, and is taken as it is
   read so that the next question gets a fresh answer. */
static int diskFileEof(PhysicalFile *p)
{
    if (checkInterrupt(p->d)) {
        setInterrupt(p->d, 0);
        return 1;
    }
    if (p->handle != 0 && !feof((FILE *)p->handle))
        return 0;
    return 1;
}

static int diskFileClose(PhysicalFile *p)
{
    if (p->handle != 0)
        return fclose((FILE *)p->handle) == 0;
    return 1;
}

static int dynaBufFileOpen(delta_state *d, PhysicalFile *p, int mode)
{
    p->d = d;
    if (p->handle != 0) {
        if (mode == 0)
            dynaBufMoveAbs((DynaBuf *)p->handle, 0);
        else if (mode == 1)
            dynaBufReset((DynaBuf *)p->handle);
        else if (mode == 2)
            dynaBufMoveAbs((DynaBuf *)p->handle, -1);
    }
    return p->handle != 0;
}

/* One line out of the block, ending in a space and a newline whether or not
   the block had them. */
static int dynaBufFileRead(PhysicalFile *p, DynaBuf *b, const char *prompt)
{
    (void)prompt;
    if (p->handle == 0)
        return 0;
    for (;;) {
        char c = dynaBufCurrentChar((DynaBuf *)p->handle, 1);

        if (c == 0 || c == '\n')
            break;
        if (!dynaBufAddChar(b, c, 0))
            return 0;
    }
    addSpace(b);
    return dynaBufAddChar(b, '\n', 0);
}

static int dynaBufFileWrite(PhysicalFile *p, const char *s, int flush)
{
    (void)flush;
    if (p->handle == 0)
        return 0;
    return dynaBufAddString((DynaBuf *)p->handle, s, 0) ? 1 : 0;
}

static int dynaBufFileEof(PhysicalFile *p)
{
    if (p->handle == 0)
        return 1;
    return dynaBufAtEnd((DynaBuf *)p->handle) ? 1 : 0;
}

static int dynaBufFileClose(PhysicalFile *p)
{
    (void)p;
    return 1;
}

static int stdTermFileOpen(delta_state *d, PhysicalFile *p, int mode)
{
    (void)mode;
    p->d = d;
    return 1;
}

static int stdTermFileRead(PhysicalFile *p, DynaBuf *b, const char *prompt)
{
    (void)p;
    if (prompt != 0)
        fputs(prompt, stdout);
    for (;;) {
        int c = getchar();

        if (c == EOF || c == '\n')
            break;
        if (!dynaBufAddChar(b, (char)c, 0))
            return 0;
    }
    addSpace(b);
    return dynaBufAddChar(b, '\n', 0);
}

static int stdTermFileWrite(PhysicalFile *p, const char *s, int flush)
{
    (void)p;
    if (s == 0)
        return 0;
    if (fputs(s, stdout) == EOF)
        return 0;
    if (!flush)
        return 1;
    return fflush(stdout) != EOF;
}

static int stdTermFileEof(PhysicalFile *p)   { (void)p; return 0; }
static int stdTermFileClose(PhysicalFile *p) { (void)p; return 1; }

static int stdErrorFileOpen(delta_state *d, PhysicalFile *p, int mode)
{
    (void)mode;
    p->d = d;
    return 1;
}

static int stdErrorFileRead(PhysicalFile *p, DynaBuf *b, const char *prompt)
{
    (void)p;
    (void)b;
    (void)prompt;
    return 1;
}

static int stdErrorFileWrite(PhysicalFile *p, const char *s, int flush)
{
    (void)p;
    if (s == 0)
        return 0;
    if (fputs(s, stderr) == EOF)
        return 0;
    if (!flush)
        return 1;
    return fflush(stderr) != EOF;
}

static int stdErrorFileEof(PhysicalFile *p)   { (void)p; return 0; }
static int stdErrorFileClose(PhysicalFile *p) { (void)p; return 1; }

static int nullFileOpen(delta_state *d, PhysicalFile *p, int mode)
{
    (void)mode;
    p->d = d;
    return 1;
}

/* Reading from nothing gives an empty line rather than an end, which is
   what keeps a stream nobody assigned from stopping the machine. */
static int nullFileRead(PhysicalFile *p, DynaBuf *b, const char *prompt)
{
    (void)p;
    (void)prompt;
    dynaBufAddChar(b, '\n', 0);
    return 1;
}

static int nullFileWrite(PhysicalFile *p, const char *s, int flush)
{
    (void)p;
    (void)s;
    (void)flush;
    return 1;
}

static int nullFileEof(PhysicalFile *p)   { (void)p; return 0; }
static int nullFileClose(PhysicalFile *p) { (void)p; return 1; }

static void setClass(PhysicalFile *p,
                     int (*open)(delta_state *, PhysicalFile *, int),
                     int (*read)(PhysicalFile *, DynaBuf *, const char *),
                     int (*write)(PhysicalFile *, const char *, int),
                     int (*eof)(PhysicalFile *),
                     int (*close)(PhysicalFile *))
{
    p->d = 0;
    p->name = 0;
    p->handle = 0;
    p->open = open;
    p->read = read;
    p->write = write;
    p->eof = eof;
    p->close = close;
}

/* ---- the table ---------------------------------------------------------- */

int32_t logio_new(delta_state *d)
{
    LogIO *g;

    if (d == 0)
        return 0;
    g = malloc(sizeof *g);
    d->logio = EVV_REF(g);
    if (g == 0)
        return -2;
    memset(g, 0, sizeof *g);
    g->pgmin = g->cmdin = g->prompt = g->prmout = g->cmdout = g->pgmout = -1;
    setClass(&g->disk, diskFileOpen, diskFileRead, diskFileWrite,
             diskFileEof, diskFileClose);
    setClass(&g->dyna, dynaBufFileOpen, dynaBufFileRead, dynaBufFileWrite,
             dynaBufFileEof, dynaBufFileClose);
    setClass(&g->term, stdTermFileOpen, stdTermFileRead, stdTermFileWrite,
             stdTermFileEof, stdTermFileClose);
    setClass(&g->none, nullFileOpen, nullFileRead, nullFileWrite,
             nullFileEof, nullFileClose);
    setClass(&g->err, stdErrorFileOpen, stdErrorFileRead, stdErrorFileWrite,
             stdErrorFileEof, stdErrorFileClose);
    return 0;
}

/* The class a stream gets when it is wired to nothing. */
void *logicalNullClass(delta_state *d)
{
    return &LOGIO(d)->none;
}

/* Which logical file each of the six standard streams is. They are the first
   six bytes of the block, but only this file knows that. */
int32_t logicalStandardStream(delta_state *d, int32_t which)
{
    const LogIO *g = LOGIO(d);

    switch (which) {
    case 0: return g->pgmin;
    case 1: return g->cmdin;
    case 2: return g->prompt;
    case 3: return g->prmout;
    case 4: return g->cmdout;
    default: return g->pgmout;
    }
}

void logio_delete(delta_state *d)
{
    if (d == 0 || EVV_AT(void *, d->logio) == 0)
        return;
    memset(EVV_AT(void *, d->logio), 0, sizeof(LogIO));
    free(EVV_AT(void *, d->logio));
    d->logio = EVV_REF(0);
}

int32_t checkInterrupt(delta_state *d)
{
    return LOGIO(d)->interrupt;
}

void setInterrupt(delta_state *d, int32_t v)
{
    LOGIO(d)->interrupt = v;
}

int8_t vffind_lf(delta_state *d, const char *name)
{
    int8_t i;

    if (name == 0 || *name == 0)
        return -1;
    for (i = 0; i < (int8_t)LOGIO(d)->room; i++)
        if (strcmp(LF(d, i)->name, name) == 0)
            return i;
    return -1;
}

void *logicalFileName(delta_state *d, int32_t lf)
{
    if ((int8_t)lf < 0 || (int8_t)lf > (int8_t)LOGIO(d)->room)
        return 0;
    return LF(d, lf);
}

int8_t addLogicalFile(delta_state *d, const char *name)
{
    LogicalFile *f;
    int8_t i;

    if (name == 0)
        return -1;
    if (vffind_lf(d, name) != -1) {
        LOGIO(d)->report(d, "LFILE", "%s is already defined", name);
        return -1;
    }
    for (i = 0; LF(d, i)->name[0] != 0; i++)
        ;
    if (i >= (int8_t)LOGIO(d)->room) {
        LOGIO(d)->report(d, "LOGIO",
                         "There are too many interactive logical files");
        return -1;
    }

    f = LF(d, i);
    strncpy(f->name, name, sizeof f->name);
    f->state = LF_CLOSED;
    f->in_cur = 0;
    f->in_head = 0;
    f->out_head = 0;
    if (f->line != 0)
        dynaBufReset(f->line);
    else
        f->line = dynaBufNew(0);
    if (f->line == 0) {
        f->name[0] = 0;
        return -1;
    }
    return i;
}

int8_t vfdef_lf(delta_state *d, const char *name)
{
    return addLogicalFile(d, name);
}

/* The six the runtime always has. Anything else is the language's. */
int32_t logicalIOInit(delta_state *d, int32_t room, void *report)
{
    LogIO *g = LOGIO(d);

    g->report = (ReportFn)report;
    g->room = room;
    g->files = calloc((size_t)room, sizeof(LogicalFile));
    if (g->files == 0)
        return 0;

    g->pgmin = addLogicalFile(d, "pgmin");
    if (g->pgmin == -1)
        return 0;
    g->pgmout = addLogicalFile(d, "pgmout");
    if (g->pgmout == -1)
        return 0;
    g->cmdin = addLogicalFile(d, "cmdin");
    if (g->cmdin == -1)
        return 0;
    g->cmdout = addLogicalFile(d, "cmdout");
    if (g->cmdout == -1)
        return 0;
    g->prompt = addLogicalFile(d, "prompt");
    if (g->prompt == -1)
        return 0;
    g->prmout = addLogicalFile(d, "prmout");
    if (g->prmout == -1)
        return 0;
    g->builtin = g->prmout + 1;
    return 1;
}

int32_t builtInLogicalFiles(delta_state *d)
{
    return LOGIO(d)->builtin;
}

/* ---- reading ------------------------------------------------------------ */

static InFile *findLastInputFile(delta_state *d, int32_t lf)
{
    InFile *p = LF(d, lf)->in_head;

    while (p->next != 0)
        p = p->next;
    return p;
}

static InFile *findInputFile(delta_state *d, const char *name, int32_t lf)
{
    InFile *p;

    if (name == 0)
        return 0;
    for (p = LF(d, lf)->in_head; p != 0; p = p->next)
        if (strcmp(p->name, name) == 0)
            return p;
    return 0;
}

static InFile *allocateInputFile(const char *name, const PhysicalFile *cls,
                                 void *handle)
{
    InFile *p;

    if (name == 0 || cls == 0)
        return 0;
    p = malloc(sizeof *p);
    if (p == 0)
        return 0;
    p->name = malloc(strlen(name) + 1);
    if (p->name == 0) {
        free(p);
        return 0;
    }
    strcpy(p->name, name);
    p->f = *cls;
    p->f.name = p->name;
    p->f.handle = handle;
    p->next = 0;
    return p;
}

static int openInputFile(delta_state *d, InFile *p, int32_t lf)
{
    if (!p->f.open(d, &p->f, 0))
        return 0;
    LF(d, lf)->in_cur = p;
    return 1;
}

static void closeInputFile(InFile *p)
{
    p->f.close(&p->f);
}

static void closeAllInputFiles(delta_state *d, int32_t lf)
{
    InFile *p;

    for (p = LF(d, lf)->in_head; p != 0; p = p->next)
        closeInputFile(p);
}

static int assignInputFile(delta_state *d, const char *name, int32_t lf,
                           const PhysicalFile *cls, void *handle)
{
    LogicalFile *f;
    InFile *p;

    if (name == 0)
        return 0;
    p = allocateInputFile(name, cls, handle);
    if (p == 0)
        return 0;

    f = LF(d, lf);
    if (f->in_head == 0)
        f->in_head = p;
    else
        findLastInputFile(d, lf)->next = p;

    if (f->state == LF_INPUT && f->in_cur == 0
        && !openInputFile(d, p, lf)) {
        LOGIO(d)->report(d, "LFILE ASSIGN",
                         "Can't open assigned input file %s", name);
        return 0;
    }
    return 1;
}

/* A whole line off the stream, echoed to the prompt's output if there is
   one. Coming to the end of one input file moves on to the next; coming to
   the end of the last one answers minus one. */
int32_t vf_gets(delta_state *d, int32_t lf, const char *prompt)
{
    LogicalFile *f = LF(d, lf);
    InFile *p;

    if (f->state != LF_INPUT) {
        LOGIO(d)->report(d, "LOGIO", "Logical file %s is not open for input",
                         f);
        return 0;
    }
    if (f->in_head == 0) {
        LOGIO(d)->report(d, "LOGIO",
                         "No input files are assigned to logical file %s", f);
        return 0;
    }
    p = f->in_cur;
    if (p == 0) {
        LOGIO(d)->report(d, "LOGIO",
                         "No input file is open for logical file %s", f);
        return 0;
    }

    dynaBufReset(f->line);
    for (;;) {
        if (p->f.eof(&p->f)) {
            /* Nothing more here: take the next input file, and if there is
               none the stream is spent. */
            if (p->next == 0)
                return 0;
            closeInputFile(p);
            f->in_cur = p->next;
            p = f->in_cur;
            if (!openInputFile(d, p, lf))
                return 0;
        } else if (!p->f.read(&p->f, f->line, prompt)) {
            return 0;
        }
        if (dynaBufLength(f->line) != 0)
            break;
    }

    if (!vf_puts(d, lf, dynaBufContents(f->line), 1)) {
        LOGIO(d)->report(d, "LOGIO", "Can't echo input to file %s", f);
        return 0;
    }
    return 1;
}

int32_t vf_getc(delta_state *d, int32_t lf)
{
    LogicalFile *f = LF(d, lf);

    if (dynaBufAtEnd(f->line)) {
        if (vf_gets(d, lf, "") != 1)
            return -1;
        dynaBufMoveAbs(LF(d, lf)->line, 0);
    }
    return (int8_t)dynaBufCurrentChar(LF(d, lf)->line, 1);
}

int32_t vf_ungetc(delta_state *d, int32_t lf)
{
    DynaBuf *b = LF(d, lf)->line;

    if (dynaBufMoveRel(b, 0) > 0) {
        dynaBufMoveRel(b, -1);
        return 1;
    }
    return 0;
}

void vf_clrbuf(delta_state *d, int32_t lf)
{
    dynaBufReset(LF(d, lf)->line);
}

DynaBuf *logicalFileInputBuffer(delta_state *d, int32_t lf)
{
    return LF(d, lf)->line;
}

int32_t vf_eof(delta_state *d, int32_t lf)
{
    LogicalFile *f = LF(d, lf);

    if (f->state != LF_INPUT)
        return 0;
    if (f->in_cur != 0 && !f->in_cur->f.eof(&f->in_cur->f))
        return 0;
    return 1;
}

/* ---- writing ------------------------------------------------------------ */

static OutFile *findOutputFile(delta_state *d, const char *name)
{
    OutFile *p;

    for (p = LOGIO(d)->out_files; p != 0; p = p->next)
        if (strcmp(p->name, name) == 0)
            return p;
    return 0;
}

static OutFile *allocateOutputFile(const char *name, const PhysicalFile *cls,
                                   void *handle)
{
    OutFile *p;

    if (name == 0 || cls == 0)
        return 0;
    p = malloc(sizeof *p);
    if (p == 0)
        return 0;
    p->name = malloc(strlen(name) + 1);
    if (p->name == 0) {
        free(p);
        return 0;
    }
    strcpy(p->name, name);
    p->opened = 0;
    p->pad_08 = 0;
    p->f = *cls;
    p->f.name = p->name;
    p->f.handle = handle;
    p->next = 0;
    return p;
}

static OutElem *findOutputElement(delta_state *d, int32_t lf, OutFile *file)
{
    OutElem *e;

    for (e = LF(d, lf)->out_head; e != 0; e = e->next)
        if (e->file == file)
            return e;
    return 0;
}

static int addOutputElement(delta_state *d, int32_t lf, OutFile *file,
                            int32_t flush)
{
    OutElem *e = malloc(sizeof *e);
    LogicalFile *f = LF(d, lf);

    if (e == 0)
        return 0;
    e->flush = flush;
    e->file = file;
    e->next = f->out_head;
    f->out_head = e;
    return 1;
}

static int openOutputFile(delta_state *d, OutFile *p, int32_t mode)
{
    if (!p->f.open(d, &p->f, mode ? 2 : 1))
        return 0;
    p->opened = 1;
    return 1;
}

static void closeOutputElement(OutElem *e)
{
    if (e->file != 0)
        e->file->f.close(&e->file->f);
}

static void closeAllOutputElements(delta_state *d, int32_t lf)
{
    OutElem *e;

    for (e = LF(d, lf)->out_head; e != 0; e = e->next)
        closeOutputElement(e);
}

static int assignOutputFile(delta_state *d, const char *name, int32_t lf,
                            const PhysicalFile *cls, void *handle,
                            int32_t append)
{
    OutFile *p = findOutputFile(d, name);

    if (p == 0) {
        p = allocateOutputFile(name, cls, handle);
        if (p == 0)
            return 0;
        p->next = LOGIO(d)->out_files;
        LOGIO(d)->out_files = p;
    } else if (findOutputElement(d, lf, p) != 0) {
        return 1;
    }

    if (!addOutputElement(d, lf, p, append))
        return 0;
    if (LF(d, lf)->state == LF_CLOSED)
        return 1;
    return openOutputFile(d, p, append);
}

int32_t vf_puts(delta_state *d, int32_t lf, const char *s, int32_t flush)
{
    LogicalFile *f = LF(d, lf);
    OutElem *e;

    if (f->state == LF_CLOSED && !f->complained) {
        LOGIO(d)->report(d, "LOGIO", "Logical file %s is not open for output",
                         f);
        f->complained = 1;
        return 0;
    }
    for (e = f->out_head; e != 0; e = e->next) {
        OutFile *p = e->file;

        if (p == 0)
            continue;
        if (!p->f.write(&p->f, s, flush))
            return 0;
    }
    return 1;
}

/* ---- opening and closing ------------------------------------------------ */

int32_t logicalFileAddPhysical(delta_state *d, int32_t lf, const char *name,
                               void *cls, void *handle, int32_t mode)
{
    if (mode == 0)
        return assignInputFile(d, name, lf, (const PhysicalFile *)cls, handle);
    return assignOutputFile(d, name, lf, (const PhysicalFile *)cls, handle,
                            mode != 1);
}

int32_t logicalFileFindPhysical(delta_state *d, int32_t lf, const char *name,
                                int32_t input, int32_t current)
{
    if (name == 0 || *name == 0)
        return 0;
    if (input) {
        InFile *p = findInputFile(d, name, lf);

        if (current)
            return p == LF(d, lf)->in_cur;
        return p != 0;
    } else {
        OutFile *p = findOutputFile(d, name);

        return p != 0 && findOutputElement(d, lf, p) != 0;
    }
}

static int fileExists(const LogicalFile *f)
{
    FILE *fp = fopen(f->name, "r");

    if (fp == 0)
        return 0;
    fclose(fp);
    return 1;
}

/* Mode nought reads, one writes, two appends. A stream with nothing
   assigned to it takes its own name as a file name. */
int32_t logicalFileOpen(delta_state *d, void *what, int32_t mode)
{
    const char *name = (const char *)what;
    LogicalFile *f;
    OutElem *e;
    int8_t lf = vffind_lf(d, name);

    if (lf == -1) {
        lf = addLogicalFile(d, name);
        if (lf == -1)
            return 0;
    }
    f = LF(d, lf);
    f->complained = 0;

    if (f->state != LF_CLOSED) {
        if (f->state == LF_OUTPUT && mode == 0) {
            LOGIO(d)->report(d, "LFILE OPEN",
                             "The file %s is currently opened for output",
                             logicalFileName(d, lf));
            return 0;
        }
        if (f->state == LF_INPUT && mode == 1) {
            LOGIO(d)->report(d, "LFILE OPEN",
                             "The file %s is currently opened for input",
                             logicalFileName(d, lf));
            return 0;
        }
        return 1;
    }

    if (mode > 0 && mode <= 2) {
        if (f->out_head == 0
            && !assignOutputFile(d, f->name, lf, &LOGIO(d)->disk, 0,
                                 mode != 1)) {
            LOGIO(d)->report(d, "LFILE OPEN",
                             "Can't open logical file %s with no output file",
                             f, f);
            return 0;
        }
    } else if (mode == 0) {
        if (f->in_head == 0) {
            if (!fileExists(f)) {
                LOGIO(d)->report(d, "LFILE OPEN",
                                 "Opening file %s with no input file", f);
                return 0;
            }
            if (!assignInputFile(d, f->name, lf, &LOGIO(d)->disk, 0))
                return 0;
        }
    }

    if (mode == 0 && !openInputFile(d, f->in_head, lf))
        return 0;
    for (e = f->out_head; e != 0; e = e->next)
        if (!openOutputFile(d, e->file, (mode == 2) ? 1 : e->flush))
            return 0;

    if (mode == 0) {
        f->state = LF_INPUT;
        dynaBufReset(f->line);
    } else {
        f->state = LF_OUTPUT;
    }
    return 1;
}

void vfclose_lf(delta_state *d, int32_t lf)
{
    closeAllOutputElements(d, lf);
    closeAllInputFiles(d, lf);
    LF(d, lf)->state = LF_CLOSED;
    LF(d, lf)->in_cur = 0;
}

/* ---- the rest of the surface ------------------------------------------- */

/* Printing to a stream with a format. The printing half of the runtime is
   stubbed, and this is part of it: the machine's own output goes through
   vf_puts, and everything that comes this way is a report or a trace. */
int32_t vf_printf(delta_state *d, int32_t lf, int32_t flush, const char *fmt,
                  ...)
{
    (void)d;
    (void)lf;
    (void)flush;
    (void)fmt;
    return 1;
}

/* What a stream is made of, as text. Part of the printing half. */
void vfstat(delta_state *d, int32_t lf)      { (void)d; (void)lf; }
void vfstatall(delta_state *d)               { (void)d; }

int32_t vfundef_lf(delta_state *d, const char *name)
{
    int8_t lf = vffind_lf(d, name);

    if (lf == -1)
        return 0;
    logicalFileRemoveAllPhysical(d, lf, 1);
    logicalFileRemoveAllPhysical(d, lf, 0);
    LF(d, lf)->name[0] = 0;
    return 1;
}

int32_t logicalFileRemovePhysical(delta_state *d, int32_t lf, const char *name,
                                  int32_t input)
{
    LogicalFile *f = LF(d, lf);

    if (input) {
        InFile **link = &f->in_head;

        while (*link != 0) {
            InFile *p = *link;

            if (strcmp(p->name, name) == 0) {
                if (f->in_cur == p)
                    f->in_cur = p->next;
                *link = p->next;
                closeInputFile(p);
                free(p->name);
                free(p);
                return 1;
            }
            link = &p->next;
        }
        return 0;
    } else {
        OutElem **link = &f->out_head;

        while (*link != 0) {
            OutElem *e = *link;

            if (e->file != 0 && strcmp(e->file->name, name) == 0) {
                *link = e->next;
                closeOutputElement(e);
                free(e);
                return 1;
            }
            link = &e->next;
        }
        return 0;
    }
}

int32_t logicalFileRemoveAllPhysical(delta_state *d, int32_t lf, int32_t input)
{
    LogicalFile *f = LF(d, lf);

    if (input) {
        while (f->in_head != 0) {
            InFile *p = f->in_head;

            f->in_head = p->next;
            closeInputFile(p);
            free(p->name);
            free(p);
        }
        f->in_cur = 0;
    } else {
        while (f->out_head != 0) {
            OutElem *e = f->out_head;

            f->out_head = e->next;
            closeOutputElement(e);
            free(e);
        }
    }
    return 1;
}

/* Give back everything the table holds. The output files are shared between
   streams, so they are freed once, from the one list they all sit in. */
void logicalIOCleanup(delta_state *d)
{
    LogIO *g = LOGIO(d);
    int32_t i;

    if (g == 0 || g->files == 0)
        return;
    for (i = 0; i < g->room; i++) {
        logicalFileRemoveAllPhysical(d, i, 1);
        logicalFileRemoveAllPhysical(d, i, 0);
        if (g->files[i].line != 0) {
            dynaBufDelete(g->files[i].line);
            g->files[i].line = 0;
        }
    }
    while (g->out_files != 0) {
        OutFile *p = g->out_files;

        g->out_files = p->next;
        free(p->name);
        free(p);
    }
    free(g->files);
    g->files = 0;
}
