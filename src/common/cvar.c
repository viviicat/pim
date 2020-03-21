#include "common/cvar.h"

#include "common/hashstring.h"
#include "stb/stb_sprintf.h"
#include <stdlib.h>

#define kMaxCvars 256

static uint32_t ms_hashes[kMaxCvars];
static cvar_t* ms_cvars[kMaxCvars];
static int32_t ms_count;

void cvar_create(cvar_t* ptr, const char* name, const char* value)
{
    ASSERT(ptr);
    ASSERT(name);
    ASSERT(value);

    uint32_t hash = HashStr(name);
    int32_t i = HashFind(ms_hashes, ms_count, hash);
    if (i != -1)
    {
        ASSERT(ptr == ms_cvars[i]);
    }
    else
    {
        ASSERT(ms_count < kMaxCvars);
        i = ms_count++;
        ms_hashes[i] = hash;
        ms_cvars[i] = ptr;
        stbsp_snprintf(ptr->name, NELEM(ptr->name), "%s", name);
        cvar_set_str(ptr, value);
    }
}

cvar_t* cvar_find(const char* name)
{
    ASSERT(name);
    int32_t i = HashFind(ms_hashes, ms_count, HashStr(name));
    return (i == -1) ? 0 : ms_cvars[i];
}

void cvar_set_str(cvar_t* ptr, const char* value)
{
    ASSERT(ptr);
    ASSERT(value);
    stbsp_snprintf(ptr->value, NELEM(ptr->value), "%s", value);
    ptr->asFloat = (float)atof(value);
}

void cvar_set_float(cvar_t* ptr, float value)
{
    ASSERT(ptr);
    stbsp_snprintf(ptr->value, NELEM(ptr->value), "%f", value);
    ptr->asFloat = value;
}
