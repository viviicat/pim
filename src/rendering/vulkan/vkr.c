#include "rendering/vulkan/vkr.h"
#include "rendering/vulkan/vkr_compile.h"
#include "rendering/vulkan/vkr_instance.h"
#include "rendering/vulkan/vkr_debug.h"
#include "rendering/vulkan/vkr_device.h"
#include "rendering/vulkan/vkr_display.h"
#include "rendering/vulkan/vkr_swapchain.h"
#include "rendering/vulkan/vkr_shader.h"
#include "rendering/vulkan/vkr_pipeline.h"
#include "rendering/vulkan/vkr_renderpass.h"
#include "rendering/vulkan/vkr_cmd.h"
#include "rendering/vulkan/vkr_mem.h"
#include "rendering/vulkan/vkr_mesh.h"
#include "rendering/vulkan/vkr_context.h"
#include "rendering/vulkan/vkr_desc.h"
#include "rendering/vulkan/vkr_texture.h"
#include "rendering/vulkan/vkr_buffer.h"
#include "rendering/vulkan/vkr_mainpass.h"
#include "rendering/vulkan/vkr_exposurepass.h"
#include "rendering/vulkan/vkr_textable.h"
#include "rendering/vulkan/vkr_bindings.h"
#include "rendering/vulkan/vkr_megamesh.h"
#include "rendering/vulkan/vkr_sampler.h"

#include "rendering/drawable.h"
#include "rendering/mesh.h"
#include "rendering/texture.h"
#include "rendering/material.h"
#include "rendering/render_system.h"
#include "rendering/framebuffer.h"
#include "rendering/camera.h"
#include "rendering/screenblit.h"
#include "rendering/lightmap.h"
#include "rendering/constants.h"

#include "ui/cimgui_ext.h"
#include "ui/ui.h"

#include "allocator/allocator.h"
#include "common/console.h"
#include "common/profiler.h"
#include "common/time.h"
#include "common/cvar.h"
#include "common/atomics.h"
#include "containers/table.h"
#include "threading/task.h"

#include "math/float4_funcs.h"
#include "math/float4x4_funcs.h"
#include "math/quat_funcs.h"

#include <string.h>

vkrSys g_vkr;
vkrLayers g_vkrLayers;
vkrInstExts g_vkrInstExts;
vkrDevExts g_vkrDevExts;
vkrProps g_vkrProps;
vkrFeats g_vkrFeats;

static ConVar* cv_r_sun_dir;
static ConVar* cv_r_sun_col;
static ConVar* cv_r_sun_lum;

static ConVar cv_lm_upload =
{
    .type = cvart_bool,
    .name = "lm_upload",
    .value = "0",
    .desc = "upload lightmap data to GPU",
};

bool vkrSys_Init(void)
{
    memset(&g_vkr, 0, sizeof(g_vkr));

    ConVar_Reg(&cv_lm_upload);
    cv_r_sun_dir = ConVar_Find("r_sun_dir");
    cv_r_sun_col = ConVar_Find("r_sun_col");
    cv_r_sun_lum = ConVar_Find("r_sun_lum");
    ASSERT(cv_r_sun_dir);
    ASSERT(cv_r_sun_col);
    ASSERT(cv_r_sun_lum);

    bool success = true;

    if (!vkrInstance_Init(&g_vkr))
    {
        success = false;
        goto cleanup;
    }

    i32 width = 0;
    i32 height = 0;
    if (!vkrDisplay_MonitorSize(&width, &height))
    {
        success = false;
        goto cleanup;
    }
    r_width_set(width);
    r_height_set(height);
    if (!vkrDisplay_New(&g_vkr.display, width, height, "pimvk"))
    {
        success = false;
        goto cleanup;
    }
    UiSys_Init(g_vkr.display.window);

    if (!vkrDevice_Init(&g_vkr))
    {
        success = false;
        goto cleanup;
    }

    if (!vkrAllocator_New(&g_vkr.allocator))
    {
        success = false;
        goto cleanup;
    }

    if (!vkrSwapchain_New(&g_vkr.chain, &g_vkr.display, NULL))
    {
        success = false;
        goto cleanup;
    }

    if (!vkrContext_New(&g_vkr.context))
    {
        success = false;
        goto cleanup;
    }

    if (!vkrSampler_Init())
    {
        success = false;
        goto cleanup;
    }

    if (!vkrTexTable_Init())
    {
        success = false;
        goto cleanup;
    }

    if (!vkrBindings_Init())
    {
        success = false;
        goto cleanup;
    }

    if (!vkrMegaMesh_Init())
    {
        success = false;
        goto cleanup;
    }

    if (!vkrMainPass_New(&g_vkr.mainPass))
    {
        success = false;
        goto cleanup;
    }

    if (!vkrExposurePass_New(&g_vkr.exposurePass))
    {
        success = false;
        goto cleanup;
    }

    vkrSwapchain_SetupBuffers(&g_vkr.chain, g_vkr.mainPass.renderPass);

cleanup:

    if (!success)
    {
        Con_Logf(LogSev_Error, "vkr", "Failed to init vkrSys");
        vkrSys_Shutdown();
    }
    return success;
}

static void vkrUploadLightmaps(void)
{
    LmPack* pack = LmPack_Get();
    for (i32 i = 0; i < pack->lmCount; ++i)
    {
        Lightmap* lm = pack->lightmaps + i;
        Lightmap_Upload(lm);
    }
}

ProfileMark(pm_update, vkrSys_Update)
void vkrSys_Update(void)
{
    if (!g_vkr.inst)
    {
        return;
    }

    if (!vkrDisplay_IsOpen(&g_vkr.display))
    {
        return;
    }

    if (vkrDisplay_UpdateSize(&g_vkr.display))
    {
        if (vkrSwapchain_Recreate(
            &g_vkr.chain,
            &g_vkr.display,
            g_vkr.mainPass.renderPass))
        {
            r_width_set(g_vkr.display.width);
            r_height_set(g_vkr.display.height);
        }
    }
    vkrSwapchain* chain = &g_vkr.chain;
    if (!chain->handle)
    {
        return;
    }

    ProfileBegin(pm_update);

    VkFence fence = NULL;
    VkCommandBuffer cmd = NULL;
    vkrSwapchain_AcquireSync(chain, &cmd, &fence);
    vkrAllocator_Update(&g_vkr.allocator);
    vkrSampler_Update();
    vkrMegaMesh_Update();
    {
        vkrExposurePass_Setup(&g_vkr.exposurePass);
        vkrMainPass_Setup(&g_vkr.mainPass);
        vkrTexTable_Update();
        vkrBindings_Update();
    }
    {
        vkrExposurePass_Execute(&g_vkr.exposurePass);
        vkrCmdBegin(cmd);
        vkrMainPass_Execute(&g_vkr.mainPass, cmd, fence);
        vkrCmdEnd(cmd);
    }
    vkrSwapchain_Submit(chain, cmd);
    vkrSwapchain_Present(chain);

    if (ConVar_GetBool(&cv_lm_upload))
    {
        ConVar_SetBool(&cv_lm_upload, false);
        vkrUploadLightmaps();
    }

    ProfileEnd(pm_update);
}

void vkrSys_Shutdown(void)
{
    if (g_vkr.inst)
    {
        vkrDevice_WaitIdle();

        LmPack_Del(LmPack_Get());
        UiSys_Shutdown();

        vkrExposurePass_Del(&g_vkr.exposurePass);
        vkrMainPass_Del(&g_vkr.mainPass);

        vkrMegaMesh_Shutdown();
        vkrBindings_Shutdown();
        vkrTexTable_Shutdown();
        vkrSampler_Shutdown();

        vkrAllocator_Finalize(&g_vkr.allocator);

        vkrContext_Del(&g_vkr.context);
        vkrSwapchain_Del(&g_vkr.chain);
        vkrAllocator_Del(&g_vkr.allocator);
        vkrDevice_Shutdown(&g_vkr);
        vkrDisplay_Del(&g_vkr.display);
        vkrInstance_Shutdown(&g_vkr);
    }
}

void vkrSys_OnLoad(void)
{
    if (g_vkr.allocator.handle)
    {
        vkrAllocator_Update(&g_vkr.allocator);
    }
}

void vkrSys_OnUnload(void)
{
    if (g_vkr.allocator.handle)
    {
        vkrAllocator_Update(&g_vkr.allocator);
    }
}

u32 vkrSys_SyncIndex(void) { return g_vkr.chain.syncIndex; }
u32 vkrSys_SwapIndex(void) { return g_vkr.chain.imageIndex; }
u32 vkrSys_FrameIndex(void) { return Time_FrameCount(); }
