/* The runtime abstraction layer the engine expects underneath it.

   The original ships this in a separate library: threads, semaphores,
   events, a little string handling, an audio device interface and a trace
   log. None of it is speech, and none of it is what this port is about, so
   it is written afresh on top of evv_port.h rather than transcribed. Only
   enough to let the engine run and hand samples back through its own
   buffer interface.

   The audio device half is deliberately empty. Asked how many devices there
   are, it answers none, which is what sends the engine down the buffer path
   we want. */

#include "evv_port.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include <stdarg.h>
#include "evv_arena.h"

int g_iTTSLogLevel = 9;
int RAL_THREAD_PRIORITY_NORMAL = 0;

void elgTraceLog(int level, const char *fmt, ...)
{
    va_list ap;

    if (fmt == NULL)
        return;
    printf("log[%d]: ", level);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

/* Semaphores, events and mutexes.

   Each call takes a request block rather than a bare handle, with two slots
   that matter: one twelve bytes in and one four bytes after it. Every family
   uses them differently, and each shape below was read off the one caller
   that builds the block.

   Creating a plain semaphore puts the initial count in the first slot and
   takes the new identifier back out of the second. Creating an event puts
   the initial state in the first slot and the caller's own object in the
   second, that object being the identity from then on. Creating a recursive
   mutex puts the caller's object in the first slot and a flag in the second.
   Every operation afterwards, whichever family, names the thing in the first
   slot.

   So identity is sometimes a number this layer invents and sometimes an
   address the caller already had. Both are just keys, and the table below is
   keyed by the value itself either way. */

#define RAL_MAX 1024

/* An entry is a semaphore or an event, and which it is has to be
   remembered because they are destroyed and waited on differently. */
#define RAL_SEM    1
#define RAL_EVENT  2

static uintptr_t ral_key[RAL_MAX];
static void    *ral_obj[RAL_MAX];
static int      ral_kind[RAL_MAX];
static unsigned ral_owner[RAL_MAX];
static int    ral_depth[RAL_MAX];
static uintptr_t ral_next = 1;
static int    ral_trace;

static void ral_init(void) __attribute__((constructor));
static void ral_init(void)
{
    ral_trace = getenv("RAL_TRACE") != NULL;
}

static void ral_say(const char *what, uintptr_t key, int rc)
{
    if (ral_trace)
        fprintf(stderr, "ral: %6lu %-28s key=%08lx rc=%d thread=%lx\n",
                (unsigned long)evv_ticks_ms(), what,
                (unsigned long)key, rc,
                (unsigned long)evv_task_self());
}

static void ral_free(void *h, int kind)
{
    if (kind == RAL_EVENT)
        evv_event_destroy((evv_event *)h);
    else
        evv_sem_destroy((evv_sem *)h);
}

static int ral_slot(uintptr_t key)
{
    int i;

    for (i = 0; i < RAL_MAX; i++)
        if (ral_key[i] == key)
            return i;
    return -1;
}

static int ral_bind(uintptr_t key, void *h, int kind)
{
    int i;

    if (h == NULL)
        return 10041;

    i = ral_slot(key);
    if (i < 0)
        for (i = 0; i < RAL_MAX; i++)
            if (ral_obj[i] == NULL)
                break;
    if (i >= RAL_MAX) {
        ral_free(h, kind);
        return 10041;
    }

    if (ral_obj[i] != NULL)
        ral_free(ral_obj[i], ral_kind[i]);

    ral_key[i] = key;
    ral_obj[i] = h;
    ral_kind[i] = kind;
    ral_owner[i] = 0;
    ral_depth[i] = 0;
    return 0;
}

static void *ral_find(struct ral_req *r, int *slot)
{
    int i;

    if (r == NULL)
        return NULL;
    i = ral_slot((uintptr_t)r->a);
    if (i < 0)
        return NULL;
    if (slot != NULL)
        *slot = i;
    return ral_obj[i];
}

/* The plain semaphores: count in the second slot, identifier back in the
   first. The other families here read the first slot and write the second,
   and this one is the other way round -- ETIThread hands the initial count
   in the slot everything else uses for a timeout, and then takes the thing
   it will name the semaphore by out of the slot it passed in empty. */
static int ral_sem_create(struct ral_req *r, int max)
{
    uintptr_t key;
    int rc;

    if (r == NULL)
        return 10041;

    key = ++ral_next;
    rc = ral_bind(key, evv_sem_create(r->b ? 1 : 0, max), RAL_SEM);
    if (rc == 0)
        r->a = (void *)key;
    ral_say("ralSemaphoreCreate", key, rc);
    return rc;
}

int ralBinarySemaphoreCreate(struct ral_req *r)
{
    return ral_sem_create(r, 1);
}

int ralCountingSemaphoreCreate(struct ral_req *r)
{
    return ral_sem_create(r, 0x7fffffff);
}

/* The event: state in, the caller's own object as the identity. */
int ralEventCreate(struct ral_req *r)
{
    int rc;

    if (r == NULL)
        return 10041;
    rc = ral_bind((uintptr_t)r->b, evv_event_create(r->a ? 1 : 0),
                  RAL_EVENT);
    ral_say("ralEventCreate", (uintptr_t)r->b, rc);
    return rc;
}

/* The recursive mutex: the caller's own object, and a flag this layer has
   no use for since it keeps the depth itself. */
int ralRecMutexSemaphoreCreate(struct ral_req *r)
{
    int rc;

    if (r == NULL)
        return 10041;
    rc = ral_bind((uintptr_t)r->a, evv_sem_create(1, 1), RAL_SEM);
    ral_say("ralRecMutexSemaphoreCreate", (uintptr_t)r->a, rc);
    return rc;
}

static int ral_drop(struct ral_req *r, const char *what)
{
    int i = -1;
    void *h = ral_find(r, &i);

    if (h == NULL) {
        ral_say(what, r ? (uintptr_t)r->a : 0, 10041);
        return 10041;
    }
    ral_free(h, ral_kind[i]);
    ral_key[i] = 0;
    ral_obj[i] = NULL;
    ral_kind[i] = 0;
    ral_say(what, (uintptr_t)r->a, 0);
    return 0;
}

int ralBinarySemaphoreDelete(struct ral_req *r)
{
    return ral_drop(r, "ralBinarySemaphoreDelete");
}

int ralCountingSemaphoreDelete(struct ral_req *r)
{
    return ral_drop(r, "ralCountingSemaphoreDelete");
}

int ralRecMutexSemaphoreDelete(struct ral_req *r)
{
    return ral_drop(r, "ralRecMutexSemaphoreDelete");
}

int ralEventDelete(struct ral_req *r)
{
    return ral_drop(r, "ralEventDelete");
}

static int ral_post(struct ral_req *r, const char *what)
{
    void *h = ral_find(r, NULL);

    ral_say(what, r ? (uintptr_t)r->a : 0, h == NULL ? 10041 : 0);
    if (h == NULL)
        return 10041;
    evv_sem_post((evv_sem *)h, 1);
    return 0;
}

int ralBinarySemaphoreSignal(struct ral_req *r)
{
    return ral_post(r, "ralBinarySemaphoreSignal");
}

int ralCountingSemaphoreSignal(struct ral_req *r)
{
    return ral_post(r, "ralCountingSemaphoreSignal");
}

/* A waiter does not know whether it holds a semaphore or an event, so ask
   the table. */
static int ral_wait_any(void *h, int ms)
{
    int i;

    for (i = 0; i < RAL_MAX; i++)
        if (ral_obj[i] == h)
            return ral_kind[i] == RAL_EVENT
                   ? evv_event_wait((evv_event *)h, ms)
                   : evv_sem_wait((evv_sem *)h, ms);
    return EVV_WAIT_FAILED;
}

/* Two answers, not one. Nearly everything here reports the way a C function
   reports an error, zero meaning nothing went wrong. Waiting on a semaphore
   does not: its callers compare against 0x2739, so that value is what "you
   have it" looks like and anything else is a refusal. Waiting on an event is
   back to zero again. Each of these was taken from the caller. */
#define RAL_GOT 0x2739

static int ral_wait(struct ral_req *r, const char *what, int got, int lost)
{
    void *h = ral_find(r, NULL);
    int w;

    ral_say(what, r ? (uintptr_t)r->a : 0, -1);
    if (h == NULL)
        return lost;
    w = ral_wait_any(h, EVV_FOREVER);
    ral_say(what, (uintptr_t)r->a, w == EVV_WAIT_OK ? got : lost);
    return w == EVV_WAIT_OK ? got : lost;
}

int ralBinarySemaphoreTest(struct ral_req *r)
{
    return ral_wait(r, "ralBinarySemaphoreTest", RAL_GOT, 0);
}

int ralCountingSemaphoreTest(struct ral_req *r)
{
    return ral_wait(r, "ralCountingSemaphoreTest", RAL_GOT, 0);
}

int ralEventWait(struct ral_req *r)
{
    return ral_wait(r, "ralEventWait", 0, 10041);
}

/* The recursive one is not a Windows mutex: the engine hands it back from
   whichever thread has finished with it, which Windows refuses. It is a
   plain semaphore with the owner and the depth kept alongside. */
int ralRecMutexSemaphoreTest(struct ral_req *r)
{
    int i = -1;
    void *h = ral_find(r, &i);
    unsigned me = evv_task_self();

    ral_say("ralRecMutexSemaphoreTest", r ? (uintptr_t)r->a : 0, h == NULL ? 0 : -1);
    if (h == NULL)
        return 0;

    if (ral_depth[i] > 0 && ral_owner[i] == me) {
        ral_depth[i]++;
        return RAL_GOT;
    }

    if (evv_sem_wait((evv_sem *)h, EVV_FOREVER) != EVV_WAIT_OK)
        return 0;

    ral_owner[i] = me;
    ral_depth[i] = 1;
    return RAL_GOT;
}

int ralRecMutexSemaphoreSignal(struct ral_req *r)
{
    int i = -1;
    void *h = ral_find(r, &i);

    ral_say("ralRecMutexSemaphoreSignal", r ? (uintptr_t)r->a : 0, h == NULL ? 10041 : 0);
    if (h == NULL)
        return 10041;

    if (ral_depth[i] > 0)
        ral_depth[i]--;
    if (ral_depth[i] == 0) {
        ral_owner[i] = 0;
        evv_sem_post((evv_sem *)h, 1);
    }
    return 0;
}

static int ral_event_set(struct ral_req *r, int how, const char *what)
{
    void *h = ral_find(r, NULL);

    ral_say(what, r ? (uintptr_t)r->a : 0, h == NULL ? 10041 : 0);
    if (h == NULL)
        return 10041;
    if (how > 0)
        evv_event_signal((evv_event *)h);
    else if (how < 0)
        evv_event_unsignal((evv_event *)h);
    else
        evv_event_pulse((evv_event *)h);
    return 0;
}

int ralEventSignal(struct ral_req *r)
{
    return ral_event_set(r, 1, "ralEventSignal");
}

int ralEventUnSignal(struct ral_req *r)
{
    return ral_event_set(r, -1, "ralEventUnSignal");
}

int ralEventPulse(struct ral_req *r)
{
    return ral_event_set(r, 0, "ralEventPulse");
}

/* The task descriptor, read off the one caller that builds it: the entry
   point sits sixteen bytes in, its argument four bytes after that, and the
   handle goes back four bytes before them. */
struct ral_task {
    unsigned char pad_00[0x0c];
    void         *handle;
    void       *(*entry)(void *);
    void         *arg;
    int           priority;
    int           stack;
    char          name[8];
};

struct ral_start {
    void *(*entry)(void *);
    void *arg;
};

static void ral_trampoline(void *p)
{
    struct ral_start *s = p;
    void *(*entry)(void *) = s->entry;
    void *arg = s->arg;

    free(s);
    entry(arg);
}

int ralTaskCreate(struct ral_task *t)
{
    struct ral_start *s;
    evv_task *h;

    if (t == NULL || t->entry == NULL)
        return 10041;

    s = malloc(sizeof(*s));
    if (s == NULL)
        return 10041;
    s->entry = t->entry;
    s->arg = t->arg;

    h = evv_task_start(ral_trampoline, s, t->stack > 0 ? t->stack : 0x10000);
    if (h == NULL) {
        free(s);
        return 10041;
    }

    t->handle = h;
    return 0;
}

int ralTaskTerminate(void *h)
{
    (void)h;
    return 0;
}

int ralTaskDelay(int ms)
{
    evv_sleep_ms(ms);
    return 0;
}

int ralTaskPriorityGet(void *h, int *out)
{
    (void)h;
    if (out != NULL)
        *out = 0;
    return 0;
}

int ralTaskPrioritySet(void *h, int p)
{
    (void)h;
    (void)p;
    return 0;
}

int ralStrCpy(int n, char *dst, const char *src)
{
    if (dst == NULL || src == NULL)
        return 10041;
    (void)n;
    strncpy(dst, src, 7);
    dst[7] = 0;
    return 0;
}

/* Written out rather than taken from the C library, because the name for
   it differs between every one of the targets this has to reach and none
   of them is in the standard. */
static int icmp(const char *a, const char *b, int n, int limited)
{
    int i;

    for (i = 0; !limited || i < n; i++) {
        int x = tolower((unsigned char)a[i]);
        int y = tolower((unsigned char)b[i]);

        if (x != y)
            return x < y ? -1 : 1;
        if (x == 0)
            return 0;
    }
    return 0;
}

/* Three arguments, not two: a length first, then the two strings, which is
   how the sound format table looks a format up by name. Zero there means
   the whole string. */
int ralStrIcmp(int n, const char *a, const char *b)
{
    if (a == NULL || b == NULL)
        return a == b ? 0 : 1;
    return icmp(a, b, n, n > 0);
}

/* The counted form, under its own name. IBM's own RAL has both, and Japanese's
   readable-Japanese converter is the only caller of this one: it compares a
   phone name against a table of five-byte entries with nought for the length,
   which is what ralStrIcmp already takes nought to mean. The same code under
   the second name, as it is in the original. */
int ralStrNicmp(int n, const char *a, const char *b)
{
    return ralStrIcmp(n, a, b);
}

/* The audio device layer.

   Everything here reports the way a C function reports an error, zero
   meaning nothing went wrong, which is the opposite of what the semaphore
   waits do. That was read off ealaudio.obj, which logs an error on any
   non-zero answer.

   It claims one device and accepts everything asked of it without ever
   producing a sound. The engine will not construct an instance at all
   unless it can open an output device, so one has to exist; but the samples
   are taken from it through its own buffer interface instead, so the device
   is only ever opened and closed again.

   The capability block is twenty-four bytes with a mask of the sample rates
   the device will take at offset twenty. The four bits it looks at mean
   eight thousand, eleven thousand and twenty-five, twenty-two thousand and
   fifty, and sixteen thousand, in that order of bit position; all four are
   offered. */
#define EAL_CAPS_RATES 0x14

static void *eal_dev = (void *)&ral_next;

int ealQueryDevCount(int type, int *count)
{
    (void)type;
    if (count != NULL)
        *count = 1;
    ral_say("ealQueryDevCount", 1, 0);
    return 0;
}

int ealQueryDevCaps(int dev, int type, int *bits, unsigned char *caps)
{
    (void)dev;
    (void)type;

    if (bits != NULL)
        *bits = 16;
    if (caps != NULL) {
        unsigned rates = 0x1 | 0x10 | 0x100 | 0x10000;

        memset(caps, 0, 0x18);
        memcpy(caps + EAL_CAPS_RATES, &rates, sizeof(rates));
    }
    ral_say("ealQueryDevCaps", dev, 0);
    return 0;
}

int ealDevCreate(int dev, int type, void *format, int flags, void **out)
{
    (void)dev;
    (void)type;
    (void)format;
    (void)flags;

    if (out != NULL)
        *out = eal_dev;
    ral_say("ealDevCreate", dev, 0);
    return 0;
}

static int eal_ok(const char *what, void *dev)
{
    ral_say(what, (int)(size_t)dev, 0);
    return 0;
}

int ealDevDestroy(void *dev)            { return eal_ok("ealDevDestroy", dev); }
int ealDevClose(void *dev)              { return eal_ok("ealDevClose", dev); }
int ealDevStop(void *dev)               { return eal_ok("ealDevStop", dev); }
int ealDevReset(void *dev)              { return eal_ok("ealDevReset", dev); }

int ealDevOpen(void *dev, void *a, void *b)
{
    (void)a;
    (void)b;
    return eal_ok("ealDevOpen", dev);
}

int ealDevStart(void *dev, int a)
{
    (void)a;
    return eal_ok("ealDevStart", dev);
}

int ealDevRegisterBuffer(void *dev, void *buf)
{
    (void)buf;
    return eal_ok("ealDevRegisterBuffer", dev);
}

int ealDevRegisterBufDoneCB(void *dev, void *cb, void *ctx)
{
    (void)cb;
    (void)ctx;
    return eal_ok("ealDevRegisterBufDoneCB", dev);
}

int ealDevRegisterErrorCB(void *dev, void *cb, void *ctx)
{
    (void)cb;
    (void)ctx;
    return eal_ok("ealDevRegisterErrorCB", dev);
}

int ealDevRegisterStateCB(void *dev, void *cb, void *ctx)
{
    (void)cb;
    (void)ctx;
    return eal_ok("ealDevRegisterStateCB", dev);
}

int ealDevUnregisterErrorCB(void *dev, void *x)
{
    (void)x;
    return eal_ok("ealDevUnregisterErrorCB", dev);
}

int ealDevUnregisterStateCB(void *dev, void *x)
{
    (void)x;
    return eal_ok("ealDevUnregisterStateCB", dev);
}

/* Two wide-string helpers the text side wants. The names the C++ compiler
   gave them cannot be written as C identifiers, so the link is told to
   answer those names with these; see the Makefile. */
unsigned uni_strlen(const unsigned short *s)
{
    unsigned n = 0;

    if (s == NULL)
        return 0;
    while (s[n] != 0)
        n++;
    return n;
}

unsigned short *uni_strstr(const unsigned short *h, const unsigned short *n)
{
    unsigned i, j;

    if (h == NULL || n == NULL)
        return NULL;
    if (n[0] == 0)
        return (unsigned short *)h;

    for (i = 0; h[i] != 0; i++) {
        for (j = 0; n[j] != 0 && h[i + j] == n[j]; j++)
            ;
        if (n[j] == 0)
            return (unsigned short *)(h + i);
    }
    return NULL;
}
