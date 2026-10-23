/* Bounds regressions for the actual SSML converter/parser. No TTS simulation. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#include <strings.h>
#endif
#undef THIS
#include "evv_abi.h"
#include "eci_parserxml.h"

extern int32_t Mbcs2Sbcs(char *, char *);
extern int32_t Sbcs2Mbcs(char *, char *);
extern THIS int32_t px_reallocTextBuffer(Parserxml *);
extern THIS int32_t px_reallocTagStack(Parserxml *);
extern THIS int32_t px_reallocAtts(Parserxml *);

void *cpp_new(uint32_t n) { return malloc(n); }
void cpp_delete(void *p) { free(p); }
int ralStrIcmp(int n, const char *a, const char *b) {
#if defined(_WIN32)
    return _strnicmp(a, b, (size_t)n);
#else
    return strncasecmp(a, b, (size_t)n);
#endif
}

static void truncated(void)
{
    size_t page;
    char *memory;
    char out[128];
#if defined(_WIN32)
    SYSTEM_INFO info;
    DWORD old;
    GetSystemInfo(&info);
    page = info.dwPageSize;
    memory = VirtualAlloc(NULL, page * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(memory && VirtualProtect(memory + page, page, PAGE_NOACCESS, &old));
#else
    page = (size_t)sysconf(_SC_PAGESIZE);
    memory = mmap(NULL, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(memory != MAP_FAILED && mprotect(memory + page, page, PROT_NONE) == 0);
#endif
    const unsigned char leads[] = {0xc2, 0xe2, 0xf0};
    for (int size = 2; size <= 4; size++) {
        for (int present = 1; present < size; present++) {
            char *text = memory + page - present - 1;
            text[0] = (char)leads[size - 2];
            for (int i = 1; i < present; i++) text[i] = (char)0x80;
            text[present] = 0;
            memset(out, 0x55, sizeof out);
            assert(Mbcs2Sbcs(text, out) == 0 && out[0] == 0);
        }
    }
    char *empty = memory + page - 1;
    *empty = 0;
    assert(Mbcs2Sbcs(empty, out) == 0);
#if defined(_WIN32)
    VirtualFree(memory, 0, MEM_RELEASE);
#else
    munmap(memory, page * 2);
#endif
}

static char parsed[256];
static void text_handler(void *data, char *text, int32_t n)
{
    (void)data;
    assert(n >= 0 && strlen(parsed) + (size_t)n < sizeof parsed);
    strncat(parsed, text, (size_t)n);
}

int main(void)
{
#if defined(_WIN32)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    truncated();
    char narrow[128], original[] = "\xf0\x9f\x93\xb1", roundtrip[128];
    assert(Mbcs2Sbcs(original, narrow) == 20);
    assert(Sbcs2Mbcs(narrow, roundtrip) == 4 && strcmp(roundtrip, original) == 0);
    char reference[] = "&#x1f4f1;";
    assert(Mbcs2Sbcs(reference, narrow) == 20);
    assert(Sbcs2Mbcs(narrow, roundtrip) == 4 && strcmp(roundtrip, original) == 0);

    Parserxml *p = CreateParserXML();
    assert(p);
    px_setTextHandler(p, text_handler);
    assert(!px_parseText(p, "hello", 1, 0, 1));
    assert(!px_parseText(p, "hello", -1, 0, 1));
    assert(!px_parseText(p, "hello", INT32_MAX, 0, 1));
    assert(!px_parseText(p, NULL, 0, 0, 1));
    char document[] = "<speak>hola &#x1f4f1;</speak>";
    assert(px_parseText(p, document, (int32_t)strlen(document), 0, 1));
    assert(strcmp(parsed, "hola \xf0\x9f\x93\xb1") == 0);

    int32_t saved = p->textRoom;
    char *buffer = p->textBuffer;
    p->textRoom = INT32_MAX;
    assert(!px_reallocTextBuffer(p) && p->textBuffer == buffer);
    p->textRoom = saved;
    saved = p->tagSlots;
    p->tagSlots = INT32_MAX;
    assert(!px_reallocTagStack(p));
    p->tagSlots = saved;
    saved = p->attsSlots;
    p->attsSlots = INT32_MAX;
    assert(!px_reallocAtts(p));
    p->attsSlots = saved;
    DeleteParserXML(p);
    puts("PASS: UTF-8 guard pages, four-byte references, SSML lengths and growth bounds");
    return 0;
}
