/* SSML's attribute values, turned into the engine's annotations.
 *
 * src/eci/ssml/eci_ssmlmap.c is the whole of it. The tag handlers in
 * src/eci/ssml/eci_ssmlprocessor.c call nothing else to work out what a value
 * means.
 */

#ifndef ECI_SSMLMAP_H
#define ECI_SSMLMAP_H

#include <stdint.h>

int32_t tolowerstr(char *from, char *to, int32_t length);
int32_t romanval(char c);
void    mapToIBMph(const char *ph, char *out);
int32_t getNumber(const char *s, char *sign, int32_t *length);

int32_t mapToIBMetiPitch(const char *value, char *out);
int32_t mapToIBMetiRange(const char *value, char *out);
int32_t mapToIBMetiSpeed(const char *value, char *out);
int32_t mapToIBMetiVolume(const char *value, char *out, int32_t current);
int32_t mapToIBMtime(char *value, char *out);
int32_t mapToIBMlevel(const char *value, char *out);

int32_t mapToIBMgender(const char *value);
int32_t getVoiceGender(int32_t voice);
int32_t mapGenderToVoice(int32_t voice, int32_t gender);
int32_t mapToIBMage(const char *value, int32_t gender);
int32_t mapToIBMvariant(const char *value, int32_t voice);

int32_t mapToIBMdate(const char *format, char *out);
int32_t mapToIBMroman(const char *value, char *out, int32_t length);
int32_t mapToIBMvxmldate(const char *value, char *out, int32_t length);
int32_t mapToIBMlang(const char *value, int32_t length);

#endif
