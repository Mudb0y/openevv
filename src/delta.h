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

/* Where the runtime is looking: a node, which field it is following, how far
   past that node in the field's own units, and flags. Bit 0 says the position
   is settled, bit 1 that it needs normalising, bits 2 and 3 ask for it to be
   snapped to the start or the end of a run.

   A rule's two pointer registers are the same sixteen bytes, which is why
   loading one clears its offset and marks it settled. */
typedef struct {
    int32_t node;      /* +0x00 */
    int8_t  field;     /* +0x04 */
    int8_t  pad_05[3];
    int32_t offset;    /* +0x08 */
    uint8_t flags;     /* +0x0c */
    uint8_t pad_0d[3];
} delta_tpos;

typedef delta_tpos delta_pta;

/* The backtracking stack. Its true size is not established: only the fields
   below have been seen, so the tail is however much room the records need. */
typedef struct {
    int32_t       spine_l;     /* 0x0000, the node the spine starts at */
    int32_t       spine_r;     /* 0x0004, and the one it ends at */
    uint8_t       pad_0008[0x5c - 8];
    const int8_t *nsq_fields;  /* 0x005c, which fields decide the flags,
                                  terminated by a negative entry */
    uint8_t       pad_0060[0x9c - 0x60];
    int32_t       unknown_9c;  /* 0x009c, cleared when a loop restarts */
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
    void     *err_jmp;         /* 0x0fac, where a thrown error lands */
    uint8_t   pad_0fb0[0x10];
    int32_t   loop_tag;        /* 0x0fc0, what a forall is iterating */
    int32_t   test_tag;        /* 0x0fc4, what the running test is matching */
    uint8_t   pad_0fc8[4];
    int32_t   scan_ptr;        /* 0x0fcc, where the scan has got to */
    uint8_t   scan_field;      /* 0x0fd0, which field it is walking */
    uint8_t   scan_rev;        /* 0x0fd1, walking right rather than left */
    uint8_t   scan_held;       /* 0x0fd2, the fence check is suspended */
    uint8_t   pad_0fd3;
    int8_t    testing;         /* 0x0fd4, a test is under way */
    uint8_t   pad_0fd5[7];
    uint8_t  *back;            /* 0x0fdc, where an unwind returns to */
    int8_t    compared_equal;  /* 0x0fe0 */
    int8_t    fence_count;     /* 0x0fe1, how many characters are fenced */
    uint8_t   pad_0fe2[0x1124 - 0xfe2];
    int32_t   relink;          /* 0x1124, keep the spine order consistent */
    uint8_t   pad_1128[0x116c - 0x1128];
    const int8_t *nsq_marks;   /* 0x116c, one per fenced field */
    uint8_t   pad_1170[4];
    int32_t   fence_base;      /* 0x1174 */
    uint8_t   pad_1178[0x40];
} delta_vars;

typedef struct delta_state delta_state;

struct delta_state {
    uint8_t      pad_0000[0x40];
    delta_pta    lpta;            /* 0x0040 */
    delta_pta    rpta;            /* 0x0050 */
    uint8_t      pad_0060[4];
    uint8_t     *owner;           /* 0x0064, whoever wants to know the spine
                                     moved; the flag it sets is at 0x1b8 */
    delta_vars  *vars;            /* 0x0068 */
    delta_stack *stack;           /* 0x006c */
    uint8_t      pad_0070[0x14];
    uint8_t     *fence_chars;     /* 0x0084, fenced character by index */
    uint8_t      pad_0088[4];
    uint8_t     *fence_index;     /* 0x008c, index by fenced character */
    uint8_t      pad_0090[4];
    uint8_t     *fence_marks;     /* 0x0094, one per fenced character */
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
    int8_t  flag;     /* +0x06, one byte, copied from the field descriptor */
    int8_t  pad_07;
} delta_operand;

#define DK_UBYTE  (-1)
#define DK_SHORT  (-2)
#define DK_LONG   (-3)
#define DK_SHORT2 (-4)
#define DK_SYNC   (-6)

/* A compiled location: what a rule names when it refers to a variable. A
   negative kind means the value follows inline at +0x04; otherwise the pair
   names a statement type and one of its fields, and -1 for the field means
   the whole record. It is the same eight bytes whether a rule is reading it,
   pushing it or saving it. */
typedef struct {
    int16_t kind;    /* +0x00 */
    int16_t field;   /* +0x02 */
    int32_t value;   /* +0x04 */
} delta_loc;

/* One field of a statement type, as the language declares it: a name, a
   printf format for the debugger, and the table of names its values may
   take. English's phone statement declares name, class, voicing, sonority,
   manner_of_artic, place_of_artic and backness this way. */
typedef struct {
    const char *name;         /* +0x00 */
    const char *format;       /* +0x04 */
    const void *values;       /* +0x08 */
    int32_t     unknown_0c;
    int16_t     unknown_10;
    int16_t     kind;         /* +0x12, the type code a comparison sees */
    int8_t      flag;         /* +0x14 */
    int8_t      pad_15[3];
} delta_fielddesc;

/* The language module's statement table, one 64-byte entry per statement
   type. The runtime is parameterised by this rather than owning it: English
   declares ten types, named char_count, inp, phone, morph, word, inton_phr,
   klatt, syllable, F0 and Ms.

   A statement type doubles as a field index into a spine node, so the same
   number indexes both this table and the node's sync array. */
typedef struct {
    const char            *name;      /* +0x00 */
    const delta_fielddesc *fields;    /* +0x04 */
    void *(*const         *get)(void *);  /* +0x08, one reader per field */
    void (*const          *put)(void *, const void *);
                                      /* +0x0c, one writer per field */
    const uint8_t         *variants;  /* +0x10, null unless the type has any */
    const uint8_t         *deflt;     /* +0x14, what a fresh statement holds */
    uint8_t                pad_18[0x20 - 0x18];
    int32_t                nfields;   /* +0x20, how many the type declares */
    int32_t                length;    /* +0x24, the whole record in bytes */
    int32_t                stride;    /* +0x28, one variant */
    int32_t                varlen;    /* +0x2c, how much of one to copy */
    uint8_t                pad_30[4];
    uint8_t                marks[2];  /* +0x34, the pair the printer brackets
                                         a statement with */
    uint8_t                walkable;  /* +0x36, only Ms sets this */
    uint8_t                pad_37;
    int32_t                unknown_38;
    int32_t                unknown_3c;
} delta_stmt;

extern const delta_stmt vstmtbl[];

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

void reset_field(delta_loc *f);
int  push_ptr(delta_state *d, int32_t p);
int  ret_ptr_active_record(delta_state *d);
void throwDeltaErrorNow(delta_state *d);
void vnspop(delta_state *d, delta_operand *out);
void vpush_var(delta_state *d, const delta_operand *v);
void DELSPINE(delta_state *d, delta_node *t);
int  vscanadv(delta_state *d, int32_t step, int32_t usefence);
void flushDeletedDeltaObjects(delta_state *d);
void SETSPINEL(delta_node *t, int32_t v);
void SETSPINER(delta_state *d, int32_t *t, int32_t v);
void bspush_ca_boa(delta_state *d, int16_t tag);
void bspush_ca_scan_boa(delta_state *d, int16_t tag);
void forceErrorBacktrack(delta_state *d);
void push_ptr_init(delta_state *d, delta_loc *p);
void npush_i(delta_state *d, int32_t x);
void npush_s(delta_state *d, int32_t x);
void vscaninit(delta_state *d);
delta_node *vmovel(delta_node *t, uint8_t f);
int32_t *vmover(delta_state *d, int32_t *t, uint8_t f);
void INSSPINEL(delta_state *d, delta_node *n, delta_node *t);
void INSSPINER(delta_state *d, delta_node *n, delta_node *t);
delta_node *lmost(delta_state *d, int8_t f, delta_node *t);
int32_t *rmost(delta_state *d, int8_t f, int32_t *t);
void vassign(delta_state *d, const delta_operand *dst, const delta_operand *src);
int  npush_fld(delta_state *d, uint8_t st, uint8_t fld);
int32_t *ctxspine(delta_state *d, int32_t *t, uint8_t f, int32_t back);
void vnsqflags(delta_state *d, int32_t *t);
void vinitloc_new(delta_state *d, delta_operand *out, const delta_loc *loc);
void startloop(delta_state *d, int16_t tag);
void save_var(delta_state *d, const delta_loc *loc);
int  testFldeq(delta_state *d, uint8_t st, uint8_t fld, uint8_t val);
void vinitflds(delta_state *d, uint8_t st, void *dst, const void *src);
int  vscanadvOverToken(delta_state *d, int32_t usefence);
int  vscanadvUptoTokenOrMarker(delta_state *d, int32_t target, int32_t usefence);

/* What seqscan is handed and fills in: which way to walk, where to start,
   how far it got, and whether anything along the way was not a lone
   sequential statement. */
typedef struct {
    int8_t  kind;      /* +0x00, one means walk the other way */
    int8_t  pad_01[3];
    int32_t flag;      /* +0x04 */
    int32_t start;     /* +0x08 */
    int32_t cur;       /* +0x0c */
} delta_seqctl;

void seqscan(delta_state *d, delta_seqctl *c);
int  advance_tok(delta_state *d);
int  forall_cont_from(delta_state *d, int16_t tag, int16_t loop,
                      int32_t unused, delta_loc *dst, const delta_loc *src);
void savescptr(delta_state *d, int16_t tag, delta_loc *v);
int  get_parm(delta_state *d, delta_loc *out, delta_loc *loc, int16_t kind);
int  test_synch(delta_state *d, int16_t tag, uint8_t n, const uint8_t *list);
int  test_string_i(delta_state *d, uint8_t st, uint8_t n, const uint8_t *str);
int  test_string_s(delta_state *d, uint8_t st, uint8_t n, const uint8_t *str);
int32_t ctxlook(delta_state *d, int32_t t, uint8_t f, int32_t right);

int vnormalize(delta_state *d, delta_tpos *p);
int vmove_tv(delta_state *d, delta_tpos *p);
int vtstsnc_tv(delta_state *d, delta_tpos *p);
int vtsttmark_tv(delta_state *d, delta_tpos *p, uint8_t back);
int test_ptr(delta_state *d);
void lpta_movel(delta_state *d, uint8_t f);
void lpta_mover(delta_state *d, uint8_t f);
int  lpta_tstmover(delta_state *d, uint8_t f);
int  setscan_l(delta_state *d, uint8_t f);
int  setscan_r(delta_state *d, uint8_t f);
int  setscan_nof_l(delta_state *d, uint8_t f);
int  setscan_nof_r(delta_state *d, uint8_t f);
int vproject(delta_state *d, int32_t t, int32_t left, int32_t right, uint8_t f);

/* Where the runtime tells its owner the spine moved. */
#define DELTA_OWNER_CHANGED 0x1b8

/* Bumped whenever the spine is relinked, so anything holding a position knows
   to look again. */
extern int32_t spine_changed;

#endif
