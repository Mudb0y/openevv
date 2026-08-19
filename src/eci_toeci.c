/* Where the Delta machine meets ECI.

   Six streams get wired up here. Two of them — the ones the machine would
   have printed its own chatter to — go nowhere. The other four are the
   links: the text to speak goes in through wordsin, the answers come back
   out through sprout, errors through errorout, and the concatenative
   synthesizer's output through consprout. After this the machine is talking
   to ECI without either of them knowing it. */

#include <stdlib.h>
#include <string.h>

#include "delta.h"
#include "eci_io.h"
#include "eci_link.h"
#include "eci_eloqc.h"

/* The Delta debugger's window. The original decides here whether to put its
   own output on screen; nothing in this library ever wants that. */
static int showDialogs(void)
{
    return 0;
}

int32_t ecilink_new(delta_state *d)
{
    if (d == 0 || EVV_AT(void *, d->eloqc) == 0)
        return 0;
    eciLinkClasses(&ELOQC(d)->link_class, &ELOQC(d)->dialog_class);
    return 0;
}

int32_t ecilink_delete(delta_state *d)
{
    (void)d;
    return 0;
}

int32_t eloqc_new(delta_state *d)
{
    if (d == 0)
        return 0;
    d->eloqc = EVV_REF(malloc(sizeof(Eloqc)));
    if (EVV_AT(void *, d->eloqc) == 0)
        return -2;
    memset(EVV_AT(void *, d->eloqc), 0, sizeof(Eloqc));
    ecilink_new(d);
    ELOQC(d)->unknown_98 = -1;
    return 0;
}

void eloqc_delete(delta_state *d)
{
    if (d == 0 || EVV_AT(void *, d->eloqc) == 0)
        return;
    ecilink_delete(d);
    memset(EVV_AT(void *, d->eloqc), 0, sizeof(Eloqc));
    free(EVV_AT(void *, d->eloqc));
    d->eloqc = EVV_REF(0);
}

/* The machine asks this between statements so that a caller can stop it. */
int32_t multitask(delta_state *d)
{
    checkInterrupt(d);
    return 0;
}

void callSetEngsynError(delta_state *d, const void *what)
{
    setEngsynError(d, *(const int16_t *)((const char *)what + 2));
}

static int addLink(delta_state *d, const char *stream, const char *name,
                   EciLink *link, int32_t mode)
{
    int8_t lf = vffind_lf(d, stream);

    if (lf == -1)
        return 0;
    return logicalFileAddPhysical(d, lf, name, &ELOQC(d)->link_class, link,
                                  mode) != 0;
}

int32_t initializeIO(delta_state *d)
{
    Eloqc *e = ELOQC(d);
    int ok = 1;

    if (e->io_done)
        goto mark;

    initDllLink();

    /* The machine's own two output streams. Nothing reads them, so they go
       to the null file unless the debugger's window is up, which it is not. */
    {
        void *cls = showDialogs() ? (void *)&e->dialog_class
                                  : logicalNullClass(d);
        int8_t lf = vffind_lf(d, "cmdout");

        if (lf == -1
            || !logicalFileAddPhysical(d, lf, "Eloquence output", cls, 0, 1)) {
            ok = 0;
            goto after;
        }
        cls = showDialogs() ? (void *)&e->dialog_class : logicalNullClass(d);
        lf = vffind_lf(d, "pgmout");
        if (lf == -1
            || !logicalFileAddPhysical(d, lf, "Eloquence program output",
                                       cls, 0, 1)) {
            ok = 0;
            goto after;
        }
    }

    /* Running out of memory here is not something the original tries to
       recover from. */
    e->cons_link = eciLinkNew();
    if (e->cons_link == 0)
        exit(0);
    e->main_link = eciLinkNew();
    if (e->main_link == 0)
        exit(0);
    e->error_link = eciLinkNew();
    if (e->error_link == 0)
        exit(0);

after:
    if (ok)
        ok = addLink(d, "consprout", "Concatenative ECI Output",
                     e->cons_link, 1);
    if (ok)
        ok = addLink(d, "wordsin", "ECIinput", e->main_link, 0);
    if (ok)
        ok = addLink(d, "sprout", "ECIoutput", e->main_link, 1);
    if (ok)
        ok = addLink(d, "errorout", "ECIoutput", e->error_link, 1);

    if (ok) {
        if (!e->io_done
            && (!logicalFileOpen(d, (void *)"wordsin", 0)
                || !logicalFileOpen(d, (void *)"sprout", 1)
                || !logicalFileOpen(d, (void *)"errorout", 1)
                || !logicalFileOpen(d, (void *)"consprout", 1))) {
            ok = 0;
            goto mark;
        }
        if (EVV_AT(int8_t *, EVV_AT(delta_vars *, d->vars)->nsq_marks) != 0) {
            int8_t i;

            for (i = 0; i < 2; i++)
                ((int8_t *)EVV_AT(int8_t *, EVV_AT(delta_vars *, d->vars)->nsq_marks))[i] = 1;
        }
        EVV_AT(delta_vars *, d->vars)->ctx_both = 0;
        if (EVV_AT(delta_vars *, d->vars)->unknown_1170 == 0)
            ok = 0;
        else
            EVV_AT(delta_vars *, d->vars)->relink = 1;
    }

mark:
    e->io_done = 1;
    return ok ? 0 : 1;
}

int32_t closeIO(delta_state *d)
{
    Eloqc *e = ELOQC(d);

    if (e->main_link != 0)
        e->main_link = eciLinkDelete(e->main_link);
    if (e->cons_link != 0)
        e->cons_link = eciLinkDelete(e->cons_link);
    if (e->error_link != 0)
        e->error_link = eciLinkDelete(e->error_link);
    return 0;
}

/* The same again, and with the original's own slip kept: the last of the
   three clears the main link's slot rather than its own. Nothing calls this
   twice, which is why it has never mattered. */
void eciLinkCleanup(delta_state *d)
{
    Eloqc *e = ELOQC(d);

    if (e->main_link != 0) {
        eciLinkDelete(e->main_link);
        e->main_link = 0;
    }
    if (e->cons_link != 0) {
        eciLinkDelete(e->cons_link);
        e->cons_link = 0;
    }
    if (e->error_link != 0) {
        eciLinkDelete(e->error_link);
        e->main_link = 0;
    }
}
