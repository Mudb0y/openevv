/* What a romanizer instance was told to be, and what went wrong with it.
 *
 * Every parameter the engine sets on a romanizer arrives here through
 * RomInstance, which does nothing but forward. Most of them are numbers this
 * object only remembers; two of them, the text mode and the number mode, are
 * clamped into range first, and the codeset is refused outright unless it is
 * one of the seven the original names.
 *
 * Two things in here are worth knowing because they look like faults and are
 * not. setParam(0) writes the text mode and getParam(0) reads the input type,
 * which is a different field: the input type is written by setInputType, which
 * is private and which only one caller in the original has. That caller is
 * ConverterInterface::addText, so getParam(0) answers whatever the last text
 * handed over was said to be, and isAnnotationsInText -- which reads the same
 * field -- answers the same. Until convtinterface.c was written nothing here
 * called it at all and this said so; the note is kept because the two fields
 * still look like one and are not. And setParam(4) reads its value into a
 * local and does nothing with it at all.
 *
 * Both matter to the manager rather than being curiosities. src/eci_romanizer.c
 * reads a parameter before it writes one and flushes what the romanizer is
 * holding when the two differ, so what getParam answers decides how often the
 * engine flushes. test/romcan.sh is what says the answers are right: it holds
 * our conversation with the romanizer against IBM's, call for call.
 */

#include <string.h>
#include "jprom.h"

/* The parameter numbers. Nought to fifteen are the engine's own; the four
   from 0x3e8 are the ones only a romanizer is told, which the synthesis
   thread sets when it hands over a voice or a sample rate. */
#define P_TEXT_MODE      0x00
#define P_NUMBER_MODE    0x01
#define P_CODESET        0x02
#define P_DICT_ON        0x03
#define P_UNUSED_FOUR    0x04
#define P_WANT_WORD_IX   0x0e
#define P_RETROFLEX      0x0f
#define P_CONCATENATIVE  0x3e8
#define P_WORD_MARKS     0x3e9
#define P_VOICE          0x3ea
#define P_SAMPLE_RATE    0x3eb

/* What a parameter call answers when the value is out of range. */
#define P_REFUSED (-6)

/* The language a Japanese romanizer is for, and the codesets it will take
   with it: Shift-JIS, then the four the engine numbers after it, then
   UCS-2. */
#define CODESET_DEFAULT 0x80000

const char USERINDEXSTR[] = "`ui";

/* ---- making and unmaking -------------------------------------------- */

/* The path is copied because the caller's own is the manager's stack. The
   original allocates one byte more than the string needs and writes the
   terminator itself over the byte strcpy already wrote there. */
RomInstParam *rp_ctor(RomInstParam *p, const char *path)
{
    memset(p, 0, sizeof *p);

    if (path != 0) {
        size_t n = strlen(path);

        p->path = (char *)cpp_new((uint32_t)(n + 1));
        if (p->path != 0) {
            strcpy(p->path, path);
            p->path[n] = 0;
        } else {
            rp_setError(p, ROM_ERR_MEMORY);
        }
    }

    p->codeset = CODESET_DEFAULT;
    p->dictOn = 1;
    p->voice = 1;
    p->rate = 0x2b11;
    return p;
}

void rp_dtor(RomInstParam *p)
{
    if (p->path != 0)
        cpp_delete(p->path);
}

/* ---- the codeset ---------------------------------------------------- */

/* Which codesets a Japanese instance may be asked for. Nought stands for
   whatever the default is and is allowed through. */
static int32_t checkCodeSet(int32_t value)
{
    switch (value) {
    case 0:
    case 0x80000:
    case 0x80100:
    case 0x80200:
    case 0x80300:
    case 0x80400:
    case 0x80800:
        return 1;
    default:
        return 0;
    }
}

/* Answers the one it had. */
static int32_t setCodeSet(RomInstParam *p, int32_t value)
{
    int32_t was = p->codeset;

    if (value == 0)
        value = CODESET_DEFAULT;
    p->codeset = value;
    return was;
}

int32_t rp_getCodeSet(RomInstParam *p)
{
    return p->codeset;
}

/* ---- errors --------------------------------------------------------- */

void rp_setError(RomInstParam *p, int32_t error)
{
    p->lastError = error;
    p->errors |= (uint32_t)error;
}

void rp_clearErrors(RomInstParam *p)
{
    p->lastError = 0;
    p->errors = 0;
}

/* One bit out of the collection, and the last error with it if that is the
   one being taken back. */
void rp_clearOneError(RomInstParam *p, int32_t error)
{
    p->errors &= ~(uint32_t)error;
    if (p->lastError == error)
        p->lastError = 0;
}

uint32_t rp_getErrors(RomInstParam *p)
{
    return p->errors;
}

int32_t rp_getLastError(RomInstParam *p)
{
    return p->lastError;
}

/* The message is appended to whatever the caller's buffer already holds,
   which is what the original does and is why nothing is written for an error
   this does not know. */
void rp_getErrorMessage(RomInstParam *p, char *out)
{
    switch (p->lastError) {
    case ROM_ERR_NONE:
        strcat(out, "No Error");
        break;
    case ROM_ERR_MEMORY:
        strcat(out, "Dynamic memory allocation failed.");
        break;
    case ROM_ERR_NO_DICT:
        strcat(out, "Dictionary file not found.");
        break;
    case ROM_ERR_DICT_BAD:
        strcat(out, "Incompatible dictionary found.");
        break;
    case ROM_ERR_DICT_READ:
        strcat(out, "Error reading dictionary file.");
        break;
    case ROM_ERR_UNICODE:
        strcat(out, "Error Unicode conversion.");
        break;
    case ROM_ERR_UNKNOWN_CHAR:
        strcat(out, "Unknown character found.");
        break;
    default:
        break;
    }
}

/* ---- what the rest of the romanizer asks --------------------------- */

int32_t rp_isDictOn(RomInstParam *p)
{
    return p->dictOn;
}

int32_t rp_isSetWantWordIndex(RomInstParam *p)
{
    return p->wantWordIndex;
}

int32_t rp_isAnnotationsInText(RomInstParam *p)
{
    return p->inputType;
}

/* The one field setParam cannot reach, and the old value back. It is private
   in the original and only ConverterInterface::addText calls it, which is
   what decides whether annotations are looked for in the text. */
int32_t rp_setInputType(RomInstParam *p, int32_t type)
{
    int32_t was = p->inputType;

    p->inputType = type;
    return was;
}

/* ---- reading and writing a parameter -------------------------------- */

int32_t rp_getParam(RomInstParam *p, int32_t which)
{
    switch (which) {
    case P_TEXT_MODE:     return p->inputType;
    case P_CODESET:       return p->codeset;
    case P_DICT_ON:       return p->dictOn;
    case P_WANT_WORD_IX:  return p->wantWordIndex;
    case P_RETROFLEX:     return p->retroflex;
    case P_CONCATENATIVE: return p->concatenative;
    case P_WORD_MARKS:    return p->wordMarks;
    case P_VOICE:         return p->voice;
    case P_SAMPLE_RATE:   return p->rate;
    default:              return -1;
    }
}

/* Answers the value the parameter had, nought for one it does not know, and
   P_REFUSED for a value out of range. The clamping is the original's: a text
   mode outside nought to three becomes nought, and a number mode outside
   nought to one becomes one. */
int32_t rp_setParam(RomInstParam *p, int32_t which, int32_t value)
{
    int32_t rc = 0;
    int32_t was;

    switch (which) {
    case P_TEXT_MODE:
        if (value < 0 || value > 3)
            value = 0;
        was = p->textMode;
        p->textMode = value;
        rc = was;
        break;

    case P_NUMBER_MODE:
        if (value < 0 || value > 1)
            value = 1;
        was = p->numberMode;
        p->numberMode = value;
        rc = was;
        break;

    case P_CODESET:
        if (checkCodeSet(value))
            rc = setCodeSet(p, value);
        break;

    case P_DICT_ON:
        if (value == 1)
            p->dictOn = 1;
        else if (value == 0)
            p->dictOn = 0;
        else
            rc = P_REFUSED;
        break;

    case P_UNUSED_FOUR:
        break;

    case P_WANT_WORD_IX:
        was = p->wantWordIndex;
        p->wantWordIndex = value;
        rc = was;
        break;

    case P_RETROFLEX:
        was = p->retroflex;
        p->retroflex = value;
        rc = was;
        break;

    case P_CONCATENATIVE:
        if (value >= 0 && value <= 1)
            p->concatenative = value;
        else
            rc = P_REFUSED;
        break;

    case P_WORD_MARKS:
        if (value >= 0 && value <= 1)
            p->wordMarks = value;
        else
            rc = P_REFUSED;
        break;

    case P_VOICE:
        p->voice = value;
        break;

    case P_SAMPLE_RATE:
        p->rate = value;
        break;

    default:
        break;
    }
    return rc;
}
