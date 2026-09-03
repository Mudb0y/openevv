/* A filter, the thing that stands between the caller's text and the engine.
 *
 * The published interface is a plain C entry point in a library of the
 * caller's own: it is handed a number saying which of two things is wanted
 * and answers with a pointer. Number seven asks for the filter object
 * itself, whose methods are below; number eight asks for a function that
 * says whether a given string is something this filter would want to look
 * at. `eciRegisterFilter' takes that entry point and the manager in
 * src/eci/ssml/eci_filtermanager.c does the rest.
 *
 * The order of the methods below is IBM's and cannot be rearranged. It is
 * the order a filter compiled against the published headers puts them in,
 * so a filter written for the original engine and this one have to agree
 * on it slot for slot.
 *
 * src/eci/ssml/eci_filter.c is the base every filter inherits, which does nothing
 * but remember whether it is on. src/eci/ssml/eci_ssmlfilter.c is the one filter
 * IBM shipped.
 */

#ifndef ECI_FILTER_H
#define ECI_FILTER_H

#include <stdint.h>
#include "evv_abi.h"

typedef struct Filter Filter;

typedef struct FilterVtbl {
    THIS int32_t (*filterText)(Filter *self, const char *text, char **out,
                               int8_t force);
    THIS int32_t (*activateFilter)(Filter *self);
    THIS int32_t (*deactivateFilter)(Filter *self);
    THIS int32_t (*deleteFilter)(Filter *self);
    THIS int8_t  (*isActive)(Filter *self);
    THIS int32_t (*updateFilter)(Filter *self, const char *a, const char *b);
    THIS void    (*setEnvironment)(Filter *self, void *env);
    THIS char   *(*getFilterDescription)(Filter *self);
    THIS int32_t (*getFilterLanguage)(Filter *self);
    THIS int32_t (*getFilterVersion)(Filter *self, int32_t *out);
    THIS char  **(*getFilterDependencies)(Filter *self);
    THIS char   *(*filterSSMLText)(Filter *self, const char *text,
                                   int32_t length);
} FilterVtbl;

struct Filter {
    const FilterVtbl *vt;
    int8_t            active;
};

/* What a caller is told about a registered filter, and what it fills in
   for the manager to key on. The name is the filter's own description and
   the language is what the filter answers when asked; both are written by
   `registerFilter' rather than by the caller. */
typedef struct ECIFilterAttrib {
    char    eciFilterName[80];
    int32_t language;
} ECIFilterAttrib;

/* The two numbers an entry point answers to. */
#define FILTER_INTERFACE_OBJECT 7
#define FILTER_INTERFACE_USABLE 8

/* The entry point itself, and the second thing it can hand back.
 *
 * The pointer is stdcall because that is the type the published headers
 * declare, so a filter in a library of somebody else's has to be. IBM's
 * own SSML entry point is compiled cdecl and so disagrees with the type it
 * is passed as; the arguments are read correctly either way and the eight
 * bytes are recovered by the frame pointer, which is why the original gets
 * away with it. Ours is stdcall, as declared. */
typedef STDCALL int (*GetFilterObjectFn)(uint32_t idInterface, void **out);
typedef int (*FilterUsableFn)(const char *text);

/* What a filter call answers with. The numbers are IBM's; the names are
   ours, since the enumeration was never published in a header this
   extraction has. */
#define FILTER_OK             0
#define FILTER_NOT_REGISTERED 5   /* or already, depending on which way */
#define FILTER_REFUSED        6
#define FILTER_NO_PARSER      2
#define FILTER_NO_STATE       3
#define FILTER_NO_HANDLE      4

/* And what the manager answers when it was asked about something it has
   not got. */
#define FILTER_NOT_FOUND      (-3)

/* The one filter this engine carries, in src/eci/ssml/eci_ssmlfilter.c.
 *
 * It is not called `ssmlFilterGetObject' here, which is the name a caller
 * asks the library for. That name belongs to the wrapper in
 * lib/eci_api.c, because a `dllexport' only exports where the function is
 * defined and this file compiles for Linux as well. */
extern STDCALL int ssml_getFilterObject(uint32_t idInterface, void **out);
extern int isSSMLFilterUsable(const char *text);

#endif
