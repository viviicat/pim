#include "lua/lua.h"
#include "lua/lualib.h"
#include "lua/lauxlib.h"
#include "common/console.h"
#include "common/cvar.h"
#include "scr_cmd.h"
#include "script.h"
#include "scr_f4.h"

static i32 scr_func_get(lua_State* L)
{
	const char* name = luaL_checkstring(L, 1);

	ConVar* var = ConVar_Find(name);
	if (!var)
	{
		Con_Logf(LogSev_Warning, "script", "no cvar '%s' exists", name);
		return 0;
	}
	
	switch (var->type)
	{
	case cvart_bool:
		lua_pushboolean(L, ConVar_GetBool(var));
		break;
	case cvart_float:
		lua_pushnumber(L, ConVar_GetFloat(var));
		break;
	case cvart_int:
		lua_pushinteger(L, ConVar_GetInt(var));
		break;
	case cvart_text:
		lua_pushstring(L, ConVar_GetStr(var));
		break;
	case cvart_color:
	case cvart_vector:
	case cvart_point:
	{
		float4 vec = ConVar_GetVec(var);
		scr_push_f4(L, vec);
		break;
	}
	default:
		ASSERT(false);
		return 0;
	}

	return 1;
}

static i32 scr_func_set(lua_State* L)
{
	const char* name = luaL_checkstring(L, 1);

	ConVar* var = ConVar_Find(name);
	if (!var)
	{
		Con_Logf(LogSev_Warning, "script", "no cvar '%s' exists", name);
		return 0;
	}

	switch (var->type)
	{
	case cvart_bool:
	{
		bool val = lua_toboolean(L, 2);
		ConVar_SetBool(var, val);
		break;
	}
	case cvart_float:
	{
		float val = (float)luaL_checknumber(L, 2);
		ConVar_SetFloat(var, val);
		break;
	}
	case cvart_int:
	{
		i32 val = (i32)luaL_checkinteger(L, 2);
		ConVar_SetInt(var, val);
		break;
	}
	case cvart_text:
	{
		const char* val = luaL_checkstring(L, 2);
		ConVar_SetStr(var, val);
		break;
	}
	case cvart_color:
	case cvart_vector:
	case cvart_point:
	{
		float4 val = scr_check_f4_or_args(L, 2);
		ConVar_SetVec(var, val);

		break;
	}
	default:
		ASSERT(false);
		break;
	}
	return 0;
}

void scr_cvar_init(lua_State* L)
{
	LUA_LIB(L,
		LUA_FN(get),
		LUA_FN(set));

	Script_RegisterLib(L, "cvar", ScrLib_Import);
}

void scr_cvar_shutdown(lua_State* L)
{
}