/* Thousands of tasks that return immediately, with real OS scheduling. */
#include <assert.h>
#include <stdio.h>
#include "evv_port.h"

static evv_sem *finished;
/* The port normally calls the rule-frame cleanup after the entry returns.
   This harness creates no rule frames and uses that point to count exits. */
void evv_frame_done(void) { assert(evv_sem_post(finished, 1) == EVV_WAIT_OK); }
static void immediate(void *arg) { (void)arg; }

int main(void)
{
    evv_port_start();
    finished = evv_sem_create(0, 16);
    assert(finished);
    assert(evv_task_start(NULL, NULL, 0) == NULL);
    for (int batch = 0; batch < 625; batch++) {
        for (int i = 0; i < 16; i++)
            assert(evv_task_start(immediate, NULL, 65536));
        for (int i = 0; i < 16; i++)
            assert(evv_sem_wait(finished, 10000) == EVV_WAIT_OK);
    }
    evv_sem_destroy(finished);
    evv_port_finish();
    puts("PASS: 10000 immediately returning tasks");
    return 0;
}
