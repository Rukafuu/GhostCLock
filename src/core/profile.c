#include "ghostclock/core.h"

#include <string.h>

gc_result gc_profile_parse(const char *value, gc_profile *profile)
{
    if (value == NULL || profile == NULL) {
        return GC_ERROR_INVALID_ARGUMENT;
    }

    if (strcmp(value, "balanced") == 0) {
        *profile = GC_PROFILE_BALANCED;
        return GC_OK;
    }
    if (strcmp(value, "gaming") == 0) {
        *profile = GC_PROFILE_GAMING;
        return GC_OK;
    }
    if (strcmp(value, "development") == 0) {
        *profile = GC_PROFILE_DEVELOPMENT;
        return GC_OK;
    }

    return GC_ERROR_INVALID_ARGUMENT;
}

gc_priority gc_profile_target_priority(gc_profile profile)
{
    switch (profile) {
    case GC_PROFILE_GAMING:
        return GC_PRIORITY_HIGH;
    case GC_PROFILE_BALANCED:
    case GC_PROFILE_DEVELOPMENT:
        return GC_PRIORITY_ABOVE_NORMAL;
    default:
        return GC_PRIORITY_UNKNOWN;
    }
}
