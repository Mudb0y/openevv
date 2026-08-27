#!/usr/bin/env python3
"""What the native build still asks the original for.

Reads nm output over our objects, drops everything the system answers,
and sorts what is left into the shapes it comes in. The count is the
work remaining, so it is worth being exact about which names are really
missing and which are only the C library's."""

import re
import sys

# Answered by the C library, the compiler's own runtime, pthreads or the
# linker. mmap and munmap arrived with the arena, which only the sixty-four
# bit build compiles, and _TLS_MODULE_BASE_ came with its thread-local model.
SYSTEM = re.compile(
    r"^(__|_GLOBAL_OFFSET_TABLE_$|_TLS_MODULE_BASE_$|pthread_|"
    r"std(in|out|err)$|"
    r"(mem|str|stp|f|is|to)[a-z]*$|"
    r"mmap$|munmap$|malloc$|calloc$|realloc$|free$|exit$|atexit$|"
    r"abort$|qsort$|"
    r"printf$|sprintf$|snprintf$|vsnprintf$|vsprintf$|puts$|putchar$|"
    r"getc$|putc$|ungetc$|rewind$|remove$|rename$|setvbuf$|perror$|"
    r"time$|clock$|clock_gettime$|nanosleep$|getenv$|system$|readlink$|"
    r"rand$|srand$|abs$|labs$|atoi$|atof$|pow$|floor$|ceil$|sqrt$|"
    r"longjmp$|setjmp$|_setjmp$|stat$)")


def main(path):
    defined, undefined = set(), {}
    obj = "?"
    for line in open(path):
        line = line.rstrip("\n")
        if line.endswith(":"):
            obj = line[:-1].split("/")[-1]
            continue
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "U" and len(parts) == 2:
            undefined.setdefault(parts[1], set()).add(obj)
        elif len(parts) >= 3 and parts[1] in "TtDdBbRrVvWw":
            defined.add(parts[2])

    missing = {k: v for k, v in undefined.items()
               if k not in defined and not SYSTEM.match(k)}

    rules = sorted(k for k in missing if k.startswith("ibm_"))
    other = sorted(k for k in missing if not k.startswith("ibm_"))
    generated = [k for k in rules if "_ZZ_" in k]

    print("defined by us: %d" % len(defined))
    print("still wanted:  %d" % len(missing))
    print("  the language's own helpers (ibm_): %d, of which %d generated"
          % (len(rules), len(generated)))
    print("  everything else:                   %d" % len(other))
    print()
    for name in other:
        print("    %s" % name)


if __name__ == "__main__":
    main(sys.argv[1])
