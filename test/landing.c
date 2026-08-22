/* A landing place jumped to from a thread that never planted it.
 *
 * The engine backtracks by jumping to a place a rule planted earlier, and the
 * place is named by the address of the rule's frame. The frames are in the
 * arena, which every thread shares, and the machine keeps the one it means to
 * return to in its own state -- so the name travels between threads even
 * though the landing place itself cannot: the table of places is per thread.
 *
 * That is how stopping the engine from another thread used to fault with
 * nothing in the fault to say where it came from. The lookup made a place
 * rather than refusing, the new place was all noughts, and the jump loaded
 * nought as the stack pointer and went to nought.
 *
 * So this checks the two halves. Planting a landing and landing on it, on one
 * thread, still works and still carries the value. Jumping to the same name
 * from a thread that never planted it says so and stops, rather than jumping
 * into nowhere.
 *
 * It expects to be killed: the guard aborts, which is the whole point. The
 * script that runs it is what reads the answer, so the two halves each print
 * before they are tried.
 *
 * usage: landing          answers 0 only if the guard did not fire
 */
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#include "evv_land.h"

/* Sixty-four bytes, the size the machine plants one in, so the name is the
   kind of address a rule would really hand over. */
static unsigned long long buf[8];

static void *fromElsewhere(void *arg)
{
    (void)arg;
    printf("landing: jumping from a thread that never planted it\n");
    fflush(stdout);
    EVV_LAND_JUMP((intptr_t)buf, 1);
    return 0;
}

int main(void)
{
    pthread_t th;

    if (EVV_LAND_SAVE((intptr_t)buf) == 0) {
        printf("landing: planted\n");
        fflush(stdout);
        EVV_LAND_JUMP((intptr_t)buf, 7);
    }
    printf("landing: landed on it, on the thread that planted it\n");
    fflush(stdout);

    if (pthread_create(&th, 0, fromElsewhere, 0) != 0) {
        printf("landing: no second thread, so nothing was checked\n");
        return 1;
    }
    pthread_join(th, 0);

    printf("landing: the jump from elsewhere came back, which it must not\n");
    return 1;
}
