/* What the heteronym filter makes of a sentence, as text.
 *
 * The filter's whole job is to write an annotation where the engine's own
 * test would miss one, so what it writes is the thing to check and the audio
 * is beside the point. This calls the rewriting directly rather than through
 * eciRegisterFilter, so that a wrong answer names itself here instead of
 * being a hash that moved.
 *
 * usage: hetero <cases.txt>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern char *hetero_rewrite(const char *text);
extern void evv_platform_start(void);

int main(int argc, char **argv)
{
    char line[4096];
    FILE *f;

    if (argc != 2) {
        fprintf(stderr, "usage: hetero <cases.txt>\n");
        return 2;
    }
    f = fopen(argv[1], "r");
    if (f == 0) {
        fprintf(stderr, "hetero: cannot read %s\n", argv[1]);
        return 2;
    }

    while (fgets(line, sizeof line, f) != 0) {
        size_t n = strlen(line);
        char *out;

        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = 0;
        if (n == 0 || line[0] == '#')
            continue;
        out = hetero_rewrite(line);
        printf("%s\n", out ? out : "(nothing)");
    }
    fclose(f);
    return 0;
}
