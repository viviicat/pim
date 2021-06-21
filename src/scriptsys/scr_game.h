#include "script.h"
#include "common/macro.h"

PIM_C_BEGIN

#define NUM_PROFILE_SAMPLES	512

typedef struct StrDict_s StrDict;

typedef struct Script_RunData {
	u64 started;
	const char* path;
	i32 ref_id;
	float profile_durations[NUM_PROFILE_SAMPLES];
	i32 profile_offset;
	float profile_max;
} Script_RunData;

void scr_game_init(lua_State* L);
void scr_game_shutdown(lua_State* L);
void scr_game_update(lua_State* L);
void scr_remove_update_handler(lua_State* L, const char* name, Script_RunData* data);
StrDict scr_game_get_running(void);

PIM_C_END
