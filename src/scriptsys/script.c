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
#include "allocator/allocator.h"
#include "common/profiler.h"
#include "containers/strlist.h"
#include "containers/sdict.h"

static lua_State* L;

static StrList sm_script_paths;

static void scr_locate_scripts_recursive(char* dir, i32 len)
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

			scr_locate_scripts_recursive(dir, subLen);
		}
		else if (fndData.attrib & ~(FAF_System | FAF_Hidden) && IEndsWith(ARGS(fndData.name), ".lua"))
		{
			char filePath[PIM_PATH];
			StrCpy(ARGS(filePath), dir);
			i32 iCat = len - 1; // remove star
			i32 l = SPrintf(&filePath[iCat], PIM_PATH - iCat, fndData.name);
			NullTerminate(filePath, PIM_PATH, iCat + l - 4);

			StrList_Add(&sm_script_paths, &filePath[NELEM(SCRIPT_DIR) - 1]);
		}
	}
}

static void scr_locate_scripts()
{
	StrList_Clear(&sm_script_paths);

	char path[PIM_PATH];
	i32 len = SPrintf(ARGS(path), "%s*", SCRIPT_DIR);
	StrPath(ARGS(path));
	scr_locate_scripts_recursive(path, len);
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
	StrList_New(&sm_script_paths, EAlloc_Perm);

	scr_locate_scripts();

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
	if (scr_game_get_running().count <= 0)
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

ProfileMark(pm_gui_update, ScriptSys_Gui)
void ScriptSys_Gui(bool* pEnabled)
{
	ProfileBegin(pm_gui_update);

	igSetNextWindowSize((ImVec2) { 250, 440 }, ImGuiCond_FirstUseEver);
	if (igBegin("ScriptSystem", pEnabled, ImGuiWindowFlags_MenuBar))
	{
		static i32 iSelected = 0;
		if (igBeginMenuBar())
		{
			if (igBeginMenu("Run...", true))
			{
				for (i32 i = 0; i < sm_script_paths.count; i++)
				{
					const char* path = sm_script_paths.ptr[i];
					if (igMenuItemBool(path, NULL, false, true))
					{
						ScriptSys_Exec(path);
						iSelected = INT_MAX; // trigger selecting last item
					}
				}

				igEndMenu();
			}

			igEndMenuBar();
		}

		Script_RunData* selected = NULL;
		const char* selectedDisplayName = "";
		const char* selectedFullName = "";
		{
			igBeginChildStr("top pane", (ImVec2) { 0, igGetFrameHeightWithSpacing() - 175 }, true, 0);

			StrDict running_scripts = scr_game_get_running();
			i32* indices = StrDict_Sort(&running_scripts, SDictStrCmp, NULL);
			Script_RunData* datas = running_scripts.values;

			if (running_scripts.count <= 0)
			{
				iSelected = -1;
			}
			else if (iSelected < 0)
			{
				iSelected = 0;
			}
			else if (iSelected >= (i32)running_scripts.count)
			{
				iSelected = running_scripts.count - 1;
			}

			for (u32 i = 0; i < running_scripts.count; i++)
			{
				i32 index = indices[i];

				const char* fullName = running_scripts.keys[index];
				const char* displayName = fullName;
				if (IStartsWith(fullName, PIM_PATH, "@script\\"))
				{
					displayName += NELEM("@script\\") - 1;
				}

				if (iSelected == i)
				{
					selected = &datas[index];
					selectedFullName = fullName;
					selectedDisplayName = displayName;
				}

				if (igSelectableBool(displayName, iSelected == i, ImGuiSelectableFlags_SelectOnClick, (ImVec2) { 0 }))
				{
					iSelected = i;
				}
			}
			Mem_Free(indices);
			igEndChild();
		}

		{
			igBeginGroup();

			igBeginChildStr("item view", (ImVec2) { 0, 0 }, false, 0);

			if (selected)
			{
				if (igExButton("Stop"))
				{
					scr_remove_update_handler(L, selectedFullName, selected);
				}

				igExSameLine();

				if (igExButton(selected->paused ? "Play" : "Pause"))
				{
					selected->paused = !selected->paused;
				}

				igExSameLine();
			}

			igText(iSelected >= 0 ? selectedDisplayName : "No active scripts");

			if (selected)
			{
				if (igBeginTable("table props", 1, 0, (ImVec2) { 0 }, 0))
				{
					igTableNextColumn();
					igLabelText("Run secs", "%f", Time_Sec(Time_Now() - selected->started));

					igEndTable();
				}

				igPlotHistogramFloatPtr("Profiling",
					selected->profile_durations, NUM_PROFILE_SAMPLES,
					selected->profile_offset, NULL, 0, selected->profile_max, (ImVec2) { 0, 80 }, sizeof(float));
			}

			igEndChild();

			igEndGroup();
		}
	}
	igEnd();

	ProfileEnd(pm_gui_update);
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

	StrPath(ARGS(path));

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


