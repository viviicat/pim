#pragma once

#include "containers/atomic_array.h"
#include "containers/pipe.h"
#include "os/atomics.h"

struct ChunkAllocator
{
    static constexpr i32 kChunkSize = 256;

    PtrPipe<kChunkSize> m_pipe;
    AArray m_free;
    AArray m_chunks;
    i32 m_itemSize;

    void Init(AllocType allocator, i32 itemSize)
    {
        ASSERT(itemSize > 0);
        m_pipe.Init();
        m_chunks.Init(allocator);
        m_free.Init(allocator);
        m_itemSize = itemSize;
    }

    void Reset()
    {
        void* ptr = m_chunks.TryPopBack();
        while (ptr)
        {
            Allocator::Free(ptr);
            ptr = m_chunks.TryPopBack();
        }
        m_chunks.Reset();
        m_free.Reset();
    }

    void PushChunk()
    {
        const i32 itemSize = m_itemSize;
        ASSERT(itemSize > 0);

        u8* pChunk = (u8*)Allocator::Alloc(m_chunks.GetAllocator(), kChunkSize * itemSize);

        m_chunks.PushBack(pChunk);

        for (i32 i = 0; i < kChunkSize; ++i)
        {
            if (!m_pipe.TryPush(pChunk))
            {
                m_free.PushBack(pChunk);
            }
            pChunk += itemSize;
        }
    }

    void* Allocate()
    {
        void* ptr = nullptr;
        while (!ptr)
        {
            ptr = m_pipe.TryPop();
            if (!ptr)
            {
                ptr = m_free.TryPopBack();
            }
            if (!ptr)
            {
                PushChunk();
            }
        }
        memset(ptr, 0, m_itemSize);
        return ptr;
    }

    void Free(void* pVoid)
    {
        if (pVoid)
        {
            if (!m_pipe.TryPush(pVoid))
            {
                m_free.PushBack(pVoid);
            }
        }
    }
};
