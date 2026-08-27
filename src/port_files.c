/* Finding a file the engine was told the name of.
 *
 * The original asks Windows to search for it, which looks in the program's
 * own directory, then the current one, then the system's, then everything
 * on PATH. There is no one call for that anywhere else, so it is written
 * out: the name as given first, in case it is already a path, and then
 * each directory on PATH in turn.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "evv_arena.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

/* What the original allows a found name to run to. */
#define PATH_ROOM 0x104

/* How the machine this is built for separates one directory from the next,
   and one path from another. */
#if defined(_WIN32)
#define DIR_SEPARATOR '\\'
#define PATH_SEPARATOR ';'
#else
#define DIR_SEPARATOR '/'
#define PATH_SEPARATOR ':'
#endif

static int readable(const char *path)
{
    FILE *f = fopen(path, "rb");

    if (f == NULL)
        return 0;
    fclose(f);
    return 1;
}

/* Answers one and fills in where it found it, or nought. The buffer is the
   caller's and is only written when the answer is one. */
int32_t fileFindInPath(const char *name, char *out)
{
    const char *path, *from;
    char        tried[PATH_ROOM];
    size_t      len;

    if (name == NULL || *name == 0)
        return 0;

    if (readable(name)) {
        if (out != NULL)
            strcpy(out, name);
        return 1;
    }

    path = getenv("PATH");
    if (path == NULL)
        return 0;

    for (from = path; *from != 0; ) {
        const char *end = strchr(from, PATH_SEPARATOR);

        len = end ? (size_t)(end - from) : strlen(from);
        if (len > 0 && len + 1 + strlen(name) + 1 <= sizeof tried) {
            memcpy(tried, from, len);
            if (tried[len - 1] != DIR_SEPARATOR)
                tried[len++] = DIR_SEPARATOR;
            strcpy(tried + len, name);

            if (readable(tried)) {
                if (out != NULL)
                    strcpy(out, tried);
                return 1;
            }
        }
        if (end == NULL)
            break;
        from = end + 1;
    }

    return 0;
}

/* The original's own name for strdup, which is not in the standard C
   library it was written against either. */
char *dupstr(const char *s)
{
    size_t n;
    char  *p;

    if (s == NULL)
        return NULL;
    n = strlen(s) + 1;
    p = malloc(n);
    if (p != NULL)
        memcpy(p, s, n);
    return p;
}

/* The directory the program itself was loaded from, with its separator still
   on the end.
 *
 * The romanizer manager works this out before asking for a romanizer, and
 * hands it over as where that romanizer's own files are to be found. IBM asks
 * GetModuleHandleA for the running image and GetModuleFileNameA for its path,
 * then walks back to the last separator. There is no such pair anywhere else,
 * so the path comes from the kernel's own answer instead and the walk back is
 * the same.
 *
 * Answers one and writes the directory, or nought and writes nothing. */
int32_t fileModuleDirectory(char *out, int32_t room)
{
    char  path[PATH_ROOM];
    int32_t i;

#if defined(_WIN32)
    unsigned long n = GetModuleFileNameA(GetModuleHandleA(NULL), path,
                                         sizeof path);

    if (n == 0 || n >= sizeof path)
        return 0;
    path[n] = 0;
#else
    ssize_t n = readlink("/proc/self/exe", path, sizeof path - 1);

    if (n <= 0)
        return 0;
    path[n] = 0;
#endif

    for (i = (int32_t)strlen(path) - 1; i >= 0; i--)
        if (path[i] == '\\' || path[i] == '/')
            break;
    if (i < 0 || i + 2 > room)
        return 0;

    memcpy(out, path, (size_t)i + 1);
    out[i + 1] = 0;
    return 1;
}
