/* What a machine has to give the engine.
 *
 * Everything above this line is portable: the synthesiser, the rules, the
 * text handling, the queues. Everything below it is one file per target.
 * The list is deliberately short -- a semaphore, an event, a thread, a
 * clock -- because the shortest list is the one that ports.
 *
 * It is short because the engine already had a porting layer of its own,
 * the ral prefix, and that layer turns out to need only these twelve
 * things underneath it. The rest of it is ordinary code that happens to
 * have been written on Windows.
 *
 * Rules a target may rely on. Waits are in milliseconds and EVV_FOREVER
 * means no limit. Nothing here is required to be recursive. Nothing here
 * may be called before evv_port_start.
 */

#ifndef EVV_PORT_H
#define EVV_PORT_H

/* The block every call into the runtime layer takes. Two of its slots carry
   what the call is about: sometimes a number the layer invented, sometimes
   the address of the caller's own object, which is why they have to be wide
   enough for one. Both sides of the call used to describe this separately,
   one with pointers and one with ints, which is the same block only while a
   pointer is four bytes. */
struct ral_req {
    unsigned char pad_00[0x0c];
    void         *a;
    void         *b;
};


/* Waiting without a limit. */
#define EVV_FOREVER  (-1)

/* What a wait answers. */
#define EVV_WAIT_OK       0
#define EVV_WAIT_TIMEOUT  1
#define EVV_WAIT_FAILED   2

typedef struct evv_sem   evv_sem;
typedef struct evv_event evv_event;
typedef struct evv_task  evv_task;

/* Called once before anything else, and once after everything else. A
   target with nothing to set up may leave them empty. */
void evv_port_start(void);
void evv_port_finish(void);

/* A counting semaphore. Created with a count and the most it may reach;
   a most of zero means no limit the target need enforce. */
evv_sem *evv_sem_create(int initial, int most);
void     evv_sem_destroy(evv_sem *s);
int      evv_sem_wait(evv_sem *s, int ms);
int      evv_sem_post(evv_sem *s, int n);

/* An event, which stays signalled until it is unsignalled. Pulsing means
   letting whoever is waiting through without leaving it signalled. */
evv_event *evv_event_create(int signalled);
void       evv_event_destroy(evv_event *e);
int        evv_event_wait(evv_event *e, int ms);
void       evv_event_signal(evv_event *e);
void       evv_event_unsignal(evv_event *e);
void       evv_event_pulse(evv_event *e);

/* A thread. The engine never joins one; it signals it and lets it end. */
evv_task *evv_task_start(void (*entry)(void *), void *arg, int stack_bytes);
void      evv_task_stop(evv_task *t);
int       evv_task_priority_get(evv_task *t, int *out);
int       evv_task_priority_set(evv_task *t, int priority);

/* Which thread is asking. Any value will do so long as it differs between
   threads and is stable within one. */
unsigned evv_task_self(void);

/* Giving up the processor, and telling the time. The clock need only count
   milliseconds forward from some fixed moment. */
void     evv_sleep_ms(int ms);
unsigned evv_ticks_ms(void);

#endif
