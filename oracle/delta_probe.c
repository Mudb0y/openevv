/* Proves the Delta runtime links and starts, which is what the primitive
   comparisons will be built on. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DELTA_STATE_BYTES 0x1088   /* what delta_new mallocs */

extern int runtime_new(void *);

int main(void)
{
    void *state = calloc(1, DELTA_STATE_BYTES);
    int rc;

    if (state == NULL)
        return 1;

    printf("delta probe: state is %d bytes\n", DELTA_STATE_BYTES);
    rc = runtime_new(state);
    printf("delta probe: runtime_new returned %d\n", rc);

    free(state);
    return 0;
}
