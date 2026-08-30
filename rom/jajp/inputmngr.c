/* How text reaches the romanizer, and how the marks in it keep their places.
 *
 * A caller does not hand the romanizer one sentence at a time. It hands it
 * whatever it has -- a fragment, a word, a whole paragraph -- and between
 * those calls it may also set a parameter or ask for an index mark, both of
 * which belong at a particular point in the text rather than to the text as a
 * whole. This class is what holds the two apart: text waits in three fields
 * until somebody asks for it, and everything that is not text goes on a queue
 * with a note of how far into the output it belonged.
 *
 * The queue is the engine's own ETIqueue, which src/eci_etiqueue.c already
 * has. IBM reaches four of its six through its vtable, which is what lets a
 * subclass of it override them; the queue made here is a plain ETIqueue and
 * can be nothing else, so the four are called by name instead. The things on
 * it are RomQueueElements: a QElementIndex for a mark,
 * which reads back as the annotation `ui, and a QElementParam for a parameter,
 * which reads back as whatever the caller wrote. Both are asked where they
 * belong by calling the converter's own getOffset through its vtable, which
 * is the one thing here that reaches back up.
 *
 * The record is ours rather than IBM's. Nothing outside this file and
 * rom/jajp/convtinterface.c so much as holds a pointer to an InputManager,
 * and the fields are named accordingly. That IBM's are 0x00 to 0x18 and ours
 * are the same seven in the same order is a check that the reading is right,
 * not a constraint: every displacement in inputmngr.obj is one of 0x04, 0x08,
 * 0x0c, 0x10, 0x14 and 0x18.
 *
 * One thing here is IBM's and is kept. When getText is given new text and has
 * older text still waiting, it puts the new text first and the waiting text
 * after it, which is the wrong way round; the comment at the join says so
 * again where it happens.
 *
 * Held to IBM's answer by test/romprims.sh.
 */

#include <stdint.h>
#include <string.h>
#include "jprom.h"
#include "romanizer.h"

/* How deep the queue starts. It grows on its own when it fills. */
#define IM_QUEUE_ROOM 0x80

/* ---- what goes on the queue ------------------------------------------ */

/* An index mark reads back as the annotation the engine already knows. */
static int32_t qi_getData(RomQueueElement *e, const char **out)
{
    (void)e;
    *out = USERINDEXSTR;
    return (int32_t)strlen(USERINDEXSTR);
}

/* And a parameter reads back as the text the caller wrote. */
static int32_t qp_getData(RomQueueElement *e, const char **out)
{
    QElementParam *p = (QElementParam *)e;

    *out = p->text;
    return p->len;
}

/* The scalar deleting destructors, which is the shape MSVC gives a virtual
   destructor: the flag says whether to give the storage back as well. */
static void *qi_destroy(RomQueueElement *e, int32_t freeIt)
{
    if (freeIt & 1)
        cpp_delete(e);
    return e;
}

static void *qp_destroy(RomQueueElement *e, int32_t freeIt)
{
    QElementParam *p = (QElementParam *)e;

    if (p->text)
        cpp_delete(p->text);
    if (freeIt & 1)
        cpp_delete(e);
    return e;
}

const RomQueueElementVtbl vtbl_qelement_index = { qi_destroy, qi_getData };
const RomQueueElementVtbl vtbl_qelement_param = { qp_destroy, qp_getData };

/* A parameter and where it belonged. The text is copied, because the caller's
   own copy is not promised to outlive the call that handed it over. */
QElementParam *qp_ctor(QElementParam *p, const char *text, int32_t len,
                       int32_t at)
{
    p->base.vt = &vtbl_qelement_param;
    if (text) {
        p->text = cpp_new((uint32_t)len + 1);
        strcpy(p->text, text);
        p->text[len] = 0;
        p->len = len;
    } else {
        p->text = 0;
        p->len  = 0;
    }
    p->base.at   = at;
    p->base.kind = 1;
    return p;
}

/* ---- the manager ----------------------------------------------------- */

/* A queue of its own, and nothing waiting. An allocation that fails is
   reported through the parameter block rather than refused here, which is
   how every other constructor in this romanizer does it. */
InputManager *im_ctor(InputManager *m, RomInstParam *param)
{
    m->param = param;
    m->queue = cpp_new(eq_bytes);
    if (m->queue)
        eq_ctor(m->queue, IM_QUEUE_ROOM);
    if (m->queue == 0)
        rp_setError(m->param, ROM_ERR_MEMORY);

    m->buf  = 0;
    m->text = 0;
    m->len  = 0;
    return m;
}

void im_dtor(InputManager *m)
{
    if (m->buf)
        cpp_delete(m->buf);
    if (m->queue) {
        eq_dtor(m->queue);
        cpp_delete(m->queue);
    }
}

/* Everything forgotten: the queue emptied and the waiting text dropped. */
void im_remove(InputManager *m)
{
    eq_reset(m->queue);
    m->text = 0;
    m->len  = 0;
}

/* Text kept until somebody asks for it. Nothing is copied and nothing is
   looked at; a length of nought is not an error but is not kept either. */
int32_t im_addText(InputManager *m, const char *text, uint32_t len,
                   int32_t codeset)
{
    if (len) {
        m->text    = text;
        m->len     = len;
        m->codeset = codeset;
    }
    return 1;
}

/* What there is to speak, which is the waiting text with the new text joined
 * to it, and how much of it there is.
 *
 * Called with no text at all it hands back what is waiting; called with text
 * and nothing waiting it hands that straight back without a copy. The join is
 * the only case that allocates, and the buffer it allocates is kept for the
 * next one.
 *
 * The answers are IBM's numbers: nought when there is text, one when there is
 * none and the queue is empty too, two when there is none but the queue still
 * has something on it, four when the caller's codeset disagrees with the
 * waiting text's and there is no chance to change it, and minus one when the
 * join could not be allocated. */
int32_t im_getText(InputManager *m, const char **outText, uint32_t *outLen,
                   const char *text, uint32_t len)
{
    int32_t   codeset;
    char     *was;
    int32_t   grew;
    uint32_t  i;

    *outText = len ? text : 0;
    *outLen  = len;

    if (m->len == 0) {
        if (len == 0)
            return eq_isEmpty(m->queue) ? 1 : 2;
        return 0;
    }

    /* Something is waiting, so the codeset in force has to be the one it
       arrived in. Where nothing new has come the engine is told to go back to
       it; where something new has come it is too late to change. */
    codeset = rp_getCodeSet(m->param);
    if (codeset != m->codeset) {
        if (len)
            return 4;
        rp_setParam(m->param, 2, m->codeset);
    }

    if (len == 0) {
        *outText = m->text;
        *outLen  = m->len;
        m->text  = 0;
        m->len   = 0;
        return 0;
    }

    *outLen = m->len + len;

    /* Whether the buffer from last time is long enough is decided by the
       length of the string still in it rather than by how much was allocated,
       which is IBM's and errs towards allocating again. */
    was  = m->buf;
    grew = 0;
    if (m->buf == 0 || strlen(m->buf) < *outLen + 1) {
        m->buf = cpp_new(*outLen + 1);
        grew   = 1;
    }
    if (m->buf == 0) {
        rp_setError(m->param, ROM_ERR_MEMORY);
        return -1;
    }

    /* The new text goes in front and the text that was already waiting goes
       behind it. That is the wrong way round -- what was said first ends up
       last -- and it is IBM's; nothing above here corrects it, and the join
       is only reached when a caller adds text twice without speaking in
       between. */
    if (m->buf != text)
        for (i = 0; i < len; i++)
            m->buf[i] = text[i];
    memcpy(m->buf + len, m->text, m->len);
    m->buf[*outLen] = 0;

    if (was && grew)
        cpp_delete(was);

    *outText = m->buf;
    m->text  = 0;
    m->len   = 0;
    return 0;
}

/* ---- the queue ------------------------------------------------------- */

/* A mark put where the text has got to. */
int32_t im_insertIndex(InputManager *m)
{
    QElementIndex *e = cpp_new(sizeof *e);

    if (e) {
        Converter *owner = m->param->owner;

        e->base.vt   = &vtbl_qelement_index;
        e->base.at   = CI_VT(owner)->getOffset(owner);
        e->base.kind = 0;
    }
    m->element = (RomQueueElement *)e;

    if (m->element == 0) {
        rp_setError(m->param, ROM_ERR_MEMORY);
        return 0;
    }
    eq_push(m->queue, m->element);
    return 1;
}

/* And a parameter, likewise. */
int32_t im_addParam(InputManager *m, const char *text, int32_t len)
{
    QElementParam *e;

    if (text == 0 || len == 0)
        return 1;

    e = cpp_new(sizeof *e);
    if (e) {
        Converter *owner = m->param->owner;

        qp_ctor(e, text, len, CI_VT(owner)->getOffset(owner));
    }
    m->element = (RomQueueElement *)e;

    if (m->element == 0) {
        rp_setError(m->param, ROM_ERR_MEMORY);
        return 0;
    }
    eq_push(m->queue, m->element);
    return 1;
}

int32_t im_hasMoreElement(InputManager *m)
{
    return !eq_isEmpty(m->queue);
}

/* The one at the head, left where it is. It is kept in the manager as well
   as answered, which is what lets removeElement destroy it afterwards. */
RomQueueElement *im_getNextElement(InputManager *m)
{
    if (eq_isEmpty(m->queue))
        return 0;
    eq_peekHead(m->queue, (void **)&m->element);
    return m->element;
}

/* Where the one at the head belonged, or a number no offset can be when
   there is nothing there at all. */
int32_t im_getNextOffset(InputManager *m)
{
    m->element = im_getNextElement(m);
    if (m->element == 0)
        return -100;
    return m->element->at;
}

/* And what it reads back as. */
int32_t im_getNextData(InputManager *m, const char **out)
{
    m->element = im_getNextElement(m);
    if (m->element == 0) {
        *out = 0;
        return 0;
    }
    return m->element->vt->getData(m->element, out);
}

/* The one at the head taken off and destroyed. */
void im_removeElement(InputManager *m)
{
    eq_pop(m->queue, (void **)&m->element);
    if (m->element)
        m->element->vt->destroy(m->element, 1);
}
