/* What the XML scanner's two halves share.
 *
 * src/eci_xmltok_tables.c is the automaton, lifted out of IBM's object by
 * tools/lift-xmltok.py; src/eci_xmltok.c is the skeleton that walks it and
 * the actions IBM's grammar attached to each rule. Only the tables cross
 * between them.
 */

#ifndef ECI_XMLTOK_H
#define ECI_XMLTOK_H

#include <stdint.h>

extern const int16_t yy_acclist[104];
extern const int16_t yy_accept[168];
extern const int32_t yy_ec[256];
extern const int32_t yy_meta[26];
extern const int16_t yy_base[180];
extern const int16_t yy_def[180];
extern const int16_t yy_nxt[272];
extern const int16_t yy_chk[271];

#endif
