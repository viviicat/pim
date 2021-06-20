#include "lua/lua.h"
#include "lua/lualib.h"
#include "lua/lauxlib.h"
#include "scriptsys/script.h"
#include "common/console.h"
#include "common/time.h"
#include "lib_log.h"
#include "lib_time.h"

static int func_toSec(lua_State* L)
{
	lua_Integer ticks = luaL_checkinteger(L, 1);
	lua_pushnumber(L, Time_Sec(ticks));
	return 1;
}

static int func_toMilli(lua_State* L)
{
	lua_Integer ticks = luaL_checkinteger(L, 1);
	lua_pushnumber(L, Time_Milli(ticks));
	return 1;
}

static int func_toMicro(lua_State* L)
{
	lua_Integer ticks = luaL_checkinteger(L, 1);
	lua_pushnumber(L, Time_Micro(ticks));
	return 1;
}

void lib_time_init(lua_State* L)
{
	LUA_LIB(L,
		LUA_FN(toSec),
		LUA_FN(toMilli),
		LUA_FN(toMicro));
	LUA_LIB_REG(L, "Time");
}

void lib_time_update(lua_State* L)
{
	u64 frameCount = Time_FrameCount();
	u64 appStart = Time_AppStart();
	u64 frameStart = Time_FrameStart();
	u64 prevFrame = Time_PrevFrame();
	u64 now = Time_Now();
	double delta = Time_Deltaf();

	lua_getglobal(L, "Time");
		lua_pushinteger(L, frameCount);
		lua_setfield(L, -2, "frameCount");

		lua_pushinteger(L, appStart);
		lua_setfield(L, -2, "appStart");

		lua_pushinteger(L, frameStart);
		lua_setfield(L, -2, "frameStart");

		lua_pushinteger(L, prevFrame);
		lua_setfield(L, -2, "prevFrame");

		lua_pushinteger(L, now);
		lua_setfield(L, -2, "now");

		lua_pushnumber(L, delta);
		lua_setfield(L, -2, "delta");
	lua_pop(L, 1);
}
