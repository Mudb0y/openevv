/* The base every filter inherits, which knows one thing: whether it is on.
 *
 * Nine of the twelve methods here do nothing but answer nought or a null
 * pointer, and that is not an omission. A filter is expected to override
 * what it implements and to inherit the rest, so the base has to have
 * something in every slot for a filter that implements only one thing to
 * be usable at all. The three that do something are the switch and the
 * reading of it.
 *
 * Names are prefixed and the aliases at the foot carry the real ones.
 */

#include <stdint.h>
#include "evv_abi.h"
#include "eci_filter.h"

extern const FilterVtbl vtbl_filter;

THIS Filter *filter_ctor(Filter *self)
{
    self->vt     = &vtbl_filter;
    self->active = 0;
    return self;
}

THIS void filter_dtor(Filter *self)
{
    self->vt = &vtbl_filter;
}

/* The base filters nothing: whoever asked gets nothing back rather than
   the text, which is why every filter that means to be used overrides
   this. */
THIS int32_t filter_filterText(Filter *self, const char *text, char **out,
                               int8_t force)
{
    (void)self; (void)text; (void)out; (void)force;
    return FILTER_OK;
}

THIS int32_t filter_activateFilter(Filter *self)
{
    self->active = 1;
    return FILTER_OK;
}

THIS int32_t filter_deactivateFilter(Filter *self)
{
    self->active = 0;
    return FILTER_OK;
}

THIS int8_t filter_isActive(Filter *self)
{
    return self->active;
}

THIS int32_t filter_updateFilter(Filter *self, const char *a, const char *b)
{
    (void)self; (void)a; (void)b;
    return FILTER_OK;
}

THIS int32_t filter_deleteFilter(Filter *self)
{
    (void)self;
    return FILTER_OK;
}

THIS void filter_setEnvironment(Filter *self, void *env)
{
    (void)self; (void)env;
}

THIS char *filter_getFilterDescription(Filter *self)
{
    (void)self;
    return 0;
}

THIS int32_t filter_getFilterLanguage(Filter *self)
{
    (void)self;
    return 0;
}

THIS int32_t filter_getFilterVersion(Filter *self, int32_t *out)
{
    (void)self; (void)out;
    return 0;
}

THIS char **filter_getFilterDependencies(Filter *self)
{
    (void)self;
    return 0;
}

/* The base has no such method, and the slot is here because the table has
   to be as long as the longest filter's. Nothing calls it on a base
   filter: the manager reaches it only through a filter that overrode it. */
THIS char *filter_filterSSMLText(Filter *self, const char *text,
                                 int32_t length)
{
    (void)self; (void)text; (void)length;
    return 0;
}

const FilterVtbl vtbl_filter = {
    filter_filterText,
    filter_activateFilter,
    filter_deactivateFilter,
    filter_deleteFilter,
    filter_isActive,
    filter_updateFilter,
    filter_setEnvironment,
    filter_getFilterDescription,
    filter_getFilterLanguage,
    filter_getFilterVersion,
    filter_getFilterDependencies,
    filter_filterSSMLText
};

ALIAS("??_7Filter@@6B@", "vtbl_filter");
ALIAS("??0Filter@@QAE@XZ", "filter_ctor");
ALIAS("??1Filter@@QAE@XZ", "filter_dtor");
ALIAS("?filterText@Filter@@UAE?AW4ECIFilterError@@PBDPAPAD_N@Z",
      "filter_filterText");
ALIAS("?activateFilter@Filter@@UAE?AW4ECIFilterError@@XZ",
      "filter_activateFilter");
ALIAS("?deactivateFilter@Filter@@UAE?AW4ECIFilterError@@XZ",
      "filter_deactivateFilter");
ALIAS("?deleteFilter@Filter@@UAE?AW4ECIFilterError@@XZ",
      "filter_deleteFilter");
ALIAS("?isActive@Filter@@UAE_NXZ", "filter_isActive");
ALIAS("?updateFilter@Filter@@UAE?AW4ECIFilterError@@PBD0@Z",
      "filter_updateFilter");
ALIAS("?setEnvironment@Filter@@UAEXPAVECIEnvironment@@@Z",
      "filter_setEnvironment");
ALIAS("?getFilterDescription@Filter@@UAEPADXZ",
      "filter_getFilterDescription");
ALIAS("?getFilterLanguage@Filter@@UAE?AW4ECILanguageDialect@@XZ",
      "filter_getFilterLanguage");
ALIAS("?getFilterVersion@Filter@@UAEHQAJ@Z", "filter_getFilterVersion");
ALIAS("?getFilterDependencies@Filter@@UAEPAPADXZ",
      "filter_getFilterDependencies");
