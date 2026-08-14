#ifndef DELTA_H
#define DELTA_H

#include <stdint.h>

/* The Delta machine's working state: one allocation of 0x1088 bytes.
 *
 * Named fields are ones a decoded primitive touches. As with the synthesizer,
 * the pad arrays are sized by the distance between offsets we do know, so
 * naming more of them later cannot move anything already placed, and the
 * offsetof assertions in delta.c hold every field where it belongs.
 */

#define DELTA_STATE_BYTES 0x1088

/* A pointer register. The rules work through two of them, a left and a right,
   and the generated code loads them a great deal more often than anything
   else it does. */
typedef struct {
    int32_t value;         /* +0x00, copied out of the token the rule names */
    int32_t unknown_04;    /* +0x04 */
    int32_t unknown_08;    /* +0x08, cleared on every load */
    int8_t  loaded;        /* +0x0c, set on every load */
    int8_t  pad_0d[3];
} delta_pta;

typedef struct delta_state delta_state;

struct delta_state {
    uint8_t    pad_0000[0x40];
    delta_pta  lpta;              /* 0x0040 */
    delta_pta  rpta;              /* 0x0050 */
    uint8_t    pad_0060[DELTA_STATE_BYTES - 0x60];
};

/* What the rules load their pointer registers from. Only the second word is
   ever read, so the rest is left alone until something reads it. */
typedef struct {
    int32_t unknown_00;
    int32_t value;
} delta_token;

void lpta_loadp(delta_state *d, const delta_token *p);
void lpta_rpta_loadp(delta_state *d, const delta_token *lp,
                     const delta_token *rp);

#endif
