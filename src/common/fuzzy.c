#include "common/fuzzy.h"
#include "common/stringutil.h"
#include "allocator/allocator.h"

// Adapted from pseudocode on https://en.wikipedia.org/wiki/Levenshtein_distance
// Maximum length allowed is MAX_LEVENSHTEIN
i32 levenshtein_dist(const char* a, const char* b)
{
	ASSERT(a);
	ASSERT(b);

	i32 sourceLen = StrLen(a);
	i32 targetLen = StrLen(b);

	ASSERT(sourceLen <= MAX_LEVENSHTEIN); // Intentionally requiring an extra slot
	ASSERT(targetLen <= MAX_LEVENSHTEIN); 

	sourceLen = pim_min(sourceLen, MAX_LEVENSHTEIN); // truncating for safety
	targetLen = pim_min(targetLen, MAX_LEVENSHTEIN);

	i32 v0_storage[MAX_LEVENSHTEIN + 1];
	i32 v1_storage[MAX_LEVENSHTEIN + 1];

	i32* v0 = v0_storage;
	i32* v1 = v1_storage;

	for (i32 i = 0; i < targetLen + 1; i++)
	{
		v0[i] = i;
	}

	for (i32 i = 0; i < sourceLen; i++)
	{
		v1[0] = i + 1;

		for (i32 j = 0; j < targetLen; j++)
		{
			// Diverging from the offical implementation to bias in favor of insertion
			i32 deletionCost = v0[j + 1] + 2;
			i32 insertionCost = v1[j] + 1;

			i32 substitutionCost;
			if (a[i] == b[j])
			{
				substitutionCost = v0[j];
			}
			else
			{
				substitutionCost = v0[j] + 2;
			}

			v1[j + 1] = pim_min(pim_min(deletionCost, insertionCost), substitutionCost);
		}

		i32* temp = v0;
		v0 = v1;
		v1 = temp;
	}

	return v0[targetLen];
}

StrList StrList_FindFuzzy(const StrList* list, const char* key, u32 max_fuzz, u32* out_fuzz)
{
	u32 min = -1;
	i32 iMin = -1;
	StrList matches;
	StrList_New(&matches, EAlloc_Temp);
	for (i32 i = 0; i < list->count; i++)
	{
		u32 val = levenshtein_dist(key, list->ptr[i]);
		if (val > max_fuzz)
		{
			continue;
		}

		if (val < min)
		{
			StrList_Clear(&matches);
			min = val;
			iMin = i;
		}
		else if (val != min)
		{
			continue;
		}

		StrList_Add(&matches, list->ptr[i]);
	}

	*out_fuzz = min;
	return matches;
}

StrList StrDict_FindFuzzy(const StrDict* dict, const char* key, u32 max_fuzz, u32* out_fuzz)
{
	u32 min = -1;
	i32 iMin = -1;

	StrList matches;
	StrList_New(&matches, EAlloc_Temp);

	for (u32 i = 0; i < dict->width; i++)
	{
		if (!dict->keys[i])
		{
			continue;
		}

		u32 val = levenshtein_dist(key, dict->keys[i]);
		if (val > max_fuzz)
		{
			continue;
		}

		if (val < min)
		{
			StrList_Clear(&matches);
			min = val;
			iMin = i;
		}
		else if (val != min)
		{
			continue;
		}

		StrList_Add(&matches, dict->keys[i]);
	}

	*out_fuzz = min;
	return matches;
}
