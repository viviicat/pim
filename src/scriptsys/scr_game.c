#include "lua/lua.h"
#include "lua/lualib.h"
#include "lua/lauxlib.h"
#include "common/time.h"
#include "scr_game.h"
#include "scriptsys/script.h"
#include "common/console.h"
#include "common/stringutil.h"
#include "containers/sdict.h"
#include "allocator/allocator.h"
#include "scr_log.h"

static StrDict sm_update_handlers;

static int scr_func_start_update(lua_State* L)
{
	lua_pushvalue(L, 1);
	i32 refId = luaL_ref(L, LUA_REGISTRYINDEX);
	if (refId == LUA_REFNIL)
	{
		ASSERT(false);
		return 0;
	}

	lua_Debug ar;
	lua_getstack(L, 1, &ar);
	lua_getinfo(L, "S", &ar);

	const char* scrPath = ar.source;

	Script_RunData runData =
	{
		.path = scrPath,
		.started = Time_Now(),
		.ref_id = refId,
	};

	char name[PIM_PATH];
	SPrintf(ARGS(name), "%s#%i", scrPath, refId);
	Con_Logf(LogSev_Verbose, "script", "starting script activity %s", name);

	if (lua_getfield(L, 1, "start") != LUA_TNIL)
	{
		lua_pushvalue(L, 1); // self arg
		if (lua_pcall(L, 1, 0, 0) != LUA_OK)
		{
			Con_Logf(LogSev_Error, "script", "in start_update: %s", lua_tostring(L, -1));
			lua_pop(L, 1); // error
			return 0;
		}
	}
	else
	{
		lua_pop(L, 1); // nil field
	}

	lua_pushstring(L, name);
	lua_setfield(L, 1, "__update_ref"); // set table.__update_ref for later unregistering

	StrDict_Add(&sm_update_handlers, name, &runData);

	return 0;
}

void scr_remove_update_handler(lua_State* L, const char* name, Script_RunData* data)
{
	Con_Logf(LogSev_Verbose, "script", "stopping script activity %s", name);

	lua_rawgeti(L, LUA_REGISTRYINDEX, data->ref_id);
	if (lua_getfield(L, 1, "stop") != LUA_TNIL)
	{
		lua_pushvalue(L, 1); // self arg
		if (lua_pcall(L, 1, 0, 0) != LUA_OK)
		{
			Con_Logf(LogSev_Error, "script", "in remove_update: %s", lua_tostring(L, -1));
			lua_pop(L, 1); // error
		}
	}
	else
	{
		lua_pop(L, 1); // nil field
	}
	lua_pop(L, 1); // table

	ASSERT(StrDict_Rm(&sm_update_handlers, name, NULL));
	luaL_unref(L, LUA_REGISTRYINDEX, data->ref_id);
}

static int scr_func_stop_update(lua_State* L)
{
	lua_getfield(L, 1, "__update_ref");
	const char* name = lua_tostring(L, 2);

	Script_RunData data;
	StrDict_Get(&sm_update_handlers, name, &data);

	scr_remove_update_handler(L, name, &data);
	return 0;
}

void scr_game_init(lua_State* L)
{
	LUA_LIB(L,
		LUA_FN(start_update),
		LUA_FN(stop_update));

	Script_RegisterLib(L, "Game", ScrLib_Global);

	StrDict_New(&sm_update_handlers, sizeof(Script_RunData), EAlloc_Perm);
}

void scr_game_shutdown(lua_State* L)
{
	const char** keys = sm_update_handlers.keys;
	Script_RunData* datas = sm_update_handlers.values;
	for (u32 i = 0; i < sm_update_handlers.width; i++)
	{
		if (!keys[i])
		{
			continue;
		}

		scr_remove_update_handler(L, keys[i], &datas[i]);
	}

	StrDict_Del(&sm_update_handlers);
}

void scr_game_update(lua_State* L)
{
	const char** keys = sm_update_handlers.keys;
	Script_RunData* datas = sm_update_handlers.values;

	for (u32 i = 0; i < sm_update_handlers.width; i++)
	{
		if (!keys[i])
		{
			continue;
		}

		u64 start = Time_Now();

		Script_RunData* data = &datas[i];

		lua_rawgeti(L, LUA_REGISTRYINDEX, data->ref_id);
		lua_getfield(L, 1, "update");
		lua_pushvalue(L, 1); // self arg
		if (lua_pcall(L, 1, 0, 0) != LUA_OK)
		{
			Con_Logf(LogSev_Error, "script", "in update: %s", lua_tostring(L, -1));
			lua_pop(L, 1); // error, table
			scr_remove_update_handler(L, keys[i], data);
		}
		lua_pop(L, 1); // table

		// Profiling info
		u64 end = Time_Now();
		float delta = (float)Time_Milli(end - start);
		data->profile_durations[data->profile_offset] = delta;
		data->profile_offset = (data->profile_offset + 1) % NUM_PROFILE_SAMPLES;
		if (delta > data->profile_max)
		{
			data->profile_max = delta;
		}
	}
}

StrDict scr_game_get_running(void)
{
	return sm_update_handlers;
}
