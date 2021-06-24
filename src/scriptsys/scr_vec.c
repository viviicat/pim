#include "lua/lua.h"
#include "lua/lualib.h"
#include "lua/lauxlib.h"
#include "math/types.h"
#include "math/float4_funcs.h"
#include "script.h"
#include "scr_vec.h"

#define SCR_FLOAT4_MT "scr_float4"

pim_inline static float4 VEC_CALL scr_check_vec(lua_State* L)
{
	return *(float4*)luaL_checkudata(L, 1, SCR_FLOAT4_MT);
}

pim_inline static i32 scr_func_tostring(lua_State* L)
{
	float4 vec = scr_check_vec(L);
	lua_pushfstring(L, "<%f, %f, %f, %f>", vec.x, vec.y, vec.z, vec.w);
	return 1;
}

pim_inline static i32 scr_func_sum(lua_State* L)
{
	float4 vec = scr_check_vec(L);
	lua_pushnumber(L, f4_sum(vec));
	return 1;
}

pim_inline static i32 scr_func_sum3(lua_State* L)
{
	float4 vec = scr_check_vec(L);
	lua_pushnumber(L, f4_sum3(vec));
	return 1;
}

pim_inline static i32 scr_func_length4(lua_State* L)
{
	float4 vec = scr_check_vec(L);
	lua_pushnumber(L, f4_length4(vec));
	return 1;
}

pim_inline static i32 scr_func_length3(lua_State* L)
{
	float4 vec = scr_check_vec(L);
	lua_pushnumber(L, f4_length3(vec));
	return 1;
}

pim_inline static i32 scr_func_lengthsq4(lua_State* L)
{
	float4 vec = scr_check_vec(L);
	lua_pushnumber(L, f4_lengthsq4(vec));
	return 1;
}

pim_inline static i32 scr_func_lengthsq3(lua_State* L)
{
	float4 vec = scr_check_vec(L);
	lua_pushnumber(L, f4_lengthsq3(vec));
	return 1;
}

static i32 scr_func_vec(lua_State* L)
{
	// Can either be separated args, or a table with the fields
	scr_push_vec(L, scr_check_vec_or_args(L, 1));
	return 1;
}

static i32 scr_func_getters(lua_State* L)
{
	float4 vec = scr_check_vec(L);
	const char* getter = luaL_checkstring(L, 2);
	if (!getter || getter[1]) // if 0 or longer than 1
	{
		return 0;
	}
	switch (getter[0])
	{
	case 'x': lua_pushnumber(L, vec.x); break;
	case 'y': lua_pushnumber(L, vec.y); break;
	case 'z': lua_pushnumber(L, vec.z); break;
	case 'w': lua_pushnumber(L, vec.w); break;
	default: return 0;
	}
	return 1;
}

static const luaL_Reg sm_f4_metameth[] = {
	{ "__index", scr_func_getters },
	{ "__tostring", scr_func_tostring },
	{ 0 }
};

/// Create a metatable for the vec4 operations.
static void scr_f4_createmeta(lua_State* L)
{
	luaL_newmetatable(L, SCR_FLOAT4_MT);
	luaL_setfuncs(L, sm_f4_metameth, 0);
	lua_pop(L, 1);
}


static lua_Number scr_checknumberfield(lua_State* L, i32 pos, const char* field)
{
	lua_getfield(L, pos, field);
	i32 isNum;
	lua_Number num = lua_tonumberx(L, pos + 1, &isNum);
	if (!isNum)
	{
		luaL_error(L, "required field %s was not a number", field);
	}

	lua_pop(L, 1);
	return num;
}

void VEC_CALL scr_push_vec(lua_State* L, float4 vec)
{
	float4* udata_vec = lua_newuserdata(L, sizeof(float4));
	*udata_vec = vec;
	luaL_setmetatable(L, SCR_FLOAT4_MT);
}

float4 scr_check_vec_or_args(lua_State* L, i32 pos)
{
	// Can either be separated args, or a table with the fields
	float4* test;
	if ((test = luaL_testudata(L, pos, SCR_FLOAT4_MT)))
	{
		return *test;
	}
	else
	{
		float4 val;
		val.x = (float)luaL_checknumber(L, pos);
		val.y = (float)luaL_checknumber(L, pos + 1);
		val.z = (float)luaL_checknumber(L, pos + 2);
		val.w = (float)luaL_optnumber(L, pos + 3, 0);
		return val;
	}
}

void scr_vec_init(lua_State* L)
{
	LUA_LIB(L,
		LUA_FN(sum),
		LUA_FN(sum3),
		LUA_FN(length4),
		LUA_FN(length3),
		LUA_FN(lengthsq4),
		LUA_FN(lengthsq3),
		LUA_FN(vec));

	Script_RegisterLib(L, "vec", ScrLib_Import);

	scr_f4_createmeta(L);
}

void scr_vec_shutdown(lua_State* L)
{
}