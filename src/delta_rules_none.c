/* No rules written as C, so every one of them runs as bytecode.
 *
 * The interpreter looks in this table before it runs a rule, and takes what
 * it finds there instead. The table it looks in is either this one, which is
 * empty, or the one tools/delta-decompile.py writes, which holds all 3,377
 * of them. Both speak the same samples; the difference is thirteen megabytes
 * of C and seven minutes of compiler, so the empty one is what an ordinary
 * build links and `make RULES=c' is what asks for the other.
 */

#include "delta_rules_c.h"

const delta_rule_c delta_rule_native[] = {
    { -1, 0 },
};
