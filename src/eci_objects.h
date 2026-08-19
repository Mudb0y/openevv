/* The objects everything above the Delta machine is built out of, and the one
   place their shape is written down.
 *
 * Each of these used to be described again in every file that touched it,
 * with the padding between the fields that file cared about worked out by
 * hand. That held only so long as a pointer was four bytes: the moment one
 * grew, six descriptions of the same object stopped agreeing with each other
 * and the engine wrote its queue over its own vtable.
 *
 * What is here is the shape of each object and nothing else. The table of
 * virtual functions is named but left incomplete, and whichever file
 * implements the object says what is in it; that is where the signatures
 * belong, and it keeps the calls where the code that makes them is.
 */

#ifndef ECI_OBJECTS_H
#define ECI_OBJECTS_H

#include <stdint.h>

struct ETIThreadVtbl;
struct ETImqThreadVtbl;
struct MessageVtbl;
struct ETIqueueVtbl;
struct QueueVtbl;

/* A thread: four semaphores and what it is doing. */
typedef struct ETIThread {
    const struct ETIThreadVtbl *vt;
    void   *done;          /* the body has come back */
    void   *may_finish;    /* and may now stop */
    void   *gate;          /* one start at a time */
    void   *finished;
    int32_t task;
    int32_t status;
    int32_t asked_to_stop;
} ETIThread;

/* One message, and how many hold it. */
typedef struct ETImessage {
    const struct MessageVtbl *vt;
    uint32_t type;
    int32_t  result;       /* one once the thread has run it */
    int32_t  refs;
    int32_t  is_send;      /* someone is waiting for the answer */
    uint8_t  lock[0x0c];
} ETImessage;

/* A ring of pointers. */
typedef struct ETIqueue {
    const struct ETIqueueVtbl *vt;
    void  **array;
    uint32_t capacity;
    uint32_t head;
    uint32_t tail;
} ETIqueue;

/* One of those with the waiting and the locking around it. */
typedef struct ETImessageQueue {
    const struct QueueVtbl *vt;
    ETIqueue queue;
    uint8_t  lock[0x0c];
    uint8_t  ready[0x0c];      /* something is waiting to be run */
    int32_t  suspended;
    uint8_t  done[0x0c];       /* the last send has been answered */
    uint8_t  send_lock[0x0c];
} ETImessageQueue;

/* A thread with a queue in it. The ETIThread it derives from is spelled out
   rather than embedded, because the subclass has a gate and a stop flag of
   its own and the two sets of names would collide. */
typedef struct ETImessageQueueThread {
    const struct ETImqThreadVtbl *vt;
    void   *th_done;
    void   *th_may_finish;
    void   *th_gate;
    void   *th_finished;
    int32_t th_task;
    int32_t th_status;
    int32_t th_asked_to_stop;
    ETImessageQueue queue;
    uint8_t turn[0x0c];        /* one round of the loop is over */
    uint8_t gate[0x0c];
    int32_t asked_to_stop;
} ETImessageQueueThread;

/* What the application is called back through. */
typedef int32_t (*Callback)(void *inst, int32_t a, int32_t b, void *param);

/* The queue the other way: what the engine has to say to whoever called it,
   and the callback it says it through. */
typedef struct ETIappMessageQueue {
    ETImessageQueue base;
    Callback cb;
    void    *cb_param;
    void    *cb_inst;
    int32_t  posted;
    int32_t  seen;
    int32_t  stopping;
    ETImessage *held;        /* one the callback asked to defer */
    void    *win;
    int16_t  post_flag;
} ETIappMessageQueue;

#endif
