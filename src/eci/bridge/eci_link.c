/* The two blocks of memory ECI and the Delta machine pass text through.

   A link is a pair of buffers: one ECI fills with the text to speak and the
   machine reads a line at a time, one the machine writes its answers into
   and ECI takes them out of. Three of them exist — the main one, one for
   errors, and one for the concatenative synthesizer's output — and each is
   handed to the logical file table as a physical file, so the machine sees a
   stream and never knows it is talking to memory.

   The dialog class below is the same idea for the Delta debugger's window.
   Nothing in the library ever asks for it, because the only thing that would
   is showDialogs, which always answers no; it is here because the block that
   holds it is laid out for both. */

#include <stdlib.h>

#include "delta.h"
#include "eci_io.h"
#include "eci_link.h"

EciLink *eciLinkNew(void)
{
    EciLink *l = malloc(sizeof *l);

    if (l == 0)
        return 0;
    l->in = dynaBufNew(0);
    if (l->in == 0) {
        free(l);
        return 0;
    }
    l->out = dynaBufNew(0);
    if (l->out == 0) {
        dynaBufDelete(l->in);
        free(l);
        return 0;
    }
    return l;
}

void *eciLinkDelete(EciLink *l)
{
    dynaBufDelete(l->in);
    dynaBufDelete(l->out);
    free(l);
    return 0;
}

/* Give the link something to say. The cursor goes back to the start so the
   machine reads what was just put there rather than what follows it. */
int32_t eciLinkDataFromECI(EciLink *l, const char *text)
{
    if (l == 0)
        return 1;
    if (text == 0)
        return 1;
    dynaBufReset(l->in);
    if (!dynaBufAddString(l->in, text, 0))
        return 0;
    dynaBufMoveAbs(l->in, 0);
    return 1;
}

/* Take back what the machine said, up to as much as the caller has room
   for, and drop that much from the front. */
int32_t eciLinkDataToECI(EciLink *l, char *out, int32_t max, int32_t *n)
{
    if (l == 0 || l->out == 0)
        return 0;
    *n = (int32_t)dynaBufLength(l->out);
    if (*n > max)
        *n = max;
    dynaBufExtract(l->out, 0, out, (uint32_t)*n);
    dynaBufMoveAbs(l->out, 0);
    dynaBufDeleteChars(l->out, (uint32_t)*n);
    return 1;
}

/* ---- the link as a physical file ---------------------------------------- */

/* The same as the one in eci_logio.c, and the original has it twice too. */
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

static int eciLinkFileOpen(delta_state *d, PhysicalFile *p, int mode)
{
    EciLink *l = (EciLink *)p->handle;

    p->d = d;
    if (l != 0) {
        if (mode == 0)
            dynaBufReset(l->in);
        else if (mode > 0 && mode <= 2)
            dynaBufReset(l->out);
    }
    return l != 0;
}

/* One line of what ECI put there, and then that line is taken off the front
   so the next read starts where this one stopped. Running out of text ends
   the line and empties the buffer. */
static int eciLinkFileRead(PhysicalFile *p, DynaBuf *b, const char *prompt)
{
    EciLink *l = (EciLink *)p->handle;
    char c;

    (void)prompt;
    if (l == 0)
        return 0;
    if (dynaBufCurrentChar(l->in, 0) == 0)
        return 0;

    for (;;) {
        c = dynaBufCurrentChar(l->in, 1);
        if (c == '\n' || c == 0)
            break;
        if (!dynaBufAddChar(b, c, 0))
            return 0;
    }

    addSpace(b);
    if (!dynaBufAddChar(b, '\n', 0))
        return 0;

    if (c == 0) {
        dynaBufReset(l->in);
    } else {
        uint32_t taken = dynaBufMoveRel(l->in, 0);

        dynaBufMoveAbs(l->in, 0);
        dynaBufDeleteChars(l->in, taken);
    }
    return 1;
}

static int eciLinkFileWrite(PhysicalFile *p, const char *s, int flush)
{
    EciLink *l;

    (void)flush;
    if (*s == 0)
        return 1;
    l = (EciLink *)p->handle;
    if (l == 0)
        return 0;
    return dynaBufAddString(l->out, s, 0) ? 1 : 0;
}

static int eciLinkFileEof(PhysicalFile *p)
{
    EciLink *l = (EciLink *)p->handle;

    if (l == 0)
        return 1;
    return dynaBufCurrentChar(l->in, 0) == 0;
}

static int eciLinkFileClose(PhysicalFile *p)
{
    (void)p;
    return 1;
}

/* ---- the debugger's window as a physical file --------------------------- */

static int dialogFileOpen(delta_state *d, PhysicalFile *p, int mode)
{
    (void)mode;
    p->d = d;
    p->handle = dynaBufNew(0);
    return p->handle != 0;
}

static int dialogFileRead(PhysicalFile *p, DynaBuf *b, const char *prompt)
{
    (void)prompt;
    if (p->handle == 0)
        return 0;
    dynaBufAddChar(b, '\n', 0);
    return 1;
}

/* A whole line is a whole message: when one ends, it is shown and the buffer
   starts again. */
static int dialogFileWrite(PhysicalFile *p, const char *s, int flush)
{
    DynaBuf *b = (DynaBuf *)p->handle;

    (void)flush;
    if (b == 0)
        return 0;
    if (!dynaBufAddString(b, s, 0))
        return 0;
    if (dynaBufChar(b, (int32_t)dynaBufLength(b) - 1) == '\n') {
        dynaBufMoveAbs(b, -1);
        dynaBufMoveRel(b, -1);
        dynaBufDeleteChars(b, 1);
        dynaBufReset(b);
    }
    return 1;
}

static int dialogFileEof(PhysicalFile *p)
{
    return p->handle == 0;
}

static int dialogFileClose(PhysicalFile *p)
{
    (void)p;
    return 1;
}

void eciLinkClasses(PhysicalFile *link, PhysicalFile *dialog)
{
    link->d = 0;
    link->name = 0;
    link->handle = 0;
    link->open = eciLinkFileOpen;
    link->read = eciLinkFileRead;
    link->write = eciLinkFileWrite;
    link->eof = eciLinkFileEof;
    link->close = eciLinkFileClose;

    dialog->d = 0;
    dialog->name = 0;
    dialog->handle = 0;
    dialog->open = dialogFileOpen;
    dialog->read = dialogFileRead;
    dialog->write = dialogFileWrite;
    dialog->eof = dialogFileEof;
    dialog->close = dialogFileClose;
}
