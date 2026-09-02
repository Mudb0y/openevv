/* Two things the tag handlers do to every attribute they read.
 *
 * An XML attribute arrives from the scanner as a pair of strings in a flat
 * list: name, value, name, value, and a null where the list ends. Finding
 * one means walking that list comparing names, and a name in a document may
 * have space around it, so it is trimmed first.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "evv_abi.h"
#include "eci_ssml.h"

extern int ralStrIcmp(int n, const char *a, const char *b);

/* Trim space off both ends, without writing anything: what comes back is a
   pointer past the leading space and a length shortened at both ends. The
   caller is left holding a string that is still terminated where it was, so
   a name with trailing space compares as though the space were there. That
   is the original's own arrangement and getAttributeValue below is its only
   caller, so the two agree with each other. */
char *stripspaces(char *s, int32_t *len)
{
    while (*len != 0 && isspace((unsigned char)s[0])) {
        s++;
        (*len)--;
    }
    while (*len != 0 && isspace((unsigned char)s[*len - 1]))
        (*len)--;
    return s;
}

/* The value of one attribute, or nothing if the list has not got it. The
   comparison ignores case, because SSML's attribute names do.
 *
 * The copy is made only to be trimmed and thrown away; the value handed
 * back points into the scanner's own list. One divergence, and it is the
 * only way this file differs from the original: IBM frees the pointer
 * stripspaces answered with rather than the one malloc gave it, so an
 * attribute name with a space in front of it frees an address the allocator
 * never issued. That is an abort on any allocator that checks, and every
 * case where the original survives it behaves identically to this. */
char *getAttributeValue(const char **atts, const char *name)
{
    int32_t i = 0;

    if (atts == 0)
        return 0;

    while (atts[i] != 0 && atts[i + 1] != 0) {
        char   *copy = malloc(strlen(atts[i]) + 1);
        char   *trimmed;
        int32_t len;

        if (copy == 0)
            return 0;

        strcpy(copy, atts[i]);
        len = (int32_t)strlen(atts[i]);
        trimmed = stripspaces(copy, &len);

        if (ralStrIcmp(0, trimmed, name) == 0) {
            free(copy);
            return (char *)atts[i + 1];
        }

        free(copy);
        i += 2;
    }

    return 0;
}

ALIAS("?stripspaces@@YAPADPADPAH@Z", "stripspaces");
ALIAS("?getAttributeValue@@YAPADPAPBDPBD@Z", "getAttributeValue");
