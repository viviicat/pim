#include "rendering/render_system.h"

#include <sokol/sokol_gfx.h>
#include <sokol/sokol_app.h>
#include <sokol/util/sokol_imgui.h>
#include <sokol/util/sokol_gl.h>
#include "ui/imgui.h"

#include "common/time.h"
#include "components/ecs.h"
#include "components/system.h"
#include "rendering/components.h"
#include "rendering/screen.h"
#include "components/transform.h"
#include "math/vec_funcs.h"

namespace Screen
{
    static i32 ms_width;
    static i32 ms_height;

    i32 Width() { return ms_width; }
    i32 Height() { return ms_height; }

    static void Update()
    {
        Screen::ms_width = sapp_width();
        Screen::ms_height = sapp_height();
    }
};

static constexpr i32 kNumFrames = 4;
static constexpr i32 kFrameMask = kNumFrames - 1;
static constexpr i32 kDrawWidth = 320;
static constexpr i32 kDrawHeight = 240;
static constexpr i32 kTileCount = 8 * 8;
static constexpr i32 kTileWidth = kDrawWidth / 8;
static constexpr i32 kTileHeight = kDrawHeight / 8;

static int4 GetTile(i32 i)
{
    i32 x = i & 7;
    i32 y = i >> 3;
    x *= kTileWidth;
    y *= kTileHeight;
    return int4(x, y, x + kTileWidth, y + kTileHeight);
}

struct alignas(64) Framebuffer
{
    u32 buffer[kDrawWidth * kDrawHeight];

    void Set(i32 x, i32 y, float4 color)
    {
        u8 r = (u8)Clamp(color.x * 256.0f, 0.0f, 255.0f);
        u8 g = (u8)Clamp(color.y * 256.0f, 0.0f, 255.0f);
        u8 b = (u8)Clamp(color.z * 256.0f, 0.0f, 255.0f);
        u32 c = 0xff;
        c <<= 8;
        c |= b;
        c <<= 8;
        c |= g;
        c <<= 8;
        c |= r;
        buffer[y * kDrawWidth + x] = c;
    }

    void Clear()
    {
        for (u32& x : buffer)
        {
            x = 0;
        }
    }
};

static void DrawTile(Framebuffer& buffer, i32 x0, i32 x1, i32 y0, i32 y1)
{
    for (i32 y = y0; y < y1; ++y)
    {
        for (i32 x = x0; x < x1; ++x)
        {
            buffer.Set(x, y, float4(x / (f32)kDrawWidth, y / (f32)kDrawHeight, 0.0f, 1.0f));
        }
    }
}

struct DrawTask final : ITask
{
    Framebuffer* m_pBuffer;
    DrawTask() : ITask(0, kTileCount, 1) {}
    void Setup(Framebuffer& buffer)
    {
        m_pBuffer = &buffer;
    }
    void Execute(i32 begin, i32 end) final
    {
        Framebuffer& buffer = *m_pBuffer;
        for (i32 i = begin; i < end; ++i)
        {
            const int4 tile = GetTile(i);
            DrawTile(buffer, tile.x, tile.z, tile.y, tile.w);
        }
    }
};

namespace RenderSystem
{
    static sg_features ms_features;
    static sg_limits ms_limits;
    static sg_backend ms_backend;
    static i32 ms_iFrame;
    static sg_image ms_images[kNumFrames];
    static DrawTask ms_tasks[kNumFrames];
    static Framebuffer ms_buffers[kNumFrames];

    static void Init();
    static void Update();
    static void Shutdown();

    static System ms_system
    {
        "RenderSystem",
        { "InputSystem", "ECS", "TaskSystem" },
        Init,
        Update,
        Shutdown,
    };

    static void Init()
    {
        ms_iFrame = 0;
        {
            sg_desc desc = {};
            desc.mtl_device = sapp_metal_get_device();
            desc.mtl_drawable_cb = sapp_metal_get_drawable;
            desc.mtl_renderpass_descriptor_cb = sapp_metal_get_renderpass_descriptor;
            desc.d3d11_device = sapp_d3d11_get_device();
            desc.d3d11_device_context = sapp_d3d11_get_device_context();
            desc.d3d11_render_target_view_cb = sapp_d3d11_get_render_target_view;
            desc.d3d11_depth_stencil_view_cb = sapp_d3d11_get_depth_stencil_view;
            sg_setup(&desc);
            ms_features = sg_query_features();
            ms_limits = sg_query_limits();
            ms_backend = sg_query_backend();

            sg_image_desc img = {};
            img.type = SG_IMAGETYPE_2D;
            img.pixel_format = SG_PIXELFORMAT_RGBA8;
            img.width = kDrawWidth;
            img.height = kDrawHeight;
            img.usage = SG_USAGE_STREAM;
            img.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
            img.wrap_v = SG_WRAP_CLAMP_TO_EDGE;

            for (i32 i = 0; i < kNumFrames; ++i)
            {
                ms_images[i] = sg_make_image(&img);
            }
        }
        {
            sgl_desc_t desc = {};
            sgl_setup(&desc);
        }
        {
            ImGui::SetAllocatorFunctions(Allocator::ImGuiAllocFn, Allocator::ImGuiFreeFn);
            simgui_desc_t desc = {};
            simgui_setup(&desc);
        }
        Screen::Update();
    }

    static void Update()
    {
        Screen::Update();
        simgui_new_frame(Screen::Width(), Screen::Height(), Time::DeltaTimeF32());
    }

    static void Shutdown()
    {
        for (i32 i = 0; i < kNumFrames; ++i)
        {
            TaskSystem::Await(ms_tasks + i);
            sg_destroy_image(ms_images[i]);
            ms_images[i] = {};
        }

        simgui_shutdown();
        sgl_shutdown();
        sg_shutdown();
    }

    void FrameEnd()
    {
        const i32 iPrev = (ms_iFrame - 1) & kFrameMask;
        const i32 iCurrent = (ms_iFrame + 0) & kFrameMask;
        {
            TaskSystem::Await(ms_tasks + iCurrent);
            sg_image_content content = {};
            content.subimage[0][0] =
            {
                ms_buffers[iCurrent].buffer,
                sizeof(ms_buffers[iCurrent].buffer)
            };
            sg_update_image(ms_images[iCurrent], &content);
            ms_buffers[iCurrent].Clear();
            ms_tasks[iCurrent].Setup(ms_buffers[iCurrent]);
            TaskSystem::Submit(ms_tasks + iCurrent);
        }
        {
            sg_pass_action clear = {};
            sg_begin_default_pass(&clear, Screen::Width(), Screen::Height());
            sgl_viewport(0, 0, Screen::Width(), Screen::Height(), ms_features.origin_top_left);
            sgl_enable_texture();
            sgl_matrix_mode_texture();
            sgl_load_identity();
            sgl_texture(ms_images[iPrev]);
            sgl_begin_triangles();
            {
                sgl_v2f_t2f(-1.0f, -1.0f, 0.0f, 0.0f); // TL
                sgl_v2f_t2f(-1.0f, 1.0f, 0.0f, 1.0f); // BL
                sgl_v2f_t2f(1.0f, -1.0f, 1.0f, 0.0f); // TR

                sgl_v2f_t2f(1.0f, -1.0f, 1.0f, 0.0f); // TR
                sgl_v2f_t2f(-1.0f, 1.0f, 0.0f, 1.0f); // BL
                sgl_v2f_t2f(1.0f, 1.0f, 1.0f, 1.0f); // BR
            }
            sgl_end();
            sgl_draw();
            simgui_render();
            sg_end_pass();
            sg_commit();
            ++ms_iFrame;
        }
    }

    bool OnEvent(const sapp_event* evt)
    {
        return simgui_handle_event(evt);
    }
};
