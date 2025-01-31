#pragma once

#include "rendering/vulkan/vkr.h"

PIM_C_BEGIN

VkCommandPool vkrCmdPool_New(i32 family, VkCommandPoolCreateFlags flags);
void vkrCmdPool_Del(VkCommandPool pool);
void vkrCmdPool_Reset(VkCommandPool pool, VkCommandPoolResetFlagBits flags);

bool vkrCmdAlloc_New(
    vkrQueue* queue,
    vkrQueueId queueId);
void vkrCmdAlloc_Del(vkrQueue* queue);

// ----------------------------------------------------------------------------

vkrCmdBuf* vkrCmdGet(vkrQueueId queueId);

// present
//vkrCmdBuf* vkrCmdGet_P(void);
// graphics
vkrCmdBuf* vkrCmdGet_G(void);
// compute
//vkrCmdBuf* vkrCmdGet_C(void);
// transfer
//vkrCmdBuf* vkrCmdGet_T(void);

void vkrCmdReset(vkrCmdBuf* cmdbuf);

vkrSubmitId vkrCmdSubmit(
    vkrCmdBuf* cmd,
    VkSemaphore waitSema,           // optional
    VkPipelineStageFlags waitMask,  // optional
    VkSemaphore signalSema);        // optional

vkrSubmitId vkrGetHeadSubmit(vkrQueueId queueId);

bool vkrSubmit_Poll(vkrSubmitId submit);
void vkrSubmit_Await(vkrSubmitId submit);
void vkrSubmit_AwaitAll(void);

void vkrCmdFlush(void);
void vkrCmdFlushQueueTransfers(void);

void vkrCmdBeginRenderPass(
    vkrCmdBuf* cmdbuf,
    VkRenderPass pass,
    VkFramebuffer framebuf,
    VkRect2D rect,
    i32 clearCount,
    const VkClearValue* clearValues);
void vkrCmdNextSubpass(vkrCmdBuf* cmdbuf, VkSubpassContents contents);
void vkrCmdEndRenderPass(vkrCmdBuf* cmdbuf);

void vkrCmdViewport(
    vkrCmdBuf* cmdbuf,
    VkViewport viewport,
    VkRect2D scissor);
void vkrCmdDefaultViewport(vkrCmdBuf* cmdbuf);

void vkrCmdDraw(vkrCmdBuf* cmdbuf, i32 vertexCount, i32 firstVertex);

void vkrCmdTouchBuffer(vkrCmdBuf* cmdbuf, vkrBuffer* buf);
void vkrCmdTouchImage(vkrCmdBuf* cmdbuf, vkrImage* img);
vkrSubmitId vkrBuffer_GetSubmit(const vkrBuffer* buf);
vkrSubmitId vkrImage_GetSubmit(const vkrImage* img);

bool vkrBufferState(
    vkrCmdBuf* cmdbuf,
    vkrBuffer* buf,
    const vkrBufferState_t* state);

bool vkrBufferState_HostRead(vkrCmdBuf* cmdbuf, vkrBuffer* buf);
bool vkrBufferState_HostWrite(vkrCmdBuf* cmdbuf, vkrBuffer* buf);
bool vkrBufferState_TransferSrc(vkrCmdBuf* cmdbuf, vkrBuffer* buf);
bool vkrBufferState_TransferDst(vkrCmdBuf* cmdbuf, vkrBuffer* buf);
bool vkrBufferState_UniformBuffer(vkrCmdBuf* cmdbuf, vkrBuffer* buf);
bool vkrBufferState_IndirectDraw(vkrCmdBuf* cmdbuf, vkrBuffer* buf);
bool vkrBufferState_IndirectDispatch(vkrCmdBuf* cmdbuf, vkrBuffer* buf);
bool vkrBufferState_VertexBuffer(vkrCmdBuf* cmdbuf, vkrBuffer* buf);
bool vkrBufferState_IndexBuffer(vkrCmdBuf* cmdbuf, vkrBuffer* buf);
bool vkrBufferState_FragLoad(vkrCmdBuf* cmdbuf, vkrBuffer* buf);
bool vkrBufferState_FragStore(vkrCmdBuf* cmdbuf, vkrBuffer* buf);
bool vkrBufferState_FragLoadStore(vkrCmdBuf* cmdbuf, vkrBuffer* buf);
bool vkrBufferState_ComputeLoad(vkrCmdBuf* cmdbuf, vkrBuffer* buf);
bool vkrBufferState_ComputeStore(vkrCmdBuf* cmdbuf, vkrBuffer* buf);
bool vkrBufferState_ComputeLoadStore(vkrCmdBuf* cmdbuf, vkrBuffer* buf);

bool vkrImageState(
    vkrCmdBuf* cmdbuf,
    vkrImage* img,
    const vkrImageState_t* state);

// transfer layout
bool vkrImageState_TransferSrc(vkrCmdBuf* cmdbuf, vkrImage* img);
bool vkrImageState_TransferDst(vkrCmdBuf* cmdbuf, vkrImage* img);
// read only optimal layout
bool vkrImageState_FragSample(vkrCmdBuf* cmdbuf, vkrImage* img);
bool vkrImageState_ComputeSample(vkrCmdBuf* cmdbuf, vkrImage* img);
// general layout
bool vkrImageState_FragLoad(vkrCmdBuf* cmdbuf, vkrImage* img);
bool vkrImageState_FragStore(vkrCmdBuf* cmdbuf, vkrImage* img);
bool vkrImageState_FragLoadStore(vkrCmdBuf* cmdbuf, vkrImage* img);
bool vkrImageState_ComputeLoad(vkrCmdBuf* cmdbuf, vkrImage* img);
bool vkrImageState_ComputeStore(vkrCmdBuf* cmdbuf, vkrImage* img);
bool vkrImageState_ComputeLoadStore(vkrCmdBuf* cmdbuf, vkrImage* img);
// attachment output layout
bool vkrImageState_ColorAttachWrite(vkrCmdBuf* cmdbuf, vkrImage* img);
bool vkrImageState_DepthAttachWrite(vkrCmdBuf* cmdbuf, vkrImage* img);
// present layout
bool vkrImageState_PresentSrc(vkrCmdBuf* cmdbuf, vkrImage* img);

bool vkrSubImageState(
    vkrCmdBuf* cmdbuf,
    vkrImage* img,
    const vkrSubImageState_t* state,
    i32 baseLayer, i32 layerCount,
    i32 baseMip, i32 mipCount);

void vkrCmdCopyBuffer(
    vkrCmdBuf* cmdbuf,
    vkrBuffer* src,
    vkrBuffer* dst);

void vkrCmdCopyImage(
    vkrCmdBuf* cmdbuf,
    vkrImage* src,
    vkrImage* dst,
    const VkImageCopy* region);

void vkrCmdBlitImage(
    vkrCmdBuf* cmdbuf,
    vkrImage* src,
    vkrImage* dst,
    const VkImageBlit* region);

void vkrCmdCopyBufferToImage(
    vkrCmdBuf* cmdbuf,
    vkrBuffer* src,
    vkrImage* dst,
    const VkBufferImageCopy* region);

void vkrCmdCopyImageToBuffer(
    vkrCmdBuf* cmdbuf,
    vkrImage* src,
    vkrBuffer* dst,
    const VkBufferImageCopy* region);

// limited to 64kb
void vkrCmdUpdateBuffer(
    vkrCmdBuf* cmdbuf,
    const void* src,
    i32 srcSize,
    vkrBuffer* dst);

void vkrCmdFillBuffer(
    vkrCmdBuf* cmdbuf,
    vkrBuffer* dst,
    u32 fillValue);

void vkrCmdBindDescSets(
    vkrCmdBuf* cmdbuf,
    VkPipelineBindPoint bindpoint,
    VkPipelineLayout layout,
    i32 setCount,
    const VkDescriptorSet* sets);

void vkrCmdBindPass(
    vkrCmdBuf* cmdbuf,
    const vkrPass* pass);

void vkrCmdPushConstants(
    vkrCmdBuf* cmdbuf,
    const vkrPass* pass,
    const void* src, i32 bytes);

void vkrCmdDispatch(vkrCmdBuf* cmdbuf, i32 x, i32 y, i32 z);

void vkrCmdDrawMesh(vkrCmdBuf* cmdbuf, vkrMeshId id);

PIM_C_END
