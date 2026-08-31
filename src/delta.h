#ifndef DELTA_H
#define DELTA_H

#include <stddef.h>
#include <stdint.h>

#include "evv_arena.h"

/* The Delta machine's working state: one allocation, named fields first and
 * then a cell for every global the language declares.
 *
 * How big it is therefore depends on which language the machine is for --
 * English runs to 0x1088 and German to 0x10b4 -- so the struct below stops
 * where the named fields do, at DG_BASE, and the cells are the rest of an
 * allocation whose size the language states. Nothing takes sizeof this.
 *
 * Named fields are ones a decoded primitive touches. As with the synthesizer,
 * the pad arrays are sized by the distance between offsets we do know, so
 * naming more of them later cannot move anything already placed, and the
 * offsetof assertions in delta.c hold every field where it belongs.
 */

#include "delta_lang.h"

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

/* What a rule saves of the machine before its body runs, so that a backtrack
   into it can put everything back. ventproc fills it in and vretproc
   restores it, field for field. */
typedef struct {
    int32_t    unknown_00;      /* +0x00 */
    uint8_t    pad_04[0x0c];
    uint8_t    tags[8];         /* +0x10, the loop and test tags together */
    uint8_t    testing;         /* +0x18 */
    uint8_t    pad_19[3];
    evv_ref    back;            /* +0x1c */
    evv_ref    top;             /* +0x20 */
    evv_ref    vbot;            /* +0x24 */
    uint8_t    fence_count;     /* +0x28 */
    uint8_t    pad_29[3];
    evv_ref    err_jmp;         /* +0x2c */
    uint8_t    scan[8];         /* +0x30 */
    delta_tpos lpta;            /* +0x38 */
    delta_tpos rpta;            /* +0x48 */
    uint8_t    compared_equal;  /* +0x58 */
    uint8_t    names_depth;     /* +0x59 */
    uint8_t    pad_5a[2];
} delta_actrec;


/* An operand as the machine keeps one, rather than as a caller builds one.
   The difference is the pointer: a delta_operand holds the host's, and one
   of these holds what fits in a value, because these live inside the
   machine's own blocks where every word is four bytes whatever the host is. */
typedef struct {
    evv_ref ptr;     /* +0x00 */
    int16_t kind;    /* +0x04 */
    int8_t  flag;    /* +0x06 */
    int8_t  pad_07;
} delta_operand_at;

/* What a generate statement collects before anything is written out: the
   frame it is to lay down, the moment it covers, and the parameters that go
   with it. Each of the three sets its own bit as it arrives, and nothing is
   generated until all three have; the second cell is the copy vgen_copy
   makes so that the next generate can start filling the first again. */
typedef struct {
    int32_t value;     /* +0x00, the frame */
    uint8_t time;      /* +0x04 */
    uint8_t nparams;   /* +0x05, how many parameter bytes there are */
    uint8_t pad_06[2];
    evv_ref params;    /* +0x08, a dynamic buffer holding them */
    uint8_t flags;     /* +0x0c, 1 the frame, 2 the time, 4 the parameters */
    uint8_t pad_0d[3];
} delta_gencell;

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

/* One segment of the Delta heap. Objects are carved off the top of the block
   downwards, so an allocation returns end minus used; the odd starting value
   of used is the alignment fudge that keeps those addresses eight-aligned.
   The backtracking stack is itself carved out of a segment this way. */
typedef struct delta_seg delta_seg;
struct delta_seg {
    evv_ref    prev;     /* +0x00 */
    int32_t    used;     /* +0x04 */
    int32_t    live;     /* +0x08, objects still allocated in it */
    evv_ref    block;    /* +0x0c */
    evv_ref    end;      /* +0x10, the last byte of the block */
    evv_ref    next;     /* +0x14 */
};

/* Where a rule can rewind the heap to. Ten of them, and the runtime picks
   whichever is free. */
typedef struct {
    evv_ref    pos;      /* +0x00, what the caller was handed back */
    evv_ref    seg;      /* +0x04 */
    int32_t    unused;   /* +0x08, set means this slot is spare */
    int32_t    used;     /* +0x0c */
    int32_t    live;     /* +0x10 */
} delta_mark;

#define DELTA_MARKS 10

/* The backtracking stack. Its true size is not established: only the fields
   below have been seen, so the tail is however much room the records need. */
typedef struct {
    int32_t       spine_l;     /* 0x0000, the node the spine starts at */
    int32_t       spine_r;     /* 0x0004, and the one it ends at */
    delta_seqctl  runs[3];     /* 0x0008, what chkdelnonseq works through */
    uint8_t       pad_0038[0x38 - 0x38];
    evv_ref       mark_fld;    /* 0x0038, which field a mark is writing */
    int16_t       mark_kind;   /* 0x003c */
    uint8_t       mark_flag;   /* 0x003e */
    uint8_t       pad_003f;
    int32_t       del_from;    /* 0x0040, the run a whole delete removes */
    int32_t       del_to;      /* 0x0044 */
    int32_t       del_left;    /* 0x0048, and what a partial one works from */
    int32_t       del_right;   /* 0x004c */
    int8_t        del_field;   /* 0x0050, which field a delete is working in */
    uint8_t       pad_0051[0x5c - 0x51];
    evv_ref       nsq_fields;  /* 0x005c, which fields decide the flags,
                                  terminated by a negative entry */
    uint8_t       pad_0060[0x6c - 0x60];
    /* Where mapsyncs numbers the syncs it walks: a table of one word per
       sync, indexed by the number absoluteSyncNum gives, and the next
       number to hand out. */
    evv_ref       sync_map;    /* 0x006c */
    int32_t       sync_next;   /* 0x0070 */
    uint8_t       pad_0074[0x84 - 0x74];
    /* What the context check was asked, and whether it cleared anything.
       vredoctxt sets the first and reads the second to decide whether to
       say the delta is correct. */
    int32_t       ctxt_arg;    /* 0x0084 */
    int32_t       ctxt_cleared; /* 0x0088 */
    uint8_t       pad_008c[0x94 - 0x8c];
    int32_t       sync_size;   /* 0x0094, how big one sync node is */
    int32_t       unknown_98;  /* 0x0098, cleared when memory is set up */
    int32_t       unknown_9c;  /* 0x009c, cleared when a loop restarts */
    evv_ref   names;         /* 0x00a0, the name stack, eight bytes an entry */
    int8_t    names_depth;   /* 0x00a4 */
    uint8_t   pad_00a5[3];
    int32_t   size_a8;       /* 0x00a8, what an unrecognised record costs */
    int32_t   size_ac;       /* 0x00ac */
    int32_t   size_b0;       /* 0x00b0, a saved scan position */
    int32_t   ca_size;       /* 0x00b4, a context record */
    int32_t   size_b8;       /* 0x00b8 */
    int32_t   boa_size;      /* 0x00bc, a begin-or-alternative marker */
    int32_t   list_fld;      /* 0x00c0, which entry of a stream list the
                                field walk is on */
    int32_t   list_val;      /* 0x00c4, and which field of that entry */
    /* Where the walk over a field's declared value names stands. One of
       these is set up by first_fieldval and stepped by next_fieldval, and
       nothing else in the machine touches them. The prefix is a pointer the
       machine holds in a value, so it has to be somewhere the arena can
       name; that is what the crossing checks. */
    int8_t    vals_stm;      /* 0x00c8, which statement type */
    uint8_t   pad_00c9[3];
    int32_t   vals_fld;      /* 0x00cc, and which of its fields */
    evv_ref   vals_str;      /* 0x00d0, the prefix a name has to start with */
    int32_t   vals_at;       /* 0x00d4, how far the walk has got */
    int32_t   vals_dashes;   /* 0x00d8, set when the prefix is all dashes */
    /* What a value named "undefined" reads back as. The tables spell the
       absent value one way and whoever asks is told another. */
    evv_ref     undefined_text;  /* 0x00dc */
    /* Set when the context check has run through. */
    int32_t   ctxt_done;     /* 0x00e0 */
    /* Six tables vctxtinit takes for the check to work in: four of a word
       per statement type and two of a byte. Nothing else transcribed here
       touches them, so what each is for is not established and they are
       named by nothing better than their order. */
    evv_ref   ctxt_a;        /* 0x00e4 */
    evv_ref   ctxt_b;        /* 0x00e8 */
    evv_ref   ctxt_c;        /* 0x00ec */
    evv_ref   ctxt_d;        /* 0x00f0 */
    evv_ref   ctxt_e;        /* 0x00f4 */
    evv_ref   ctxt_f;        /* 0x00f8 */
    /* What the save layer works in. It is only ever reached through the
       routines at the end of delta_trace.c, which the engine does not use;
       a target that wants to write the machine out and read it back is what
       they are for. */
    evv_ref   saved_spine;   /* 0x00fc, one slot per statement type */
    char      line[0x146 - 0x100];  /* 0x0100, a line being built */
    uint8_t   save_stream;   /* 0x0146, where the script is written */
    uint8_t   pad_0147;
    evv_ref   save_file;     /* 0x0148, and where the bytes go */
    uint8_t   pad_014c[4];
    char      save_name[100];  /* 0x0150, the name last read off it; the
                                  original writes past the end of this
                                  rather than stop at it */
    uint8_t   pad_01b4[0x1bc - 0x1b4];
    /* Where val_expr2 looks when it is not asked to work a position out for
       itself: one entry per statement type, the two ends it should measure
       between. And three caches beside them, twelve bytes to a statement
       type, which durcalc keeps its last answer in. */
    evv_ref   expr_l;          /* 0x01bc */
    evv_ref   expr_r;          /* 0x01c0 */
    evv_ref   dur_cache_a;     /* 0x01c4 */
    evv_ref   dur_cache_b;     /* 0x01c8 */
    evv_ref   dur_cache_c;     /* 0x01cc */
    /* visleft remembers its last fifty answers here. The whole table is
       thrown away whenever the spine is relinked, which is what the stamp
       is for; the counts keep a hot pair from being evicted. */
    int32_t   left_stamp;      /* 0x01d0 */
    int32_t   left_next;       /* 0x01d4 */
    int32_t   left_a[50];      /* 0x01d8 */
    int32_t   left_b[50];      /* 0x02a0 */
    int32_t   left_ans[50];    /* 0x0368 */
    int32_t   left_hits[50];   /* 0x0430 */
    evv_ref   top;           /* 0x04f8 */
    evv_ref   limit;         /* 0x04fc */
    evv_ref    heap_first;   /* 0x0500, where the heap starts */
    evv_ref    seg;          /* 0x0504, the segment the stack lives in */
    evv_ref    heap_cur;     /* 0x0508, where the next object comes from */
    evv_ref    vbot;         /* 0x050c, how far back an unwind may go */
    evv_ref    walk;         /* 0x0510, where a walk over the records has
                                got to; only the two peek calls use it */
    int32_t    seg_size;     /* 0x0514 */
    evv_ref    base;         /* 0x0518 */
    delta_mark marks[DELTA_MARKS];  /* 0x051c */
    int32_t    free_count;   /* 0x05e4, how many spare segments are held */
    evv_ref    free_segs;    /* 0x05e8 */
    /* The block is 0x664 bytes: that is what delta_lib_new asks malloc
       for, so this runs to the end of it. */
    uint8_t    pad_05ec[0x664 - 0x5ec];
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
    evv_ref   err_jmp;         /* 0x0fac, where a thrown error lands */
    uint8_t   return_code;     /* 0x0fb0, what a C helper answered with */
    uint8_t   pad_0fb1[3];
    /* The generate statement being read, whose first byte says which of the
       three parts of a frame this one carries. */
    evv_ref   gen_stmt;        /* 0x0fb4 */
    uint8_t   pad_0fb8[8];
    int32_t   loop_tag;        /* 0x0fc0, what a forall is iterating */
    int32_t   test_tag;        /* 0x0fc4, what the running test is matching */
    uint8_t   pad_0fc8[4];
    int32_t   scan_ptr;        /* 0x0fcc, where the scan has got to */
    uint8_t   scan_field;      /* 0x0fd0, which field it is walking */
    uint8_t   scan_rev;        /* 0x0fd1, walking right rather than left */
    uint8_t   scan_held;       /* 0x0fd2, the fence check is suspended */
    uint8_t   pad_0fd3;
    int8_t    testing;         /* 0x0fd4, a test is under way */
    uint8_t   pad_0fd5[3];
    /* The rule now running, as the language compiled it. Kept as a number
       because that is how a rule names an activation when it asks for a
       variable; only the save layer ever reads through it. */
    int32_t   running;         /* 0x0fd8, saved and restored around a rule */
    evv_ref   back;            /* 0x0fdc, where an unwind returns to */
    int8_t    compared_equal;  /* 0x0fe0 */
    int8_t    fence_count;     /* 0x0fe1, how many characters are fenced */
    uint8_t   pad_0fe2[2];
    /* The frame being collected, and the copy of it that is written out. */
    delta_gencell gen_now;     /* 0x0fe4 */
    delta_gencell gen_done;    /* 0x0ff4 */
    uint8_t   gen_len;         /* 0x1004, how many parameter bytes to take */
    uint8_t   gen_nparams;     /* 0x1005, and how many the statement says */
    /* Scratch the runtime builds a value in when it has to convert one. */
    uint8_t   scratch_b;       /* 0x1006 */
    uint8_t   pad_1007[0x100c - 0x1007];
    int32_t   scratch_l;       /* 0x100c */
    uint8_t   pad_1010[0x1022 - 0x1010];
    int16_t   scratch_s;       /* 0x1022 */
    uint8_t   pad_1024[0x1030 - 0x1024];
    evv_ref   gen_at;          /* 0x1030, where the parameter bytes are read
                                  from, stepped a byte at a time */
    uint8_t   pad_1034[0x106c - 0x1034];
    delta_operand_at gen_src;  /* 0x106c, what a frame is assigned from */
    delta_operand_at gen_dst;  /* 0x1074, and the cell it goes into */
    uint8_t   pad_107c[0x1120 - 0x107c];
    int32_t   ctx_both;        /* 0x1120, look both ways for a context */
    int32_t   relink;          /* 0x1124, keep the spine order consistent */
    uint8_t   pad_1128[0x116c - 0x1128];
    evv_ref       nsq_marks;   /* 0x116c, one per fenced field */
    int32_t   unknown_1170;    /* 0x1170, cleared after an insert */
    int32_t   fence_base;      /* 0x1174 */
    uint8_t   pad_1178[0x11e8 - 0x1178];
    int32_t   unknown_11e8;    /* 0x11e8, cleared when a rule returns */
    int16_t   unknown_11ec;    /* 0x11ec, what actd_goto answers with */
    /* The block is 0x11f0 bytes: that is what ccode_new asks malloc for,
       so the two bytes after the last named field are all there is. */
    uint8_t   pad_11ee[2];
} delta_vars;

/* One variable of a rule, as the language compiled it. */
typedef struct {
    const char *name;         /* +0x00 */
    int32_t     unknown_04;
    int16_t     kind;         /* +0x08, DK_SYNC for one that holds a node */
    int8_t      flag;         /* +0x0a, bit 7 means the rule keeps it to
                                 itself and nothing outside may name it */
    int8_t      pad_0b;
} delta_varinfo;

/* A rule as the language compiled it, so far as the save layer reads one. */
typedef struct {
    uint8_t              pad_00[8];
    const delta_varinfo *locals;   /* +0x08 */
    uint8_t              pad_0c[0x22 - 0x0c];
    int16_t              nlocals;  /* +0x22 */
} delta_actdesc;

/* One global variable of the machine, as the language declared it. Every
   variable lives in the tail of delta_state as a cell: its type tag first,
   then its value, so that a pointer to the value always has the tag just
   in front of it. There are four kinds and the tag says which. */
#define DG_WORD      (-6)   /* a 32-bit value, in an eight-byte cell */
#define DG_LONG      (-3)   /* likewise, but the language calls it a long */
#define DG_SHORT     (-4)   /* a 16-bit value, in a four-byte cell */
#define DG_COMPOUND  (-9)   /* a run of bytes, described separately */

/* Where the cells start, which is the first byte of delta_state the fields
   above do not name. */
#define DG_BASE 0xb0

/* What a compound variable needs beyond its kind: what its first word is
   set to when the machine is reset, and how many bytes follow it. The
   whole cell is that plus the four bytes in front. */
typedef struct delta_compound_decl {
    int32_t init;
    int32_t bytes;
} delta_compound_decl;

/* The language's variable list, in the order it declared them, and the
   compound ones' extra description in the same order. Generated, and read
   through whichever language this thread is speaking -- see delta_lang.h. */
#define delta_globals    (delta_lang_now()->globals)
#define delta_globals_n  (delta_lang_now()->globals_n)
#define delta_compounds  (delta_lang_now()->compounds)

/* One entry of the compound index the machine builds. */
typedef struct {
    unsigned char *at;     /* +0x00 */
    int32_t        init;   /* +0x04 */
    int32_t        bytes;  /* +0x08 */
} delta_compound;

typedef struct delta_state delta_state;

struct delta_state {
    /* How many of each kind the language declared -- but doubled. Every one
       of the four indexes below holds its list twice, back to back, and
       these counts are of the doubled list. */
    int32_t      ncompound;       /* 0x0000 */
    int32_t      nlong;           /* 0x0004 */
    int32_t      nshort;          /* 0x0008 */
    int32_t      unknown_000c;
    int32_t      nword;           /* 0x0010 */
    /* Where each variable's value is. Nothing outside delta_new knows what
       offset a variable landed at; everything reaches one through these. */
    evv_ref     word;            /* 0x0014 */
    evv_ref     compound;     /* 0x0018 */
    evv_ref     lng;             /* 0x001c */
    evv_ref     shrt;            /* 0x0020 */
    int32_t      unknown_0024;
    evv_ref     sets;            /* 0x0028, the language's lookup sets, one
                                     0x24-byte descriptor each */
    evv_ref     act_table;       /* 0x002c, the dictionary's action table,
                                     one 0x28-byte entry each */
    evv_ref     set_store;  /* 0x0030, one pointer per set to the
                                         entries themselves */
    /* Two direct handles on the second and third word variable, kept here
       beside the language's own tables. Nothing transcribed so far reads
       either of them, so what they are for is still open. */
    evv_ref     direct_a;        /* 0x0034 */
    evv_ref     direct_b;        /* 0x0038 */
    int32_t      unknown_3c;      /* 0x003c, a forto's third parameter */
    delta_pta    lpta;            /* 0x0040 */
    delta_pta    rpta;            /* 0x0050 */
    evv_ref     act_store;  /* 0x0060, the same for the actions */
    evv_ref     owner;           /* 0x0064, whoever wants to know the spine
                                     moved; the flag it sets is at 0x1b8 */
    evv_ref     vars;            /* 0x0068 */
    evv_ref     stack;           /* 0x006c */
    evv_ref     dlang;           /* 0x0070, the language's own block: the
                                     statement generator hangs off it */
    evv_ref     logio;           /* 0x0074, the logical file table */
    evv_ref     eloqc;           /* 0x0078, what the machine keeps for ECI */
    uint8_t      fence_room;      /* 0x007c, how many the arrays below hold */
    uint8_t      pad_007d[3];
    /* Each of the three fenced-character arrays is kept twice: where it was
       allocated, and where the machine is working in it. */
    evv_ref     fence_chars_base;   /* 0x0080 */
    evv_ref     fence_chars;     /* 0x0084, fenced character by index */
    evv_ref     fence_index_base;   /* 0x0088 */
    evv_ref     fence_index;     /* 0x008c, index by fenced character */
    evv_ref     fence_marks_base;   /* 0x0090 */
    evv_ref     fence_marks;     /* 0x0094, one per fenced character */
    uint8_t      nstmts;         /* 0x0098, how many statement types
                                    the language declares, which is
                                    also how many fields a node has;
                                    the fence index uses it as the
                                    mark for a field it does not
                                    fence */
    uint8_t      pad_0099;
    int16_t      lang_a;          /* 0x009a */
    int16_t      lang_b;          /* 0x009c */
    int16_t      pad_009e;
    evv_ref     lfnames;   /* 0x00a0, the streams that can be opened */
    uint8_t      nlfnames;        /* 0x00a4 */
    uint8_t      pad_00a5;
    int16_t      nsets;           /* 0x00a6 */
    evv_ref     dictfile;        /* 0x00a8 */
    int16_t      nactions;        /* 0x00ac */
    uint8_t      pad_00ae[DG_BASE - 0xae];
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
    int16_t     nvalues;      /* +0x10, how many names above */
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
typedef struct delta_stmt {
    const char            *name;      /* +0x00 */
    const delta_fielddesc *fields;    /* +0x04 */
    void *(*const         *get)(void *);  /* +0x08, one reader per field */
    void (*const          *put)(void *, const void *);
                                      /* +0x0c, one writer per field */
    const uint8_t         *variants;  /* +0x10, null unless the type has any */
    const uint8_t         *deflt;     /* +0x14, what a fresh statement holds */
    int32_t                unknown_18; /* +0x18, set when the type has a
                                          statement worth starting from */
    int32_t                unknown_1c; /* +0x1c, cleared on a reinit */
    int32_t                nfields;   /* +0x20, how many the type declares */
    int32_t                length;    /* +0x24, the whole record in bytes */
    int32_t                stride;    /* +0x28, one variant */
    int32_t                varlen;    /* +0x2c, how much of one to copy */
    int32_t                whole_token; /* +0x30, one means the
                                          reader takes a whole
                                          line as one token */
    uint8_t                marks[2];  /* +0x34, the pair the printer brackets
                                         a statement with */
    uint8_t                walkable;  /* +0x36, only Ms sets this */
    uint8_t                pad_37;
    int32_t                gen_sel;   /* +0x38, which end a generate takes
                                          when the two disagree */
    int32_t                unknown_3c;
} delta_stmt;

/* Not const: the runtime marks a type when it has a statement
   worth starting from, and clears that again on a reinit. It belongs to a
   language, so it is reached through the one this thread is speaking; every
   site that reads it is a primitive running on a machine, and the language
   is set around anything that runs one. */
#define vstmtbl (delta_lang_now()->stmtbl)

void delta_delete(delta_state *d);

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
int  testgt(delta_state *d);
int  testge(delta_state *d);
int  testlt(delta_state *d);
int  testle(delta_state *d);
int  test_time(delta_state *d, int16_t tag);
int  test_fence(delta_state *d, int16_t tag, uint8_t n, const uint8_t *chars);
int  test_eof(delta_state *d, int32_t lf);
int  test_hasval(delta_state *d);

void  fence(delta_state *d, int8_t n, const uint8_t *chars);
void *TFLDS(void *p);
void *getDeltaStackVBot(delta_state *d);
void  setDeltaStackVBot(delta_state *d, void *v);
int32_t vback(delta_state *d, int32_t depth);
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
void CLRNONSEQ(delta_node *t);
int  visnonseq(delta_state *d, uint8_t f, int32_t l, int32_t r);
int  vmergable(delta_state *d, int32_t l, int32_t r);
int  insert_2pt(delta_state *d, uint8_t f, uint8_t n, const uint8_t *str,
                uint8_t mode);
int32_t merge(delta_state *d);
void *TVFLDS(void *p);
const char *streamName(int8_t st);
void noop1(delta_state *d);
void code_end(delta_state *d);
void goto_1(delta_state *d);
void c_code(delta_state *d);
void call(delta_state *d);
void call2(delta_state *d);
void execcmd(delta_state *d);
void startcmd(delta_state *d);
void startstmt(delta_state *d);
void startstmt_e(delta_state *d);
void startstmt_l(delta_state *d);
void tag(delta_state *d);
void tag_e(delta_state *d);
void tag_l(delta_state *d);
void nullines(delta_state *d);
void nullines_l(delta_state *d);
void fail(delta_state *d);
void halt(delta_state *d);
void abort_1(delta_state *d);
void prt_tvar(delta_state *d);
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
int32_t vgen_frame(delta_state *d);
int32_t vgen_time(delta_state *d);
int32_t vgen_params(delta_state *d);
int32_t vgen_copy(delta_state *d);
void gendef_framedur(delta_state *d, delta_loc *loc);
void gendef_timestm(delta_state *d, uint8_t when);
void gendef_params(delta_state *d, uint8_t count, uint8_t n,
                   const uint8_t *str);
void gencur_framedur(delta_state *d, delta_loc *loc);
void gencur_timestm(delta_state *d, uint8_t when);
void gencur_params(delta_state *d, uint8_t count, uint8_t n,
                   const uint8_t *str);
int32_t gen_copy(delta_state *d);
void vsub(delta_state *d, const delta_operand *a, const delta_operand *b);
void vmult(delta_state *d, const delta_operand *a, const delta_operand *b);
void vdiv(delta_state *d, const delta_operand *a, const delta_operand *b);
void divzero(delta_state *d);
void vnegate(delta_state *d, const delta_operand *a);
int32_t vcompareTypeCheck(delta_state *d, const delta_operand *a,
                          const delta_operand *b);
int32_t VLSYNC(const delta_node *t, int8_t i);
int32_t VRSYNC(delta_state *d, const int32_t *t, int8_t i);
int32_t gcql(delta_state *d, int32_t at, int8_t f, int8_t i);
int32_t gcqr(delta_state *d, int32_t at, int8_t f, int8_t i);
int  chksyncsflags(delta_state *d);
int  vctxtinit(delta_state *d);
int  vclrctxt(delta_state *d, int32_t unused);
void mapsyncs(delta_state *d, int32_t t);
int  vredoctxt(delta_state *d, int32_t arg);
int32_t etiwinMain(delta_state *d, int32_t argc, char **argv);

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
void set_saved_ptrs(delta_state *d, int32_t was, int32_t now);
void npush_i(delta_state *d, int32_t x);
void npush_s(delta_state *d, int32_t x);
void vscaninit(delta_state *d);
delta_node *vmovel(delta_state *d, delta_node *t, uint8_t f);
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

void seqscan(delta_state *d, delta_seqctl *c);
int  advance_tok(delta_state *d);
int  forall_cont_from(delta_state *d, int16_t tag, int16_t loop,
                      int32_t unused, delta_loc *dst, const delta_loc *src);
void savescptr(delta_state *d, int16_t tag, delta_loc *v);
int  get_parm(delta_state *d, delta_loc *out, delta_loc *loc, int16_t kind);
int  test_synch(delta_state *d, int16_t tag, uint8_t n, const uint8_t *list);
int  test_string_i(delta_state *d, uint8_t st, uint8_t n, const uint8_t *str);
int  test_string_s(delta_state *d, uint8_t st, uint8_t n, const uint8_t *str);
int  test_string(delta_state *d, uint8_t st, uint8_t n,
                 const uint8_t *str);
int  test_string_l(delta_state *d, uint8_t st, uint8_t n,
                   const uint8_t *str);
int  test_string_lng(delta_state *d, uint8_t st, uint8_t n,
                     const uint8_t *str);
int32_t ctxlook(delta_state *d, int32_t t, uint8_t f, int32_t right);

int vnormalize(delta_state *d, delta_tpos *p);
int vmove_tv(delta_state *d, delta_tpos *p);
int vtstsnc_tv(delta_state *d, delta_tpos *p);
int vtsttmark_tv(delta_state *d, delta_tpos *p, uint8_t back);
int test_ptr(delta_state *d);
void lpta_movel(delta_state *d, uint8_t f);
void lpta_mover(delta_state *d, uint8_t f);
void rpta_mover(delta_state *d, uint8_t f);
int  lpta_tstmover(delta_state *d, uint8_t f);
int  rpta_tstmover(delta_state *d, uint8_t f);
int  rpta_tstmovel(delta_state *d, uint8_t f);
int  setscan_l(delta_state *d, uint8_t f);
int  setscan_r(delta_state *d, uint8_t f);
int  setscan_nof_l(delta_state *d, uint8_t f);
int  setscan_nof_r(delta_state *d, uint8_t f);
int32_t vgetsc(delta_state *d, int32_t back, int32_t ctx, int32_t t, uint8_t f);
int  vtimept_tv(delta_state *d, delta_tpos *p, uint8_t back);
int  for_loop_preamble(delta_state *d, int32_t tag, int32_t loop, int32_t f,
                       const delta_token *tok);
int  dupsync(delta_state *d, int32_t t, int32_t src, uint8_t back);
int  vdef_proj(delta_state *d, int32_t t, uint8_t f);
int  vprt_range(delta_state *d, delta_tpos *a, delta_tpos *b);
int  forto_adv_r(delta_state *d, int16_t tag, int16_t loop, int16_t bound,
                 uint8_t f, delta_token *tok, const delta_token *end);
int  forto_adv_upto_r(delta_state *d, int16_t tag, int16_t loop, int16_t bound,
                      uint8_t f, delta_token *tok, const delta_token *end);
int  setd_lookup(delta_state *d, int32_t arg, int16_t set);
int  vmark(delta_state *d, uint8_t st, uint8_t fld, int32_t t, int32_t stop,
           const void *value);
int  visleft(delta_state *d, int32_t a, int32_t b);
int  visright(delta_state *d, int32_t a, int32_t b);

/* The Delta heap. The structure is the original's; where the raw memory comes
   from is not, so that a target without a general allocator can supply an
   arena instead. */
void *delta_sys_alloc(size_t n);
void  delta_sys_free(void *p);

int   initializeDeltaHeap(delta_state *d, int32_t size);
void  resetDeltaHeap(delta_state *d);
/* These two are fastcall in the original -- the machine in ecx and the size
   or the object in edx -- which is how their names come to carry the
   argument size. Everything else here is cdecl. */
#ifndef DELTA_FASTCALL
#if defined(__i386__)
#define DELTA_FASTCALL __attribute__((fastcall))
#else
#define DELTA_FASTCALL
#endif
#endif

DELTA_FASTCALL void *allocDeltaHeapObject(delta_state *d, int32_t size);
DELTA_FASTCALL void  freeDeltaHeapObject(delta_state *d, void *p);

/* Giving the heap and the stack back, and setting the stack up. */
void  deltaHeapCleanup(delta_state *d);
int32_t initializeDeltaStack(delta_state *d, int32_t size);
int32_t peekDeltaStackStart(delta_state *d);
int32_t peekDeltaStackNext(delta_state *d);
void    resetDeltaStack(delta_state *d);
void  freeDeltaHeapTo(delta_state *d, uint8_t *pos, int32_t release);
int32_t getDeltaHeapSegNumber(delta_state *d, uint8_t *p, int32_t unit);
int   recordDeltaHeapPos(delta_state *d);
void  free_heap(delta_state *d, void *p);
void *alloc_tok(delta_state *d, const delta_stmt *e);
void *alloc_sync(delta_state *d);
int   vcomp_pta(delta_state *d, delta_tpos *a, delta_tpos *b);
const char *vseqbad(void *w, void *x, void *y, const char *what);
void cacheDeletedDeltaObject(delta_state *d, void *p);
int  compare_ptas(delta_state *d);
void delsync(delta_state *d, void *p);
int  mashtoks(delta_state *d, uint8_t f, int32_t t);
int  vchkseqbad(delta_state *d, int32_t t, uint8_t f, const char *what);
int  chkdelnonseq(delta_state *d, int32_t t, uint8_t f);
int  fdeldel(delta_state *d, int32_t from, int32_t to, int32_t arg);
void fdel(delta_state *d, int32_t whole, int32_t arg);
int  vdel_1pt(delta_state *d, uint8_t f, int32_t t, int32_t arg);
int  vdel_2pt(delta_state *d, uint8_t f, int32_t l, int32_t r);
int  vins_tok(delta_state *d, uint8_t f, int32_t l, int32_t r,
              const delta_operand *v);
int  vinit_stm(delta_state *d, int8_t f);
int  ins_tokens_s(delta_state *d, uint8_t f, const uint8_t *str, uint8_t n,
                  int32_t arg);
int  ins_tokens_i(delta_state *d, uint8_t f, const uint8_t *str, uint8_t n,
                  int32_t arg);
int  ins_tokens_l(delta_state *d, uint8_t f, const uint8_t *str, uint8_t n,
                  int32_t arg);
int  ins_tokens_lng(delta_state *d, uint8_t f, const uint8_t *str, uint8_t n,
                    int32_t arg);
int32_t vsplit_time(delta_state *d, uint8_t f, int32_t t, int32_t off);
int  vsync_tv(delta_state *d, delta_tpos *p);
int  vtmark_tv(delta_state *d, delta_tpos *p, uint8_t back);
void delete_1pt(delta_state *d, uint8_t f);
void lpta_storep(delta_state *d, delta_loc *loc);
int  vrange_l(delta_state *d, delta_tpos *p, delta_tpos *out, int8_t f,
              uint8_t dup);
int  vrange_r(delta_state *d, delta_tpos *p, delta_tpos *out, int8_t f,
              uint8_t dup);
int  vrange_2pt(delta_state *d, delta_tpos *a, delta_tpos *b, int8_t f,
                uint8_t mode);
int  insert_rv(delta_state *d, uint8_t f, delta_loc *loc, uint8_t dup);
int32_t wordIndexCallback(delta_state *d, const delta_loc *loc);
int32_t userIndexCallback(delta_state *d);
void insert_l(delta_state *d, int8_t f, uint8_t n, const uint8_t *str,
              uint8_t dup);
void insert_r(delta_state *d, int8_t f, uint8_t n, const uint8_t *str,
              uint8_t dup);
int  insert_2pt_s(delta_state *d, uint8_t f, uint8_t n, const uint8_t *str,
                  uint8_t mode);
int  insert_2pt_l(delta_state *d, uint8_t f, uint8_t n, const uint8_t *str,
                  uint8_t mode);
int  insert_2pt_lng(delta_state *d, uint8_t f, uint8_t n, const uint8_t *str,
                    uint8_t mode);
int  insert_2pt_i(delta_state *d, uint8_t f, uint8_t n, const uint8_t *str,
                  uint8_t mode);
int  delete_2pt(delta_state *d, uint8_t f, uint8_t mode);
int  mark_s(delta_state *d, uint8_t f, uint8_t fld, uint8_t value,
            uint8_t mode);
int  mark_v(delta_state *d, uint8_t f, uint8_t fld, delta_loc *loc,
            uint8_t mode);
int  insert_2ptv(delta_state *d, uint8_t f, delta_loc *loc, uint8_t mode);
void deltaReinit(delta_state *d, int32_t full);
void initdelta(delta_state *d, uint8_t n, const uint8_t *list);

/* The frame a compiled rule runs inside. */
int  init_ptr_active_record(delta_state *d);
int  ventproc(delta_state *d, delta_actrec *rec, uint8_t *index,
              uint8_t *chars, uint8_t *marks, void *jb);
int  vretproc(delta_state *d, int32_t tag);
int  succeed(delta_state *d);
void move_i(delta_state *d, delta_loc *loc, int16_t value);
void move_lng(delta_state *d, delta_loc *loc, int32_t value);
void pause(delta_state *d);
int  actd_goto(delta_state *d);
void npush_lng(delta_state *d, int32_t v);
void npush_l(delta_state *d, int32_t x);
void ncompare(delta_state *d);
int  back(delta_state *d);
int  back_nboa(delta_state *d);
void bsclr_pushca(delta_state *d, int16_t tag);
void bspush_vbot(delta_state *d);
void bspop_vbot(delta_state *d);
void npush_v(delta_state *d, delta_loc *loc);
void npush_vf(delta_state *d, delta_loc *loc);
void c_assvar(delta_state *d, delta_loc *loc);
int  advance_strm(delta_state *d);
int32_t absoluteSyncNum(delta_state *d, uint8_t *p);
int  while_iterate(delta_state *d, int16_t test_tag, int16_t loop_tag);
void proj_def(delta_state *d, uint8_t f);
void rpta_movel(delta_state *d, uint8_t f);
int  lpta_tstmovel(delta_state *d, uint8_t f);
void rpta_storep(delta_state *d, delta_loc *loc);
void lpta_loadv(delta_state *d, uint8_t f, const delta_loc *loc);
void lpta_loadi(delta_state *d, uint8_t f, int32_t v);
void lpta_loadlng(delta_state *d, uint8_t f, int32_t v);
void rpta_loadv(delta_state *d, uint8_t f, const delta_loc *loc);
void rpta_loadi(delta_state *d, uint8_t f, int32_t v);
void rpta_loadl(delta_state *d, uint8_t f, int32_t v);
void lpta_leftmost(delta_state *d, uint8_t f);
void rpta_leftmost(delta_state *d, uint8_t f);
void lpta_rightmost(delta_state *d, uint8_t f);
void rpta_rightmost(delta_state *d, uint8_t f);
void settvar_i(delta_state *d, delta_loc *loc, int32_t v);
void settvar_s(delta_state *d, delta_loc *loc, int32_t v);
void settvar_l(delta_state *d, delta_loc *loc, int32_t v);
void settvar_lng(delta_state *d, delta_loc *loc, int32_t v);
void settvar_v(delta_state *d, delta_loc *loc, delta_loc *src);
void assok(delta_state *d, delta_loc *loc);
void noass(delta_state *d, delta_loc *loc);
void chkvars(delta_state *d);
void chkokass(delta_state *d);
int  vnegative(delta_state *d, const delta_operand *v);
void compare_tvars(delta_state *d, delta_loc *a, delta_loc *b);
int  if_testeq(delta_state *d);
int  if_testneq(delta_state *d);
int  if_testlt(delta_state *d);
int  if_testle(delta_state *d);
int  if_testgt(delta_state *d);
int  if_testge(delta_state *d);
void npop(delta_state *d, delta_loc *loc);
void ncompare_s(delta_state *d, uint8_t c);
int  forall_to_test(delta_state *d, delta_loc *a, delta_loc *b);
int  mark_i(delta_state *d, uint8_t st, uint8_t fld, const void *v,
            uint8_t mode);
int  mark_l(delta_state *d, uint8_t st, uint8_t fld, const void *v,
            uint8_t mode);
int  mark_lng(delta_state *d, uint8_t st, uint8_t fld, const void *v,
              uint8_t mode);
void SETCTXL(delta_state *d, int32_t *table, uint8_t idx, int32_t bits);
void SETCTXR(delta_state *d, int32_t *table, uint8_t idx, int32_t bits);
int  vctxt_tv(delta_state *d, delta_tpos *p);
int  testeq_tvars(delta_state *d, delta_loc *a, delta_loc *b);
int  testneq_tvars(delta_state *d, delta_loc *a, delta_loc *b);
int  if_testeq_v_i(delta_state *d, delta_loc *loc, int32_t x);
int  if_testneq_v_i(delta_state *d, delta_loc *loc, int32_t x);
int  if_testlt_v_i(delta_state *d, delta_loc *loc, int32_t x);
int  if_testgt_v_i(delta_state *d, delta_loc *loc, int32_t x);
int  if_testge_v_i(delta_state *d, delta_loc *loc, int32_t x);
int  if_testle_v_i(delta_state *d, delta_loc *loc, int32_t x);
int  if_testeq_v_lng(delta_state *d, delta_loc *loc, int32_t x);
int  if_testneq_v_lng(delta_state *d, delta_loc *loc, int32_t x);
int  if_testlt_v_lng(delta_state *d, delta_loc *loc, int32_t x);
int  if_testgt_v_lng(delta_state *d, delta_loc *loc, int32_t x);
int  if_testge_v_lng(delta_state *d, delta_loc *loc, int32_t x);
int  if_testle_v_lng(delta_state *d, delta_loc *loc, int32_t x);
void proj_def_mult(delta_state *d, uint8_t n, const uint8_t *str,
                   const delta_token *p);
void lpta_ctxtl(delta_state *d, uint8_t f);
void lpta_ctxtr(delta_state *d, uint8_t f);
void rpta_ctxtl(delta_state *d, uint8_t f);
void rpta_ctxtr(delta_state *d, uint8_t f);
int  calcETI2WPM(delta_state *d, const delta_loc *in, delta_loc *out);
int  calcMidline(delta_state *d, const delta_loc *in, delta_loc *out);
int  calcSpeedFactori(delta_state *d, const delta_loc *in, delta_loc *out);
void copyvar(delta_state *d, delta_loc *a, delta_loc *b);
int  forall_adv_l(delta_state *d, int16_t tag, int16_t loop, int16_t bound,
                  uint8_t f, delta_token *tok);
int  forto_adv_l(delta_state *d, int16_t tag, int16_t loop, int16_t bound,
                 uint8_t f, delta_token *tok, const delta_token *end);
int  for_test(delta_state *d, delta_loc *var, delta_loc *bound,
              delta_loc *step);
int  for_adv(delta_state *d, int16_t test_tag, int16_t loop_tag,
             delta_loc *var, delta_loc *bound, delta_loc *step);
int  savetok(delta_state *d, delta_loc *loc);
int  chk_itok(const char *s);
int  calcIntoni(delta_state *d, const delta_loc *base, const delta_loc *a,
                const delta_loc *b, delta_loc *out);
int  modulate_pwindi(delta_state *d, const delta_loc *in, delta_loc *a,
                     delta_loc *b);
void getDeltaCcodeParm(const delta_loc *src, void *dst, int16_t want);
void setDeltaCcodeReturnValue(const void *src, int16_t from, delta_loc *dst);
void setDeltaReturnCode(delta_state *d, uint8_t code);
int  modulo(delta_state *d, const delta_loc *a, const delta_loc *b,
            delta_loc *out);
int  ctxt_clstr(delta_state *d, int32_t t, int8_t f);
int  chstream(delta_state *d, int16_t v, uint8_t f);
int  calcWPM2ETI(delta_state *d, const delta_loc *in, delta_loc *out);
int  calcST2HZ(delta_state *d, const delta_loc *in, delta_loc *out);
int  calcHZ2ST(delta_state *d, const delta_loc *in, delta_loc *out);
void project_rl(delta_state *d, delta_node *t, int32_t unused_10,
                int32_t unused_14, delta_node *l, delta_node *r, uint8_t f);
int  actd_lookup(delta_state *d, int16_t n, delta_token *outl,
                 delta_token *outr);
int  vproj_r(delta_state *d, delta_node *t, delta_node *at, uint8_t f);
int  conj_merge(delta_state *d, delta_token *tok);
int  vproj_l(delta_state *d, delta_node *t, delta_node *at, uint8_t f);
void proj_r(delta_state *d, uint8_t f);
void proj_l(delta_state *d, uint8_t f);
int  forto_adv_upto_l(delta_state *d, int16_t tag, int16_t loop,
                      int16_t bound, uint8_t f, delta_token *tok,
                      const delta_token *end);
int  calcHZ2ETI(delta_state *d, const delta_loc *in, delta_loc *out);
int  vscanadvUptoToken(delta_state *d, int32_t usefence);
int  forall_adv_over_r(delta_state *d, int16_t tag, int16_t loop,
                       int16_t bound, uint8_t f, delta_token *tok);
int  forall_adv_upto_r(delta_state *d, int16_t tag, int16_t loop,
                       int16_t bound, uint8_t f, delta_token *tok);
int  forall_adv_over_l(delta_state *d, int16_t tag, int16_t loop,
                       int16_t bound, uint8_t f, delta_token *tok);
int  forall_adv_upto_l(delta_state *d, int16_t tag, int16_t loop,
                       int16_t bound, uint8_t f, delta_token *tok);
int  forto_adv_over_l(delta_state *d, int16_t tag, int16_t loop,
                      int16_t bound, uint8_t f, delta_token *tok,
                      const delta_token *end);
int  forto_adv_over_r(delta_state *d, int16_t tag, int16_t loop,
                      int16_t bound, uint8_t f, delta_token *tok,
                      const delta_token *end);
int  for_cont_from(delta_state *d, int16_t tag, int16_t loop, int32_t unused,
                   delta_loc *dst, const delta_loc *src);
void insert_lv(delta_state *d, uint8_t st, delta_loc *loc, uint8_t mode);
int  vtstctx_tv(delta_state *d, delta_tpos *p, int32_t back);
int  lpta_tstctxtl(delta_state *d, uint8_t f);
int  lpta_tstctxtr(delta_state *d, uint8_t f);
int  rpta_tstctxtl(delta_state *d, uint8_t f);
int  rpta_tstctxtr(delta_state *d, uint8_t f);
int  f0_stepi(delta_state *d, const delta_loc *n, const delta_loc *f0,
              const delta_loc *step, const delta_loc *count, delta_loc *out);

/* The printing, reading and file layer, stubbed rather than transcribed.
   See src/delta_trace.c for what that costs and why. */
int32_t dur2(delta_state *d, const delta_tpos *a, const delta_tpos *b,
             int8_t f, int32_t back);
int32_t durcalc(delta_state *d, delta_tpos *a, delta_tpos *b, int8_t f,
                int32_t *cache, int32_t direct);
int32_t firstdefd(delta_state *d, int8_t f, int32_t t, uint8_t st,
                  int32_t back);
int32_t val_expr2(delta_state *d, delta_tpos *p, int8_t st, uint8_t fld,
                  int32_t which, int32_t mode, int32_t *out);
int32_t val_expr(delta_state *d, delta_tpos *p, int8_t st, uint8_t fld,
                 int32_t which);
void    val_expr1(delta_state *d, delta_loc *loc, uint8_t st, uint8_t fld);
int32_t vdur(delta_state *d, const delta_tpos *a, const delta_tpos *b,
             int8_t f);
int32_t vgen(delta_state *d, delta_tpos *l, delta_tpos *r,
             const delta_gencell *g, int32_t lf);
int32_t vgenerate(delta_state *d);
void    generate(delta_state *d, int32_t lf);
int  vdur_ass(delta_state *d, delta_tpos *a, delta_tpos *b, int8_t f,
              int32_t total);
int  dur_ass(delta_state *d, int8_t f, delta_loc *field, uint8_t mode);
void dur_expr(delta_state *d, uint8_t f, delta_loc *field);

int  open_input(delta_state *d, int32_t which);
int  open_output(delta_state *d, int32_t which);
int  read_tvar(delta_state *d, int8_t f, delta_loc *field);
int  vrd_tvar(delta_state *d, int32_t f, const delta_operand *v);
int  checkInterrupt(delta_state *d);
int  vf_getc(delta_state *d, int32_t f);
int32_t vf_ungetc(delta_state *d, int32_t f);
void *logicalFileName(delta_state *d, int32_t which);
int  logicalFileOpen(delta_state *d, void *name, int32_t mode);
void vfclose_lf(delta_state *d, int32_t lf);
int8_t vffind_lf(delta_state *d, const char *name);
int32_t vf_gets(delta_state *d, int32_t lf, const char *prompt);
int32_t vf_puts(delta_state *d, int32_t lf, const char *s, int32_t flush);
void vf_clrbuf(delta_state *d, int32_t lf);
int32_t vf_eof(delta_state *d, int32_t lf);
void setInterrupt(delta_state *d, int32_t v);
int32_t logio_new(delta_state *d);
void logio_delete(delta_state *d);
int32_t logicalIOInit(delta_state *d, int32_t room, void *report);
void logicalIOCleanup(delta_state *d);
int8_t addLogicalFile(delta_state *d, const char *name);
int8_t vfdef_lf(delta_state *d, const char *name);
int32_t vfundef_lf(delta_state *d, const char *name);
int32_t builtInLogicalFiles(delta_state *d);
int32_t logicalFileAddPhysical(delta_state *d, int32_t lf, const char *name,
                               void *cls, void *handle, int32_t mode);
int32_t logicalFileRemovePhysical(delta_state *d, int32_t lf,
                                  const char *name, int32_t input);
int32_t logicalFileRemoveAllPhysical(delta_state *d, int32_t lf,
                                     int32_t input);
int32_t logicalFileFindPhysical(delta_state *d, int32_t lf, const char *name,
                                int32_t input, int32_t current);
int32_t vf_printf(delta_state *d, int32_t lf, int32_t flush,
                  const char *fmt, ...);
void vfstat(delta_state *d, int32_t lf);
void vfstatall(delta_state *d);
void *logicalNullClass(delta_state *d);
int32_t logicalStandardStream(delta_state *d, int32_t which);

/* Where the Delta machine meets ECI. */
int32_t eloqc_new(delta_state *d);
void eloqc_delete(delta_state *d);
int32_t ecilink_new(delta_state *d);
int32_t ecilink_delete(delta_state *d);
int32_t initializeIO(delta_state *d);
int32_t closeIO(delta_state *d);
void eciLinkCleanup(delta_state *d);
int32_t multitask(delta_state *d);
void callSetEngsynError(delta_state *d, const void *what);

/* Supplied by the layers above, not by us. */
void initDllLink(void);
void setEngsynError(delta_state *d, int32_t code);

void print_lit(delta_state *d, ...);
void print_var(delta_state *d, ...);
void print_stream(delta_state *d, ...);
void vprt_var(delta_state *d, ...);
void vprt_strm(delta_state *d, ...);
/* Spell one field of one token as the language names it, which is what the
   phoneme callback reports and what a person reads a token by. The pointer is
   the token four bytes in, as the original's own caller hands it over. */
void disptok(delta_state *d, const void *at, int32_t stream, int32_t field,
             char *out);
void lithex(const char *in, char *out, int32_t max);
int8_t getbksl(delta_state *d, int32_t f);
void readErrorReport(delta_state *d, ...);
int  var_rderr(delta_state *d, int32_t f, const char *buf);
int  rdtokverr(delta_state *d, int32_t f, uint8_t st,
               const char *buf);
int  vrd_nvar(delta_state *d, int32_t f, const delta_operand *v);
int  vrd_delta(delta_state *d, int32_t f, uint8_t st);
void *varloc(delta_state *d, uint8_t hi, uint8_t lo, int32_t ctx);
void *vonstack(delta_state *d, int32_t ctx);

/* Writing the machine out and reading it back. Nothing in the engine calls
   any of this; see the end of delta_trace.c. */
void    svgeterr(delta_state *d, int32_t which);
void    svgetmsg(delta_state *d);
void    svgetimp(delta_state *d, int32_t which);
void    svputerr(delta_state *d);
int32_t svgetl(delta_state *d);
int     svgeti(delta_state *d);
int8_t  svgetc(delta_state *d);
uint8_t svgetu(delta_state *d);
char   *svgets(delta_state *d);
void    svputl(delta_state *d, int32_t v);
void    svputi(delta_state *d, int32_t v);
void    svputc(delta_state *d, int8_t c);
void    svputu(delta_state *d, uint8_t c);
void    svputs(delta_state *d, const char *s);
void    svputgptrs(delta_state *d);
void    svputlptrs(delta_state *d, int32_t node, int8_t sep);
int32_t findsync(delta_state *d, int32_t n, int8_t dir);
int     vsvdelta(delta_state *d, uint8_t stream);
void    vsv2delta(delta_state *d);
int     vrsdelta2(delta_state *d);

/* Where those bytes go. The original reaches straight for the C library on
   a FILE it keeps in the stack block; these two are the seam a target puts
   its own file layer behind, and src/delta_savefile.c is the C library one.
   Both answer how many bytes moved. */
int32_t delta_save_read(delta_state *d, void *buf, int32_t n);
int32_t delta_save_write(delta_state *d, const void *buf, int32_t n);

/* Supplied by the language module, not by the runtime. */
const uint8_t *actdlookup(delta_state *d, int32_t l, int32_t r,
                          const void *entry);

/* The runtime's constant tables, lifted out of the original by
   tools/delta-tables.py. */
extern const int16_t delta_ETI2WPM_Table[252];
extern const int16_t delta_ExpTab[176];
extern const int16_t delta_ExpTable[128];
extern const int32_t delta_ExpTableCh0[8];
extern const int32_t delta_ExpTableCh1[8];
extern const int32_t delta_ExpTableCh2[8];
extern const int16_t delta_Hz2MiTable[287];
extern const int16_t delta_LnTable[268];
extern const int16_t delta_LogTab[160];
extern const int16_t delta_Mi2HzTable[228];
extern const int16_t delta_MidlineVals[104];
extern const int16_t delta_PwindModTable[12];
extern const int16_t delta_SpeedTable[152];
/* Four-byte records; the runtime reads the first half of each. */
extern const int32_t delta_frequencyInHz[122];
extern const int32_t delta_frequencyInST[122];

/* Supplied by the language, not the runtime: lay a string of values into a
   range the caller has already opened. */
int ins_tokens(delta_state *d, int8_t f, const uint8_t *str, uint8_t n,
               int32_t arg);
int ins_rdtoks(delta_state *d, uint8_t f, int32_t l, int32_t r, int32_t arg);
void *vins_sync(delta_state *d, uint8_t f, int32_t l, int32_t r);

/* Supplied by the language, not the runtime: match the span between the two
   registers against one of its lookup sets. */
int setdlookup(delta_state *d, int32_t from, int32_t to, void *set,
               int32_t arg);
int vproject(delta_state *d, int32_t t, int32_t left, int32_t right, uint8_t f);

/* The block the runtime reports to. It belongs to whoever embedded the
   machine, and only a handful of its fields are ever touched from here; the
   offsets they sat at in the original are against each one, because that is
   what they were read off. Nothing compiled from a rule reaches into it -- a
   rule tells the runtime and the runtime tells the owner -- so the fields may
   sit where the compiler puts them. */
typedef struct {
    const char **names;      /* 0x000, what each kind of activation is called */
    int32_t      unknown_04; /* 0x004, set to three and never read from here */
    int32_t      unknown_10; /* 0x010, set to two */
    int8_t       unknown_14; /* 0x014, cleared by the save layer */
    int32_t      code;       /* 0x1a4, left when a rule returns with the
                                backtracking stack not empty */
    int32_t      unknown_1a8; /* 0x1a8, cleared by the save layer */
    int8_t       unknown_1b0; /* 0x1b0, set to five */
    int32_t      changed;    /* 0x1b8, bumped whenever the spine moved */
    int32_t      unknown_1cc; /* 0x1cc, set to a constant nothing reads */
    int32_t      unknown_1d0; /* 0x1d0, cleared */
    int32_t      unknown_1b4; /* 0x1b4, how many streams the runtime declared */
    int32_t      unknown_1dc; /* 0x1dc, set to one */
    int32_t      argc;       /* 0x1d4, what the owner was started with */
    char       **argv;       /* 0x1d8 */
    const char  *unknown_1ec; /* 0x1ec, an empty name */
} delta_owner;


/* Bumped whenever the spine is relinked, so anything holding a position knows
   to look again. */
extern int32_t spine_changed;

#endif
