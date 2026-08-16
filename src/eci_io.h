/* The layer between the Delta machine and ECI: a growable buffer, the
   physical file classes built on it, and the logical file table the machine
   reaches its streams through.

   Delta never names a file. It names a stream, and this decides what that
   stream is: a block of memory ECI fills with the text to speak, another it
   takes the answers out of, a real file, or nothing at all. */

#ifndef ECI_IO_H
#define ECI_IO_H

#include <stdint.h>

/* A run of characters that grows as it is written, with a cursor in it.
   Sixteen bytes, laid out as the original's, because the file classes below
   are handed one by callers that still belong to the original. */
typedef struct {
    char     *base;   /* +0x00 */
    uint32_t  room;   /* +0x04, what was allocated */
    uint32_t  used;   /* +0x08, what is in it, not counting the terminator */
    uint32_t  at;     /* +0x0c, where the cursor is */
} DynaBuf;

DynaBuf *dynaBufNew(uint32_t size);
int      dynaBufDelete(DynaBuf *b);
DynaBuf *dynaBufReset(DynaBuf *b);
int      dynaBufAddChar(DynaBuf *b, char c, int insert);
int      dynaBufAddString(DynaBuf *b, const char *s, int insert);
int      dynaBufAddInt(DynaBuf *b, int32_t v, int insert);
int      dynaBufAddDynaBuf(DynaBuf *b, const DynaBuf *src, int insert);
void     dynaBufDeleteChars(DynaBuf *b, uint32_t n);
uint32_t dynaBufLength(const DynaBuf *b);
uint32_t dynaBufMoveRel(DynaBuf *b, int32_t delta);
uint32_t dynaBufMoveAbs(DynaBuf *b, int32_t pos);
int      dynaBufAtEnd(const DynaBuf *b);
char     dynaBufChar(const DynaBuf *b, int32_t i);
char     dynaBufCurrentChar(DynaBuf *b, int advance);
char    *dynaBufContents(const DynaBuf *b);
char    *dynaBufExtract(DynaBuf *b, int32_t from, char *out, uint32_t max);

#endif
