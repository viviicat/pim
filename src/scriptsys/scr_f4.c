#include "lua/lua.h"
#include "lua/lualib.h"
#include "lua/lauxlib.h"
#include "math/types.h"
#include "math/float4_funcs.h"
#include "script.h"
#include "scr_f4.h"

#define SCR_FLOAT4_MT "scr_float4"

pim_inline static float4 VEC_CALL scr_check_f4(lua_State* L, i32 n)
{
	return *(float4*)luaL_checkudata(L, n, SCR_FLOAT4_MT);
}

pim_inline static i32 scr_func_tostring(lua_State* L)
{
	float4 vec = scr_check_f4_or_args(L, 1);
	lua_pushfstring(L, "<%f, %f, %f, %f>", vec.x, vec.y, vec.z, vec.w);
	return 1;
}

pim_inline static i32 scr_func_sum(lua_State* L)
{
	float4 vec = scr_check_f4_or_args(L, 1);
	lua_pushnumber(L, f4_sum(vec));
	return 1;
}

pim_inline static i32 scr_func_sum3(lua_State* L)
{
	float4 vec = scr_check_f4_or_args(L, 1);
	lua_pushnumber(L, f4_sum3(vec));
	return 1;
}

pim_inline static i32 scr_func_length4(lua_State* L)
{
	float4 vec = scr_check_f4_or_args(L, 1);
	lua_pushnumber(L, f4_length4(vec));
	return 1;
}

pim_inline static i32 scr_func_length3(lua_State* L)
{
	float4 vec = scr_check_f4_or_args(L, 1);
	lua_pushnumber(L, f4_length3(vec));
	return 1;
}

pim_inline static i32 scr_func_lengthsq4(lua_State* L)
{
	float4 vec = scr_check_f4_or_args(L, 1);
	lua_pushnumber(L, f4_lengthsq4(vec));
	return 1;
}

pim_inline static i32 scr_func_lengthsq3(lua_State* L)
{
	float4 vec = scr_check_f4_or_args(L, 1);
	lua_pushnumber(L, f4_lengthsq3(vec));
	return 1;
}

static i32 scr_func_new(lua_State* L)
{
	// Can either be separated args, or a table with the fields
	scr_push_f4(L, scr_check_f4_or_args(L, 1));
	return 1;
}

static const char* scr_getfieldname(lua_State* L, i32 n)
{
	static const char order[] = "xyzw";

	const char* field;

	if (lua_type(L, n) == LUA_TSTRING) // can't use lua_isstring since that returns true for numbers
	{
		field = lua_tostring(L, n);
		if (field[1])
		{
			// field length longer than 1 
			luaL_error(L, "Only the fields: [x, y, z, w] are supported for f4.");
			return 0;
		}
	}
	else
	{
		i32 isNum;
		lua_Integer index = lua_tointegerx(L, n, &isNum);
		if (!isNum)
		{
			luaL_error(L, "Expected x, y, z, w or an index from 1 - 4.");
			return 0;
		}

		if (index <= 0 || index > NELEM(order))
		{
			luaL_error(L, "Index out of range [1, 4]");
			return 0;
		}

		field = &order[index];
	}

	ASSERT(field);
	return field;
}

static i32 scr_func_getters(lua_State* L)
{
	float4 vec = scr_check_f4(L, 1);

	const char* getter = scr_getfieldname(L, 2);

	switch (getter[0])
	{
	case 'x': lua_pushnumber(L, vec.x); break;
	case 'y': lua_pushnumber(L, vec.y); break;
	case 'z': lua_pushnumber(L, vec.z); break;
	case 'w': lua_pushnumber(L, vec.w); break;
	default: luaL_error(L, "Only the fields: [x, y, z, w] are supported for f4.");
	}

	return 1;
}

static i32 scr_func_setters(lua_State* L)
{
	float4 vec = scr_check_f4(L, 1);

	const char* setter = scr_getfieldname(L, 2);
	float value = (float)luaL_checknumber(L, 3);

	switch (setter[0])
	{
	case 'x': vec.x = value; break;
	case 'y': vec.y = value; break;
	case 'z': vec.z = value; break;
	case 'w': vec.w = value; break;
	default: luaL_error(L, "Only the fields: [x, y, z, w] are supported for f4.");
	}

	return 0;
}

static i32 scr_func_add(lua_State* L)
{
	float4 lhs = scr_check_f4(L, 1);
	float4 rhs = scr_check_f4(L, 2);
	scr_push_f4(L, f4_add(lhs, rhs));
	return 1;
}

static i32 scr_func_sub(lua_State* L)
{
	float4 lhs = scr_check_f4(L, 1);
	float4 rhs = scr_check_f4(L, 2);
	scr_push_f4(L, f4_sub(lhs, rhs));
	return 1;
}

static i32 scr_func_addvs(lua_State* L)
{
	float4 lhs = scr_check_f4(L, 1);
	float rhs = (float)luaL_checknumber(L, 2);
	scr_push_f4(L, f4_addvs(lhs, rhs));
	return 1;
}

static i32 scr_func_addsv(lua_State* L)
{
	float lhs = (float)luaL_checknumber(L, 1);
	float4 rhs = scr_check_f4(L, 2);
	scr_push_f4(L, f4_addsv(lhs, rhs));
	return 1;
}

static i32 scr_func_subvs(lua_State* L)
{
	float4 lhs = scr_check_f4(L, 1);
	float rhs = (float)luaL_checknumber(L, 2);
	scr_push_f4(L, f4_subvs(lhs, rhs));
	return 1;
}

static i32 scr_func_subsv(lua_State* L)
{
	float lhs = (float)luaL_checknumber(L, 1);
	float4 rhs = scr_check_f4(L, 2);
	scr_push_f4(L, f4_subsv(lhs, rhs));
	return 1;
}

static i32 scr_func_concat(lua_State* L)
{
	luaL_tolstring(L, 1, NULL);
	luaL_tolstring(L, 2, NULL);
	lua_concat(L, 2);
	return 1;
}

static const luaL_Reg sm_f4_metameth[] = {
	{ "__index", scr_func_getters },
	{ "__newindex", scr_func_setters },
	{ "__tostring", scr_func_tostring },
	{ "__add", scr_func_add },
	{ "__sub", scr_func_sub },
	{ "__len", scr_func_length4 },
	{ "__concat", scr_func_concat },
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

void VEC_CALL scr_push_f4(lua_State* L, float4 vec)
{
	float4* udata_vec = lua_newuserdata(L, sizeof(float4));
	*udata_vec = vec;
	luaL_setmetatable(L, SCR_FLOAT4_MT);
}

float4 scr_check_f4_or_args(lua_State* L, i32 pos)
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

void scr_f4_init(lua_State* L)
{
	LUA_LIB(L,
		LUA_FN(sum),
		LUA_FN(sum3),
		LUA_FN(length4),
		LUA_FN(length3),
		LUA_FN(lengthsq4),
		LUA_FN(lengthsq3),
		LUA_FN(add),
		LUA_FN(sub),
		LUA_FN(new),
		LUA_FN(addvs),
		LUA_FN(addsv),
		LUA_FN(subvs),
		LUA_FN(subsv));

	Script_RegisterLib(L, "f4", ScrLib_Import);

	scr_f4_createmeta(L);
}

void scr_f4_shutdown(lua_State* L)
{
}