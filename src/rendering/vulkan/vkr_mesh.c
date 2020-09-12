#include "rendering/vulkan/vkr_mesh.h"
#include "rendering/vulkan/vkr_mem.h"
#include "rendering/vulkan/vkr_buffer.h"
#include "rendering/vulkan/vkr_cmd.h"
#include "rendering/vulkan/vkr_context.h"
#include "allocator/allocator.h"
#include "common/profiler.h"
#include <string.h>

bool vkrMesh_New(
    vkrMesh* mesh,
    i32 vertCount,
    const float4* pim_noalias positions,
    const float4* pim_noalias normals,
    const float4* pim_noalias uv01,
    i32 indexCount,
    const u16* pim_noalias indices)
{
    ASSERT(mesh);
    memset(mesh, 0, sizeof(*mesh));
    if (vertCount <= 0)
    {
        ASSERT(false);
        return false;
    }
    if ((indexCount % 3))
    {
        ASSERT(false);
        return false;
    }
    if (!positions || !normals || !uv01)
    {
        ASSERT(false);
        return false;
    }

    SASSERT(sizeof(positions[0]) == sizeof(normals[0]));
    SASSERT(sizeof(positions[0]) == sizeof(uv01[0]));

    const i32 streamSize = sizeof(positions[0]) * vertCount;
    const i32 indicesSize = sizeof(indices[0]) * indexCount;
    const i32 totalSize = streamSize * vkrMeshStream_COUNT + indicesSize;

    if (!vkrBuffer_New(
        &mesh->buffer,
        totalSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        vkrMemUsage_GpuOnly, PIM_FILELINE))
    {
        ASSERT(false);
        return false;
    }

    mesh->vertCount = vertCount;
    mesh->indexCount = indexCount;

    if (!vkrMesh_Upload(mesh, vertCount, positions, normals, uv01, indexCount, indices))
    {
        ASSERT(false);
        goto cleanup;
    }

    return true;
cleanup:
    vkrMesh_Del(mesh);
    return false;
}

void vkrMesh_Del(vkrMesh* mesh)
{
    if (mesh)
    {
        vkrBuffer_Release(&mesh->buffer, NULL);
        memset(mesh, 0, sizeof(*mesh));
    }
}

bool vkrMesh_Upload(
    vkrMesh* mesh,
    i32 vertCount,
    const float4* pim_noalias positions,
    const float4* pim_noalias normals,
    const float4* pim_noalias uv01,
    i32 indexCount,
    const u16* pim_noalias indices)
{
    if (vertCount <= 0)
    {
        ASSERT(false);
        return false;
    }
    if ((indexCount % 3))
    {
        ASSERT(false);
        return false;
    }
    if (!positions || !normals || !uv01)
    {
        ASSERT(false);
        return false;
    }

    SASSERT(sizeof(positions[0]) == sizeof(normals[0]));
    SASSERT(sizeof(positions[0]) == sizeof(uv01[0]));

    const i32 streamSize = sizeof(positions[0]) * vertCount;
    const i32 indicesSize = sizeof(indices[0]) * indexCount;
    const i32 totalSize = streamSize * vkrMeshStream_COUNT + indicesSize;

    vkrBuffer stagebuf = {0};
    if (!vkrBuffer_New(
        &stagebuf,
        totalSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        vkrMemUsage_CpuOnly, PIM_FILELINE))
    {
        ASSERT(false);
        return false;
    }

    {
        u8* pim_noalias dst = vkrBuffer_Map(&stagebuf);
        ASSERT(dst);
        if (dst)
        {
            memcpy(dst, positions, streamSize);
            dst += streamSize;
            memcpy(dst, normals, streamSize);
            dst += streamSize;
            memcpy(dst, uv01, streamSize);
            dst += streamSize;
            if (indices)
            {
                memcpy(dst, indices, indicesSize);
                dst += indicesSize;
            }
        }
        vkrBuffer_Unmap(&stagebuf);
        vkrBuffer_Flush(&stagebuf);
    }

    vkrFrameContext* ctx = vkrContext_Get();
    VkCommandBuffer cmd = NULL;
    VkFence fence = NULL;
    VkQueue queue = NULL;
    vkrContext_GetCmd(ctx, vkrQueueId_Gfx, &cmd, &fence, &queue);
    vkrCmdBegin(cmd);
    vkrCmdCopyBuffer(cmd, stagebuf, mesh->buffer);
    const VkBufferMemoryBarrier barrier =
    {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask =
            VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT |
            VK_ACCESS_INDEX_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = mesh->buffer.handle,
        .offset = 0,
        .size = VK_WHOLE_SIZE,
    };
    vkrCmdBufferBarrier(
        cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        &barrier);
    vkrCmdEnd(cmd);
    vkrCmdSubmit(queue, cmd, fence, NULL, 0x0, NULL);
    vkrBuffer_Release(&stagebuf, fence);

    return true;
}
