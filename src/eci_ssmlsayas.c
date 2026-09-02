/* The five kinds of say-as the reader has to remember it is inside.
 *
 * `<say-as interpret-as="date">' does not turn into an annotation on its
 * own: the annotation the engine wants brackets the text, so the opening
 * tag records what kind of reading is wanted and the text handler puts the
 * annotation round whatever arrives. Which means the state has to know, and
 * has to know it a nesting deep at a time, since one say-as inside another
 * of the same kind has to close twice.
 *
 * Five counters, three methods each, and each is IBM's own object rather
 * than part of SSMLState's because that is how the objects divide. The
 * arithmetic is the same as the counters in src/eci_ssmlstate.c: going
 * below nought, or releasing what was never set, is a document that cannot
 * be read.
 */

#include <stdint.h>
#include "evv_abi.h"
#include "eci_ssml.h"
#include "eci_ssmlstate.h"

#define SAYAS(name, field)                                                \
    THIS void ss_set##name(SSMLState *s)                                  \
    {                                                                     \
        if (s->field < 0)                                                 \
            ss_setErrorSyntax(s);                                         \
        else                                                              \
            s->field++;                                                   \
    }                                                                     \
    THIS void ss_rel##name(SSMLState *s)                                  \
    {                                                                     \
        if (s->field <= 0)                                                \
            ss_setErrorSyntax(s);                                         \
        else                                                              \
            s->field--;                                                   \
    }                                                                     \
    THIS int8_t ss_is##name(SSMLState *s)                                 \
    {                                                                     \
        return s->field > 0;                                              \
    }

SAYAS(SayAsDate, sayAsDate)
SAYAS(SayAsNumber, sayAsNumber)
SAYAS(SayAsBoolean, sayAsBoolean)
SAYAS(SayAsVXMLdate, sayAsVXMLdate)
SAYAS(SayAsVXMLcurrency, sayAsVXMLcurrency)

ALIAS("?setSayAsDate@SSMLState@@QAEXXZ", "ss_setSayAsDate");
ALIAS("?relSayAsDate@SSMLState@@QAEXXZ", "ss_relSayAsDate");
ALIAS("?isSayAsDate@SSMLState@@QAE_NXZ", "ss_isSayAsDate");
ALIAS("?setSayAsNumber@SSMLState@@QAEXXZ", "ss_setSayAsNumber");
ALIAS("?relSayAsNumber@SSMLState@@QAEXXZ", "ss_relSayAsNumber");
ALIAS("?isSayAsNumber@SSMLState@@QAE_NXZ", "ss_isSayAsNumber");
ALIAS("?setSayAsBoolean@SSMLState@@QAEXXZ", "ss_setSayAsBoolean");
ALIAS("?relSayAsBoolean@SSMLState@@QAEXXZ", "ss_relSayAsBoolean");
ALIAS("?isSayAsBoolean@SSMLState@@QAE_NXZ", "ss_isSayAsBoolean");
ALIAS("?setSayAsVXMLdate@SSMLState@@QAEXXZ", "ss_setSayAsVXMLdate");
ALIAS("?relSayAsVXMLdate@SSMLState@@QAEXXZ", "ss_relSayAsVXMLdate");
ALIAS("?isSayAsVXMLdate@SSMLState@@QAE_NXZ", "ss_isSayAsVXMLdate");
ALIAS("?setSayAsVXMLcurrency@SSMLState@@QAEXXZ", "ss_setSayAsVXMLcurrency");
ALIAS("?relSayAsVXMLcurrency@SSMLState@@QAEXXZ", "ss_relSayAsVXMLcurrency");
ALIAS("?isSayAsVXMLcurrency@SSMLState@@QAE_NXZ", "ss_isSayAsVXMLcurrency");
