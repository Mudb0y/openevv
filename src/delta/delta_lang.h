/* One language, as everything outside it sees it.
 *
 * A language module is a pile of tables -- the rules as bytecode, the
 * constants they name by address, the statement table, the variable list,
 * the settings the engine carries in its image -- and until now there was
 * one of each and the engine reached them by name. Two languages cannot be
 * linked that way: every module gives its tables the same names, because
 * they are IBM's names and IBM built one library per language.
 *
 * So each module now names its own tables after itself -- `enus_vstmtbl',
 * `dede_vstmtbl' -- and gathers them into one of these, which is the only
 * name the engine knows it by. tools/gen-lang.py writes that gathering.
 *
 * How the engine finds the right one is the other half. Almost everything
 * that reads a table is a primitive the rules call, and a good many of those
 * are handed nothing but the value they are working on: STMTYP takes a kind
 * and nothing else. There is no argument to carry the language in, so the
 * language in force is kept per thread and set around anything that runs the
 * machine -- delta_run_rule, and the few places the engine drives it
 * directly. delta.h turns `vstmtbl' and its neighbours into reaches through
 * that, so the four thousand sites that read one did not have to change and
 * cannot quietly read the wrong language's.
 *
 * Per thread rather than per program because two instances speaking two
 * languages have a thread each. Nothing here is a lock: a thread sets the
 * language it is about to run and puts back what was there, the way the
 * arena's own thread-local free lists work.
 */

#ifndef DELTA_LANG_H
#define DELTA_LANG_H

#include <stdint.h>

/* Named rather than typedef'd, because delta.h names them properly and a
   header cannot say the same typedef twice. */
struct delta_state;
struct delta_stmt;
struct delta_compound_decl;
struct delta_rule_c;

/* A function a rule can call. Declared without an argument list because
   every arity from none to twenty-five appears among them. */
typedef void (*delta_rule_fn)(void);

/* And one rule of the language written as C rather than left as bytecode. */
typedef int32_t (*delta_rule_cfn)(void *state, const int32_t *args, int nargs);

/* One character of the language's alphabet as a caller writes it: the code
   point it arrives as, and the byte the machine knows it by. IBM's engine has
   no such table -- it turns a code point into a single byte through one
   Windows Western list and keeps the low byte of anything else, which is
   nothing for a letter outside that set. A language IBM never shipped needs
   somewhere to say what its own letters arrive as, and this is it. */
typedef struct {
    uint32_t cp;
    uint8_t  byte;
} delta_codepoint;

/* One store of bytes the rules name by address. */
typedef struct {
    uint8_t *at;
    uint32_t bytes;
} delta_store;

/* One rule, as the interpreter needs it. */
typedef struct {
    const char *name;
    const char *object;   /* which one it was compiled in */
    int32_t     offset;   /* into the bytecode */
    int32_t     length;
    int32_t     frame;    /* bytes below the frame base */
    int32_t     pbase;    /* where its arguments start */
    int32_t     params;
} delta_rule;

/* Everything one language is. Const, except that the symbol table has to be
   bound once the arena exists, so what is const is the address of the slot
   the bound table goes in rather than the table. */
typedef struct delta_language {
    const char *tag;            /* "enus" */
    const char *name;           /* "US English", for a person to choose from */
    int32_t     id;             /* the language packed as the API has it */
    const char *library_name;   /* "Static Engine ENU" */
    int32_t     state_bytes;    /* how big a machine of this language is */

    /* the rules */
    const uint8_t       *rule_code;
    const int32_t       *rule_imm;
    const uint8_t       *rule_map;
    const delta_rule_fn *rule_entry;
    const char *const   *rule_entry_name;
    const void *const   *rule_sym;
    int32_t              rule_sym_count;
    const delta_rule    *rules;
    int32_t              rule_count;
    int32_t              rule_setjmp;
    int32_t              frame_max;
    /* The ones written as C, in as many pieces as the decompiler wrote
       files, ending in a null. The pieces are only there because thirteen
       megabytes in one translation unit cannot be compiled on more than one
       core; nothing reads them as pieces, since delta_run_rule walks the lot
       once into an index by rule number. */
    const struct delta_rule_c *const *rule_native;
    delta_rule_cfn           **rule_native_by_number;

    /* the bytes the rules name by address, and the same as values once
       delta_syms_bind has copied them into the arena */
    const delta_store  *const_store;
    /* And the ones a rule of ours names rather than a rule of IBM's. Kept
       apart because the lifted list is generated out of the objects and
       anything added to it there would be lost the next time that ran. */
    const delta_store  *authored_store;

    /* What each of its own characters arrives as, for the text on the way
       in. Empty for the languages IBM shipped: theirs are all in the
       Western set already. */
    const delta_codepoint *codepoints;
    int32_t                codepoints_n;
    const int32_t     **sym_ref;

    /* the statement table, and the fixing-up the language does to it */
    struct delta_stmt *stmtbl;
    void      (*sizes)(void);

    /* the variables the language declares */
    const int8_t                     *globals;
    int32_t                           globals_n;
    const struct delta_compound_decl *compounds;

    /* what a machine of this language needs building and taking down */
    void (*link_new)(struct delta_state *);
    void (*link_delete)(struct delta_state *);
    void (*set_dict_new)(struct delta_state *);
    void (*set_dict_delete)(struct delta_state *);
    void (*act_dict_new)(struct delta_state *);
    void (*act_dict_delete)(struct delta_state *);

    /* the five entries the engine drives a machine through */
    int32_t (*proc_start)(struct delta_state *);
    int32_t (*proc_end)(struct delta_state *);
    int32_t (*proc_flush)(struct delta_state *);
    int32_t (*proc_process_sentences)(struct delta_state *);
    int32_t (*proc_process_remaining)(struct delta_state *);
    /* The command layer's entry. Nothing in the library path calls it --
       etiwinMain is what does, and etiwinMainDLL is what this engine uses
       instead -- but a language module has it and the table is where a
       module's entries belong. */
    int32_t (*proc_main)(struct delta_state *);

    /* the settings this language carries in the image */
    const char *ini;
    int32_t     ini_size;
} delta_language;

/* Every language linked into this program, ending with a null, and the
   numbers each of them states filled into it. The build writes both,
   because which languages are in is the build's to say. */
extern const delta_language *const delta_languages[];
void delta_lang_bind_all(void);

/* By the number the API uses. Answers null for one that is not linked in. */
const delta_language *delta_lang_by_id(int32_t id);

/* The one this thread is speaking. Setting answers what was there, so a
   caller puts it back.

   Per thread because two instances speaking two languages have a thread each,
   and set-and-put-back because a thread may drive one machine from inside
   another's callback. Inline because every reach for a table of the language
   comes through here -- the statement table, the globals, the rules -- and a
   call into another file for a thread-local read was four per cent of a run.

   Nothing may read a table without a language in force. Every path that runs
   the machine sets one, so an empty one here is a path nobody thought about
   rather than something to guess at. Guessing would speak the wrong language
   and sound almost right, which is the worst way for this to fail. */
extern __thread const delta_language *delta_lang_current;
void delta_lang_none(void);

static inline const delta_language *delta_lang_now(void)
{
    if (delta_lang_current == 0)
        delta_lang_none();
    return delta_lang_current;
}

static inline const delta_language *delta_lang_set(const delta_language *l)
{
    const delta_language *was = delta_lang_current;

    delta_lang_current = l;
    return was;
}

/* Which language made a machine. Kept in a word in front of the allocation:
   the state is IBM's layout from its first byte and the rules address it
   from there, so there is nowhere inside it to put one. */
const delta_language *delta_lang_of(const struct delta_state *d);

/* Running one rule of whichever language the machine is. This is where
   the language in force is set, and put back afterwards. */
int32_t delta_run_rule(void *state, const delta_rule *r,
                       const int32_t *args, int nargs);
const delta_rule *delta_find_rule(const char *name);

/* A machine of one language, and giving it back. The size is the language's,
   and the language is remembered in front of the block. */
struct delta_state *delta_lang_alloc(const delta_language *l);
void                delta_lang_free(struct delta_state *d);

/* The settings of every language linked in, as one blob with a section
   apiece, which is the shape the original's reader expects and what
   eciGetAvailableLanguages walks. Built once, on the first ask. */
const char *delta_lang_ini(void);
int32_t     delta_lang_ini_size(void);

#endif
