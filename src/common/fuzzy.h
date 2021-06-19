#pragma once

#include "common/macro.h"
#include "containers/strlist.h"
#include "containers/sdict.h"

PIM_C_BEGIN

#define MAX_LEVENSHTEIN 128

i32 levenshtein_dist(const char* a, const char* b);

StrList StrList_FindFuzzy(const StrList* list, const char* key, u32 max_fuzz, u32* out_fuzz);
StrList StrDict_FindFuzzy(const StrDict* dict, const char* key, u32 max_fuzz, u32* out_fuzz);

PIM_C_END
