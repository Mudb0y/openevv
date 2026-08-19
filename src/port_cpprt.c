/* The three things the original's C++ runtime supplied.
 *
 * The engine is C++ only in the shallowest sense: it makes objects with
 * new, drops them with delete, and puts a trap in the table slot of a
 * method that has no body. Nothing else of that runtime is reached, so
 * these three stand in for the whole of it.
 *
 * The differential build does not use this file; there the names come from
 * the original's own runtime, and they are spelled the way its compiler
 * spelled them, which is why the aliases are here rather than plain
 * definitions.
 */

#include <stdint.h>
#include <stdlib.h>
#include "evv_abi.h"
#include "evv_arena.h"

/* Asking for nothing still has to answer with something of its own, since
   the caller will compare it against nothing to see whether it worked. */
void *cpp_new(uint32_t n)
{
    return malloc(n ? (size_t)n : 1);
}

void cpp_delete(void *p)
{
    free(p);
}

/* Reached only by calling a method that was never written. There is no
   sensible way on from here. */
void purecall(void)
{
    abort();
}

ALIAS("??2@YAPAXI@Z", "cpp_new");
ALIAS("??3@YAXPAX@Z", "cpp_delete");
ALIAS("__purecall", "purecall");
