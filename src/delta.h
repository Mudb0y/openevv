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

/* The backtracking stack. Its true size is not established: only the fields
   below have been seen, so the tail is however much room the records need. */
typedef struct {
    uint8_t   pad_0000[0xac];
    int32_t   step_ac;       /* 0x00ac */
    int32_t   step_b0;       /* 0x00b0 */
    int32_t   ca_size;       /* 0x00b4, how far a context record moves it */
    int32_t   step_b8;       /* 0x00b8 */
    int32_t   boa_size;      /* 0x00bc, and a begin-or-alternative record */
    uint8_t   pad_00c0[0x4f8 - 0xc0];
    uint8_t  *top;           /* 0x04f8 */
    uint8_t  *limit;         /* 0x04fc */
    uint8_t   pad_0500[0x100];
} delta_stack;

/* Where the rules keep their variables and the result of the last compare.
   Size not established either. */
typedef struct {
    uint8_t   pad_0000[0xfe0];
    int8_t    compared_equal;  /* 0x0fe0 */
    uint8_t   pad_0fe1[0x120];
} delta_vars;

typedef struct delta_state delta_state;

struct delta_state {
    uint8_t      pad_0000[0x40];
    delta_pta    lpta;            /* 0x0040 */
    delta_pta    rpta;            /* 0x0050 */
    uint8_t      pad_0060[8];
    delta_vars  *vars;            /* 0x0068 */
    delta_stack *stack;           /* 0x006c */
    uint8_t      pad_0070[DELTA_STATE_BYTES - 0x70];
};

/* What the rules load their pointer registers from. Only the second word is
   ever read, so the rest is left alone until something reads it. */
typedef struct {
    int32_t unknown_00;
    int32_t value;
} delta_token;

/* A record pushed on the backtracking stack. */
typedef struct {
    int8_t  kind;
    int8_t  pad_01[3];
    int32_t value;
} delta_frame;

void lpta_loadp(delta_state *d, const delta_token *p);
void lpta_loadpn(delta_state *d, const delta_token *p);
void rpta_loadp(delta_state *d, const delta_token *p);
void rpta_loadpn(delta_state *d, const delta_token *p);
void lpta_rpta_loadp(delta_state *d, const delta_token *lp,
                     const delta_token *rp);

void bspush_ca(delta_state *d, int16_t tag);
void bspush_boa(delta_state *d);
void bspush_nboa(delta_state *d);

int  testeq(delta_state *d);
int  testneq(delta_state *d);

#endif
