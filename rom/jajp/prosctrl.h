/* What the prosody chain is, as records.
 *
 * `ProsCtrl` is the last thing between the analysis and the engine: it takes
 * the breath groups `IntonPhrase` built and writes the ESPR string the
 * synthesiser speaks -- phonemes with pitch and stress and pause annotations
 * threaded through them. IBM keeps it in three objects, `PCProsCtrl.obj`,
 * `PCRoman2BG.obj` and `PCWriteESPR2.obj`, and thirteen of the sixteen
 * objects in that chain are empty because everything was inlined away.
 *
 * The first thing it does is copy: `BG_T2BreathGroups` walks IntonPhrase's
 * chain and builds a four-level tree of its own out of it. That copy is what
 * says where every field of both records sits, and it is a second source for
 * IntonPhrase's own map -- it confirms eleven fields of rom/jajp/intonphrase.h
 * independently, and corrected one, since the int16 it reads at 0x66 of a
 * group phrase is where a twenty-ninth code of the reading would have been.
 *
 * The four levels, with what each holds and what allocates it:
 *
 *   BREATHGROUP   0x0c   one per breath group in the chain
 *   phrase        0x0c   BG_PHRASES of them, per group
 *   ACC_PHRASE    0x28   PH_WORDS of them, per phrase
 *   mora          0x1c   AP_MORAS of them, per accent phrase
 *
 * Nothing here is a transcription yet. It is what one has to agree with.
 */

#ifndef PROSCTRL_H
#define PROSCTRL_H

#include <stdint.h>

/* ---- the class's own three fields ----------------------------------- */

/* All the constructor does is clear them. */
#define PC_MODE         0x00      /* int32, one or two, out of ENVPARAMS */
#define PC_UNREAD_04    0x04      /* int32, nothing reads it */
#define PC_ARG          0x08      /* int32, what GenerateESPR was handed */
#define PC_BYTES        0x0c

/* ---- a breath group, as this class holds one ------------------------ */

#define BG_PHRASE       0x00      /* phrase *, BG_PHRASES of them */
#define BG_PAUSE        0x04      /* int16, from IG_PAUSE */
#define BG_LEVEL        0x06      /* uint8, from IG_LEVEL, and then kept in
                                     step as codes are added and dropped */
#define BG_PHRASES      0x07      /* uint8, from IG_PHRASES */
#define BG_KIND         0x08      /* uint8, from IG_KIND */
#define BG_SIZE         0x0c

/* ---- one phrase of a group ------------------------------------------ */

#define PH_MORAS        0x00      /* uint8, from IH_KANA_LEN */
#define PH_FLAG         0x01      /* uint8, from IH_FLAG */
#define PH_FIRST        0x02      /* uint8, from IH_FIRST */
#define PH_WORDS        0x03      /* uint8, from IH_COUNT */
#define PH_AT4          0x04      /* int16, from IH_AT66 */
#define PH_WORD         0x08      /* ACC_PHRASE *, PH_WORDS of them */
#define PH_SIZE         0x0c

/* ---- one accent phrase ---------------------------------------------- */

#define AP_CODES        0x00      /* uint8, how many codes it holds. Starts as
                                     the phrase's IH_LEN and is kept in step as
                                     the copy inserts and drops codes. */
#define AP_CODE         0x01      /* uint8 [], the codes of the reading as this
                                     class rewrites them */
#define AP_MORAS        0x1a      /* uint8, from IH_MORAS, and the index of the
                                     last code: it is read back as one */
#define AP_LONG         0x1c      /* int16, from IH_F */
#define AP_MORA_N       0x1e      /* uint8, how many of the moras below */
#define AP_MORA         0x20      /* mora *, AP_MORA_N of them */
#define AP_LAST         0x24      /* uint8, the phrase's flag on the last
                                     accent phrase of it and nought on the
                                     others */
#define AP_HEAD         0x25      /* uint8, from IH_A */
#define AP_LEN          0x26      /* uint8, from IH_MORAS again, kept as a
                                     second copy that is decremented with
                                     AP_MORAS rather than instead of it */
#define AP_PITCH        0x27      /* uint8, from IH_PITCH */
#define AP_SIZE         0x28

/* ---- one mora ------------------------------------------------------- */

#define MO_CODES        0x00      /* uint8, how many codes it holds */
#define MO_CODE         0x01      /* uint8 [], and the codes. WriteGokiInfo
                                     reads each as eight times a pitch level
                                     plus a phoneme, so one byte is both. */
#define MO_KIND         0x1a      /* int16, from IH_F. GetGokiInfoToWrite
                                     switches on it over one to ten, so it is
                                     what kind of mora this is rather than a
                                     length. */
#define MO_SIZE         0x1c

/* ---- what the ESPR text is made of ---------------------------------- */

/* The escape a user index becomes, one per index, and how long it is. IBM
   works the length out with a strlen in a static initialiser; it is four. */
#define PC_USER_IDX     "`ui "
#define PC_USER_IDX_LEN 4

/* The pitch pair written where a mora wants one and has none of its own. */
#define PC_DUMMY_F0     "(1,1)"

/* And the two the head of a breath group takes, by whether it has a body
   pitch of its own to state. */
#define PC_F0_TWO       "(p1, t1) "
#define PC_F0_THREE     "(p1, b1, t1) "

#endif
