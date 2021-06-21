#include "scriptsys/script.h"
#include "lua/lauxlib.h"
#include "lua/lualib.h"
#include "lua/lua.h"
#include "common/console.h"
#include "common/stringutil.h"
#include "common/sort.h"
#include "scr_log.h"
#include "scr_time.h"
#include "scr_cmd.h"
#include "scr_cvar.h"
#include "scr_game.h"
#include "common/profiler.h"
#include "common/time.h"
#include "ui/cimgui_ext.h"
#include "io/fnd.h"
#include "containers/sdict.h"
#include "allocator/allocator.h"
#include "common/profiler.h"

typedef struct Script_LoadData_s {
	bool running;
	u64 started;
} Script_LoadData;

static lua_State* L;

static StrDict sm_scripts;

static void scr_update_scripts_recursive(char* dir, i32 len)
{
	Finder fnd = { -1 };
	FinderData fndData;
	while (Finder_Iterate(&fnd, &fndData, dir))
	{
		if (fndData.attrib & FAF_SubDir && StrCmp(ARGS(fndData.name), "..") && StrCmp(ARGS(fndData.name), "."))
		{
			i32 subLen = len - 1; // remove star
			subLen += SPrintf(&dir[subLen], PIM_PATH - subLen, "%s/*", fndData.name);
			StrPath(dir, PIM_PATH);

			scr_update_scripts_recursive(dir, subLen);
		}
		else if (fndData.attrib & ~(FAF_System | FAF_Hidden) && IEndsWith(ARGS(fndData.name), ".lua"))
		{
			char filePath[PIM_PATH];
			StrCpy(ARGS(filePath), dir);
			i32 iCat = len - 1; // remove star
			SPrintf(&filePath[iCat], PIM_PATH - iCat, fndData.name);

			Script_LoadData data = { 0 };
			StrDict_Add(&sm_scripts, filePath, &data);
		}
	}
}

static void scr_update_scripts()
{
	for (u32 i = 0; i < sm_scripts.width; i++)
	{
		Script_LoadData* vals = sm_scripts.values;
		if (&vals[i])
		{
			Mem_Free(&vals[i]);
		}
	}

	char path[PIM_PATH];
	i32 len = SPrintf(ARGS(path), "%s*", SCRIPT_DIR);
	StrPath(ARGS(path));
	scr_update_scripts_recursive(path, len);

}

static void* scr_lua_alloc(void* ud, void* ptr, size_t osize, size_t nsize)
{
	if (nsize == 0)
	{
		Mem_Free(ptr);
		return NULL;
	}
	else
	{
		ASSERT((i32)nsize > 0);
		return Mem_Realloc(EAlloc_Script, ptr, (i32)nsize);
	}
}

void ScriptSys_Init(void)
{
	StrDict_New(&sm_scripts, sizeof(Script_LoadData), EAlloc_Perm);

	scr_update_scripts();

	L = lua_newstate(scr_lua_alloc, NULL);
	ASSERT(L);
	luaL_openlibs(L);

	scr_cmd_init(L);
	scr_log_init(L);
	scr_time_init(L);
	scr_cvar_init(L);
	scr_game_init(L);

	ScriptSys_Exec("init");
}

void ScriptSys_Shutdown(void)
{
	scr_game_shutdown(L);
	scr_cmd_shutdown(L);
	scr_log_shutdown(L);
	scr_time_shutdown(L);
	scr_cvar_shutdown(L);

	lua_close(L);
	L = NULL;
}

ProfileMark(pm_update, scrUpdate)
ProfileMark(pm_time_update, scrUpdate_Time)
ProfileMark(pm_game_update, scrUpdate_Game)
void ScriptSys_Update(void)
{
	if (scr_game_num_scripts() <= 0)
	{
		return;
	}

	ProfileBegin(pm_update);

	ProfileBegin(pm_time_update);
	scr_time_update(L);
	ProfileEnd(pm_time_update);

	ProfileBegin(pm_game_update);
	scr_game_update(L);
	ProfileEnd(pm_game_update);

	ProfileEnd(pm_update);
}

void ScriptSys_Gui(bool* pEnabled)
{
	igSetNextWindowSize((ImVec2) { 500, 440 }, ImGuiCond_FirstUseEver);
	if (igBegin("ScriptSystem", pEnabled, 0x0))
	{
		// left
		Script_LoadData selected = { 0 };
		const char* selectedName = "";
		static i32 iSelected = 0;
		{
			igBeginChildStr("left pane", (ImVec2) { 150, 0 }, true, 0);
			i32* indices = StrDict_Sort(&sm_scripts, SDictStrCmp, NULL);
			Script_LoadData* datas = sm_scripts.values;
			for (u32 i = 0; i < sm_scripts.count; i++)
			{
				i32 index = indices[i];

				const char* fullPath = sm_scripts.keys[index];
				const char* pathWithoutScriptsDir = &fullPath[sizeof(SCRIPT_DIR) - 1];

				if (iSelected == i)
				{
					selected = datas[index];
					selectedName = fullPath;
				}

				if (igSelectableBool(pathWithoutScriptsDir, iSelected == i, ImGuiSelectableFlags_SelectOnClick, (ImVec2) { 0 }))
				{
					iSelected = i;
				}
			}
			Mem_Free(indices);
			igEndChild();

			igSameLine(0, 0);
		}

		// right
		{
			igBeginGroup();

			igBeginChildStr("item view", (ImVec2) { 0, 0 }, false, 0);
			igText(selectedName);
			
			if (igBeginTable("table props", 1, 0, (ImVec2) { 0 }, 0))
			{
				igTableNextColumn();
				bool cached = selected.running;
				igCheckbox("Running", &selected.running);
				selected.running = cached;

				igTableNextColumn();
				igLabelText("Runtime", selected.running ? "%f" : "n/a", Time_Sec(Time_Now() - selected.started));

				igEndTable();
			}

			igEndChild();

			igEndGroup();
		}
	}
	igEnd();
}

void Script_RegisterLib(lua_State* L, const char* name, ScrLib_Reg regType)
{
	if (regType == ScrLib_Global)
	{
		lua_setglobal(L, name);
		return;
	}

	lua_getglobal(L, "package");
	lua_getfield(L, -1, "loaded");
	lua_pushvalue(L, 1);
	lua_setfield(L, -2, name);
	lua_pop(L, 3);
}

bool ScriptSys_Exec(const char* filename)
{
	char path[PIM_PATH];
	StrCpy(ARGS(path), SCRIPT_DIR);
	i32 len = StrCat(ARGS(path), filename);
	if (!IEndsWith(ARGS(path), ".lua"))
	{
		len = StrCat(ARGS(path), ".lua");
	}

	if (len > PIM_PATH)
	{
		Con_Logf(LogSev_Error, "script", "path is too long (> %i)", PIM_PATH);
		return false;
	}

	Con_Logf(LogSev_Verbose, "script", "executing script from %s", path);

	if (luaL_dofile(L, path) != LUA_OK)
	{
		Con_Logf(LogSev_Error, "script", "in exec: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
		return false;
	}

	return true;
}

bool ScriptSys_Eval(const char* script)
{
	if (luaL_dostring(L, script) != LUA_OK)
	{
		Con_Logf(LogSev_Error, "script", "in eval: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
		return false;
	}

	return true;
}


