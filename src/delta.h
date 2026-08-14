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
    uint8_t   pad_0000[0xa0];
    uint8_t  *names;         /* 0x00a0, the name stack, eight bytes an entry */
    int8_t    names_depth;   /* 0x00a4 */
    uint8_t   pad_00a5[3];
    int32_t   size_a8;       /* 0x00a8, what an unrecognised record costs */
    int32_t   size_ac;       /* 0x00ac */
    int32_t   size_b0;       /* 0x00b0, a saved scan position */
    int32_t   ca_size;       /* 0x00b4, a context record */
    int32_t   size_b8;       /* 0x00b8 */
    int32_t   boa_size;      /* 0x00bc, a begin-or-alternative marker */
    uint8_t   pad_00c0[0x4f8 - 0xc0];
    uint8_t  *top;           /* 0x04f8 */
    uint8_t  *limit;         /* 0x04fc */
    uint8_t   pad_0500[4];
    void     *block;         /* 0x0504, the allocation the stack lives in */
    uint8_t   pad_0508[4];
    uint8_t  *vbot;          /* 0x050c, how far back an unwind may go */
    uint8_t   pad_0510[8];
    uint8_t  *base;          /* 0x0518 */
    uint8_t   pad_051c[0x40];
} delta_stack;

/* Where the rules keep their variables and the result of the last compare.
   Size not established either. */
typedef struct {
    /* The block opens with the active record stack: a count and 999 slots,
       which is exactly what push_ptr refuses to exceed. */
    int32_t   ptr_count;       /* 0x0000 */
    int32_t   ptr_stack[999];  /* 0x0004 */
    uint8_t   pad_0fa0[4];
    int32_t   active_record;   /* 0x0fa4 */
    int32_t   error_thrown;    /* 0x0fa8 */
    uint8_t   pad_0fac[0x18];
    int32_t   test_tag;        /* 0x0fc4, what the running test is matching */
    uint8_t   pad_0fc8[4];
    uint8_t   scan[8];         /* 0x0fcc, the current scan position */
    int8_t    testing;         /* 0x0fd4, a test is under way */
    uint8_t   pad_0fd5[7];
    uint8_t  *back;            /* 0x0fdc, where an unwind returns to */
    int8_t    compared_equal;  /* 0x0fe0 */
    int8_t    fence_count;     /* 0x0fe1, how many characters are fenced */
    uint8_t   pad_0fe2[0x1174 - 0xfe2];
    int32_t   fence_base;      /* 0x1174 */
    uint8_t   pad_1178[0x40];
} delta_vars;

typedef struct delta_state delta_state;

struct delta_state {
    uint8_t      pad_0000[0x40];
    delta_pta    lpta;            /* 0x0040 */
    delta_pta    rpta;            /* 0x0050 */
    uint8_t      pad_0060[8];
    delta_vars  *vars;            /* 0x0068 */
    delta_stack *stack;           /* 0x006c */
    uint8_t      pad_0070[0x14];
    uint8_t     *fence_chars;     /* 0x0084, fenced character by index */
    uint8_t      pad_0088[4];
    uint8_t     *fence_index;     /* 0x008c, index by fenced character */
    uint8_t      pad_0090[8];
    uint8_t      fence_fill;      /* 0x0098 */
    uint8_t      pad_0099[DELTA_STATE_BYTES - 0x99];
};

/* What the rules load their pointer registers from. Only the second word is
   ever read, so the rest is left alone until something reads it. */
typedef struct {
    int32_t unknown_00;
    int32_t value;
} delta_token;

/* What a comparison is handed: where the value is and what type it is. The
   type codes are negative; anything else indexes the language's statement
   table for a length and the two values are compared as bytes. */
typedef struct {
    void   *ptr;      /* +0x00 */
    int16_t kind;     /* +0x04 */
    int16_t pad_06;
} delta_operand;

#define DK_UBYTE  (-1)
#define DK_SHORT  (-2)
#define DK_LONG   (-3)
#define DK_SHORT2 (-4)
#define DK_SYNC   (-6)

/* The language module's statement table: 64-byte entries, one per statement
   type. Entry+0x04 points at a descriptor and entry+0x24 is a length. The
   runtime is parameterised by this table rather than owning it, and English
   ships about ten entries. */
extern const uint8_t vstmtbl[];
#define VSTMTBL_ENTRY 0x40
#define VSTMTBL_DESC  0x04
#define VSTMTBL_LEN   0x24

/* A node on the spine: the linked structure the rules walk over. Its links
   are tagged pointers, with flags in the low two bits that a reader has to
   mask off. */
typedef struct {
    int32_t flags0;    /* +0x00, bit 1 marks a sync */
    int32_t link;      /* +0x04, bit 0 one statement, bit 1 all nonsequential */
    int32_t flags8;    /* +0x08, bit 1 nonsequential */
    int32_t syncs[8];  /* +0x0c, one per field */
} delta_node;

/* A record pushed on the backtracking stack. */
typedef struct {
    int8_t  kind;
    int8_t  pad_01[3];
    int32_t value;   /* +0x04 */
    int32_t length;  /* +0x08, only a variable length record carries one */
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

void bspush_ca_scan(delta_state *d, int16_t tag);

int  testeq(delta_state *d);
int  testneq(delta_state *d);

void  fence(delta_state *d, int8_t n, const uint8_t *chars);
void *TFLDS(void *p);
void *getDeltaStackVBot(delta_state *d);
void  setDeltaStackVBot(delta_state *d, void *v);
void *popDeltaStackTop(delta_state *d);
int   FENCED(delta_state *d, const int32_t *table, int8_t idx);

int32_t absoluteSyncNumPtr(int32_t p);
void  freeDeltaStackTo(delta_state *d, uint8_t *to);
void  clearDeltaStackBack(delta_state *d);
void  starttest(delta_state *d, int16_t tag);
void  vcompare(delta_state *d, const delta_operand *a, const delta_operand *b);

int16_t STMTYP(int8_t kind);
int  ONESTM(const delta_node *t);
int  ALLNSQ(const delta_node *t);
int  NONSEQ(const delta_node *t);
void SETONESTM(delta_node *t);
void SETALLNSQ(delta_node *t);
void SETNONSEQ(delta_node *t);
void CLRONESTM(delta_node *t);
void CLRALLNSQ(delta_node *t);
void bsclear(delta_state *d);
void *bspop_boa(delta_state *d);
void starttest_e(delta_state *d, int16_t tag);
void starttest_l(delta_state *d, int16_t tag);
void SETFENCE(delta_state *d, int32_t *table, int8_t idx);
void UNSETFENCE(delta_state *d, int32_t *table, int8_t idx);
void addfence(delta_state *d, int8_t idx);
void remfence(delta_state *d, int8_t idx);
int32_t deltaErrorThrown(delta_state *d);
int  emptyDeltaStack(delta_state *d);
void *popDeltaStackFrame(delta_state *d, uint8_t *to);
void vnspush(delta_state *d, const delta_operand *v);
void vadd(delta_state *d, const delta_operand *a, const delta_operand *b);
int32_t VLSYNC(const delta_node *t, int8_t i);
int32_t VRSYNC(delta_state *d, const int32_t *t, int8_t i);

/* Two sixteen-bit halves; resetting one clears the second. */
typedef struct {
    int16_t a;
    int16_t b;
} delta_field;

void reset_field(delta_field *f);
int  push_ptr(delta_state *d, int32_t p);
int  ret_ptr_active_record(delta_state *d);
void throwDeltaErrorNow(delta_state *d);
void vnspop(delta_state *d, delta_operand *out);

#endif
