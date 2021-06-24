#include "script.h"
#include "common/macro.h"

PIM_C_BEGIN

typedef struct pim_alignas(16) float4_s float4;

void scr_f4_init(lua_State* L);
void scr_f4_shutdown(lua_State* L);

float4 scr_check_f4_or_args(lua_State* L, i32 pos);
void VEC_CALL scr_push_f4(lua_State* L, float4 vec);

PIM_C_END
