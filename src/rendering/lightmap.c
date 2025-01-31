#include "rendering/lightmap.h"

#include "allocator/allocator.h"
#include "rendering/drawable.h"
#include "math/float2_funcs.h"
#include "math/int2_funcs.h"
#include "math/float4_funcs.h"
#include "math/float4x4_funcs.h"
#include "math/sdf.h"
#include "math/area.h"
#include "math/sampling.h"
#include "math/sh.h"
#include "math/sphgauss.h"
#include "common/console.h"
#include "common/sort.h"
#include "common/stringutil.h"
#include "threading/task.h"
#include "threading/mutex.h"
#include "rendering/path_tracer.h"
#include "rendering/sampler.h"
#include "rendering/mesh.h"
#include "rendering/material.h"
#include "rendering/vulkan/vkr_texture.h"
#include "rendering/vulkan/vkr_mesh.h"
#include "rendering/vulkan/vkr_textable.h"
#include "common/profiler.h"
#include "common/cmd.h"
#include "common/atomics.h"
#include "assets/crate.h"
#include "io/fstr.h"
#include <stb/stb_image_write.h>
#include <string.h>

pim_optimize;

#define CHART_SPLITS        2
#define ROW_RESET           -(1<<20)
#define kUnmappedMaterials  (MatFlag_Sky | MatFlag_Lava)
#define kMaskPadding        (1.0f)
#define kFillPadding        (2.0f)

typedef struct mask_s
{
    int2 size;
    u8* pim_noalias ptr;
} mask_t;

typedef struct chartnode_s
{
    Plane3D plane;
    Tri2D triCoord;
    float area;
    i32 drawableIndex;
    i32 vertIndex;
} chartnode_t;

typedef struct chart_s
{
    mask_t mask;
    chartnode_t* pim_noalias nodes;
    i32 nodeCount;
    i32 atlasIndex;
    int2 translation;
    float area;
} chart_t;

typedef struct atlas_s
{
    Mutex mtx;
    mask_t mask;
    i32 chartCount;
} atlas_t;

static LmPack ms_pack;
static bool ms_once;

static cmdstat_t CmdPrintLm(i32 argc, const char** argv);

LmPack* LmPack_Get(void) { return &ms_pack; }

void Lightmap_New(Lightmap* lm, i32 size)
{
    ASSERT(lm);
    ASSERT(size > 0);
    memset(lm, 0, sizeof(*lm));

    lm->size = size;

    const i32 texelcount = size * size;
    i32 probesBytes = sizeof(float4) * texelcount * kGiDirections;
    i32 positionBytes = sizeof(lm->position[0]) * texelcount;
    i32 normalBytes = sizeof(lm->normal[0]) * texelcount;
    i32 sampleBytes = sizeof(lm->sampleCounts[0]) * texelcount;
    i32 texelBytes = probesBytes + sampleBytes + positionBytes + normalBytes;
    u8* allocation = Tex_Calloc(texelBytes);

    for (i32 i = 0; i < kGiDirections; ++i)
    {
        lm->probes[i] = (float4*)allocation;
        allocation += sizeof(float4) * texelcount;
    }

    lm->position = (float3*)allocation;
    allocation += sizeof(float3) * texelcount;

    lm->normal = (float3*)allocation;
    allocation += sizeof(float3) * texelcount;

    lm->sampleCounts = (float*)allocation;
    allocation += sizeof(float) * texelcount;

    lm->slot = vkrTexTable_Alloc(
        VK_IMAGE_VIEW_TYPE_2D_ARRAY,
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        size,
        size,
        1,
        kGiDirections,
        true);

    Lightmap_Upload(lm);
}

void Lightmap_Del(Lightmap* lm)
{
    if (lm)
    {
        vkrTexTable_Free(lm->slot);
        Mem_Free(lm->probes[0]);
        memset(lm, 0, sizeof(*lm));
    }
}

void Lightmap_Upload(Lightmap* lm)
{
    ASSERT(lm);
    const i32 len = lm->size * lm->size;
    for (i32 i = 0; i < kGiDirections; ++i)
    {
        const float4* pim_noalias probes = lm->probes[i];
        vkrTexTable_Upload(lm->slot, i, probes, sizeof(probes[0]) * len);
    }
}

pim_inline i32 TexelCount(const Lightmap* lightmaps, i32 lmCount)
{
    i32 texelCount = 0;
    for (i32 i = 0; i < lmCount; ++i)
    {
        i32 lmSize = lightmaps[i].size;
        texelCount += lmSize * lmSize;
    }
    return texelCount;
}

pim_inline mask_t VEC_CALL mask_new(int2 size)
{
    i32 len = size.x * size.y;
    mask_t mask;
    mask.size = size;
    mask.ptr = Perm_Calloc(sizeof(u8) * len);
    return mask;
}

pim_inline void VEC_CALL mask_del(mask_t* mask)
{
    Mem_Free(mask->ptr);
    mask->ptr = NULL;
    mask->size.x = 0;
    mask->size.y = 0;
}

pim_inline bool VEC_CALL mask_fits(mask_t a, mask_t b, int2 b_tr)
{
    const int2 lo = b_tr;
    const int2 hi = i2_add(b.size, b_tr);
    if ((lo.x < 0) || (lo.y < 0))
    {
        return false;
    }
    if ((hi.x > a.size.x) || (hi.y > a.size.y))
    {
        return false;
    }
    const i32 astride = a.size.x;
    const i32 bstride = b.size.x;
    for (i32 ay = lo.y; ay < hi.y; ++ay)
    {
        i32 by = ay - lo.y;
        for (i32 ax = lo.x; ax < hi.x; ++ax)
        {
            i32 bx = ax - lo.x;
            i32 bi = bx + by * bstride;
            i32 ai = ax + ay * astride;
            ASSERT(bx >= 0);
            ASSERT(bx < b.size.x);
            ASSERT(by >= 0);
            ASSERT(by < b.size.y);
            if ((b.ptr[bi] & a.ptr[ai]) != 0)
            {
                return false;
            }
        }
    }
    return true;
}

pim_inline void VEC_CALL mask_write(mask_t a, mask_t b, int2 tr)
{
    for (i32 by = 0; by < b.size.y; ++by)
    {
        for (i32 bx = 0; bx < b.size.x; ++bx)
        {
            i32 bi = bx + by * b.size.x;
            if (b.ptr[bi])
            {
                i32 ax = bx + tr.x;
                i32 ay = by + tr.y;
                i32 ai = ax + ay * a.size.x;
                ASSERT(ax >= 0);
                ASSERT(ay >= 0);
                ASSERT(ax < a.size.x);
                ASSERT(ay < a.size.y);
                ASSERT(!a.ptr[ai]);
                a.ptr[ai] = b.ptr[bi];
            }
        }
    }
}

pim_inline int2 VEC_CALL tri_size(Tri2D tri)
{
    float2 hi = f2_max(f2_max(tri.a, tri.b), tri.c);
    int2 size = f2_i2(f2_ceil(hi));
    return size;
}

pim_inline bool VEC_CALL TriTest(Tri2D tri, float2 pt)
{
    return sdTriangle2D(tri.a, tri.b, tri.c, pt) <= kMaskPadding;
}

pim_inline void VEC_CALL mask_tri(mask_t mask, Tri2D tri)
{
    const int2 size = mask.size;
    for (i32 y = 0; y < size.y; ++y)
    {
        for (i32 x = 0; x < size.x; ++x)
        {
            const i32 i = x + y * size.x;
            float2 texelCenter = { x + 0.5f, y + 0.5f };
            if (TriTest(tri, texelCenter))
            {
                mask.ptr[i] = 0xff;
            }
        }
    }
}

pim_inline bool VEC_CALL mask_find(mask_t atlas, mask_t item, int2* trOut, i32 prevRow)
{
    const int2 range = i2_sub(atlas.size, item.size);
    i32 y = 0;
    if (prevRow != ROW_RESET)
    {
        y = prevRow;
    }
    for (; y < range.y; ++y)
    {
        for (i32 x = 0; x < range.x; ++x)
        {
            int2 tr = { x, y };
            if (mask_fits(atlas, item, tr))
            {
                *trOut = tr;
                return true;
            }
        }
    }
    return false;
}

pim_inline float2 VEC_CALL ProjUv(float3x3 TBN, float4 pt)
{
    float u = f4_dot3(TBN.c0, pt);
    float v = f4_dot3(TBN.c1, pt);
    return f2_v(u, v);
}

pim_inline Tri2D VEC_CALL ProjTri(float4 A, float4 B, float4 C)
{
    Plane3D plane = triToPlane(A, B, C);
    float3x3 TBN = NormalToTBN(plane.value);
    Tri2D tri;
    tri.a = ProjUv(TBN, A);
    tri.b = ProjUv(TBN, B);
    tri.c = ProjUv(TBN, C);
    return tri;
}

pim_inline chartnode_t VEC_CALL chartnode_new(
    float4 A,
    float4 B,
    float4 C,
    float texelsPerUnit,
    i32 iDrawable,
    i32 iVert)
{
    chartnode_t node = { 0 };
    node.plane = triToPlane(A, B, C);
    node.triCoord = ProjTri(A, B, C);
    node.triCoord.a = f2_mulvs(node.triCoord.a, texelsPerUnit);
    node.triCoord.b = f2_mulvs(node.triCoord.b, texelsPerUnit);
    node.triCoord.c = f2_mulvs(node.triCoord.c, texelsPerUnit);
    node.area = TriArea3D(A, B, C);
    node.drawableIndex = iDrawable;
    node.vertIndex = iVert;
    return node;
}

pim_inline void chart_del(chart_t* chart)
{
    if (chart)
    {
        mask_del(&chart->mask);
        Mem_Free(chart->nodes);
        chart->nodes = NULL;
        chart->nodeCount = 0;
    }
}

pim_inline bool VEC_CALL plane_equal(
    Plane3D lhs, Plane3D rhs, float distThresh, float minCosTheta)
{
    float dist = f1_distance(lhs.value.w, rhs.value.w);
    float cosTheta = f4_dot3(lhs.value, rhs.value);
    return (dist < distThresh) && (cosTheta >= minCosTheta);
}

pim_inline i32 plane_find(
    const Plane3D* planes,
    i32 planeCount,
    Plane3D plane,
    float distThresh,
    float degreeThresh)
{
    const float minCosTheta = cosf(degreeThresh * kRadiansPerDegree);
    for (i32 i = 0; i < planeCount; ++i)
    {
        if (plane_equal(planes[i], plane, distThresh, minCosTheta))
        {
            return i;
        }
    }
    return -1;
}

pim_inline void VEC_CALL chart_minmax(chart_t chart, float2* loOut, float2* hiOut)
{
    const float bigNum = 1 << 20;
    float2 lo = f2_s(bigNum);
    float2 hi = f2_s(-bigNum);
    for (i32 i = 0; i < chart.nodeCount; ++i)
    {
        Tri2D tri = chart.nodes[i].triCoord;
        lo = f2_min(lo, tri.a);
        hi = f2_max(hi, tri.a);
        lo = f2_min(lo, tri.b);
        hi = f2_max(hi, tri.b);
        lo = f2_min(lo, tri.c);
        hi = f2_max(hi, tri.c);
    }
    *loOut = lo;
    *hiOut = hi;
}

pim_inline float VEC_CALL chart_area(chart_t chart)
{
    float2 lo, hi;
    chart_minmax(chart, &lo, &hi);
    float2 size = f2_sub(hi, lo);
    return size.x * size.y;
}

pim_inline float VEC_CALL chart_width(chart_t chart)
{
    float2 lo, hi;
    chart_minmax(chart, &lo, &hi);
    float2 size = f2_sub(hi, lo);
    return f2_hmax(size);
}

pim_inline float VEC_CALL chart_triarea(chart_t chart)
{
    float sum = 0.0f;
    for (i32 i = 0; i < chart.nodeCount; ++i)
    {
        Tri2D tri = chart.nodes[i].triCoord;
        float area = TriArea2D(tri);
        sum += area;
    }
    return sum;
}

pim_inline float VEC_CALL chart_density(chart_t chart)
{
    float fromTri = chart_triarea(chart);
    float fromBounds = chart_area(chart);
    ASSERT(fromBounds >= (fromTri * 0.99f));// 1% overlap!
    return fromTri / f1_max(fromBounds, fromTri);
}

pim_inline float2 VEC_CALL tri_center(Tri2D tri)
{
    const float scale = 1.0f / 3;
    float2 center = f2_mulvs(tri.a, scale);
    center = f2_add(center, f2_mulvs(tri.b, scale));
    center = f2_add(center, f2_mulvs(tri.c, scale));
    return center;
}

pim_inline float2 VEC_CALL cluster_mean(const Tri2D* tris, i32 count)
{
    const float scale = 1.0f / count;
    float2 mean = f2_0;
    for (i32 i = 0; i < count; ++i)
    {
        float2 center = tri_center(tris[i]);
        mean = f2_add(mean, f2_mulvs(center, scale));
    }
    return mean;
}

pim_inline i32 cluster_nearest(const float2* means, i32 k, Tri2D tri)
{
    i32 chosen = -1;
    float chosenDist = 1 << 20;
    for (i32 i = 0; i < k; ++i)
    {
        float dist = sdTriangle2D(tri.a, tri.b, tri.c, means[i]);
        if (dist < chosenDist)
        {
            chosenDist = dist;
            chosen = i;
        }
    }
    return chosen;
}

static void chart_split(chart_t chart, chart_t* split)
{
    const i32 nodeCount = chart.nodeCount;
    const chartnode_t* nodes = chart.nodes;

    float2 means[CHART_SPLITS] = { 0 };
    float2 prevMeans[CHART_SPLITS] = { 0 };
    i32 counts[CHART_SPLITS] = { 0 };
    Tri2D* triLists[CHART_SPLITS] = { 0 };
    i32* nodeLists[CHART_SPLITS] = { 0 };
    const i32 k = CHART_SPLITS;

    // create k initial means
    Prng rng = Prng_Get();
    for (i32 i = 0; i < k; ++i)
    {
        i32 j = Prng_i32(&rng) % nodeCount;
        Tri2D tri = nodes[j].triCoord;
        means[i] = tri_center(tri);
        triLists[i] = Temp_Alloc(sizeof(Tri2D) * nodeCount);
        nodeLists[i] = Temp_Alloc(sizeof(i32) * nodeCount);
    }
    Prng_Set(rng);

    do
    {
        // reset cluster lists
        for (i32 i = 0; i < k; ++i)
        {
            counts[i] = 0;
        }

        // associate each node with nearest mean
        for (i32 i = 0; i < nodeCount; ++i)
        {
            Tri2D tri = nodes[i].triCoord;
            i32 cl = cluster_nearest(means, k, tri);

            i32 back = counts[cl]++;
            triLists[cl][back] = tri;
            nodeLists[cl][back] = i;
        }

        // update means
        for (i32 i = 0; i < k; ++i)
        {
            prevMeans[i] = means[i];
            means[i] = cluster_mean(triLists[i], counts[i]);
        }

    } while (memcmp(means, prevMeans, sizeof(means)));

    for (i32 i = 0; i < k; ++i)
    {
        chart_t ch = { 0 };
        ch.nodeCount = counts[i];
        if (ch.nodeCount > 0)
        {
            Perm_Reserve(ch.nodes, ch.nodeCount);
            const i32* nodeList = nodeLists[i];
            for (i32 j = 0; j < ch.nodeCount; ++j)
            {
                i32 iNode = nodeList[j];
                ch.nodes[j] = nodes[iNode];
            }
        }
        split[i] = ch;
    }
}

typedef struct chartmask_s
{
    Task task;
    chart_t* charts;
    i32 chartCount;
} chartmask_t;

static void ChartMaskFn(void* pbase, i32 begin, i32 end)
{
    chartmask_t* task = pbase;
    chart_t* charts = task->charts;

    for (i32 i = begin; i < end; ++i)
    {
        chart_t chart = charts[i];

        float2 lo, hi;
        chart_minmax(chart, &lo, &hi);
        lo = f2_subvs(lo, 2.0f);
        for (i32 iNode = 0; iNode < chart.nodeCount; ++iNode)
        {
            Tri2D tri = chart.nodes[iNode].triCoord;
            tri.a = f2_sub(tri.a, lo);
            tri.b = f2_sub(tri.b, lo);
            tri.c = f2_sub(tri.c, lo);
            chart.nodes[iNode].triCoord = tri;
        }
        chart_minmax(chart, &lo, &hi);
        float2 size = f2_sub(hi, lo);
        chart.area = size.x * size.y;
        hi = f2_addvs(hi, 2.0f);

        chart.mask = mask_new(f2_i2(hi));
        for (i32 iNode = 0; iNode < chart.nodeCount; ++iNode)
        {
            Tri2D tri = chart.nodes[iNode].triCoord;
            mask_tri(chart.mask, tri);
        }

        charts[i] = chart;
    }
}

static chart_t* chart_group(
    chartnode_t* pim_noalias nodes,
    i32 nodeCount,
    i32* pim_noalias countOut,
    float distThresh,
    float degreeThresh,
    float maxWidth)
{
    ASSERT(nodes);
    ASSERT(nodeCount >= 0);
    ASSERT(countOut);

    i32 chartCount = 0;
    chart_t* pim_noalias charts = NULL;
    Plane3D* pim_noalias planes = NULL;

    // assign nodes to charts by triangle plane
    for (i32 iNode = 0; iNode < nodeCount; ++iNode)
    {
        chartnode_t node = nodes[iNode];
        i32 iChart = plane_find(
            planes,
            chartCount,
            node.plane,
            distThresh,
            degreeThresh);
        if (iChart == -1)
        {
            iChart = chartCount;
            ++chartCount;
            Perm_Grow(charts, chartCount);
            Perm_Grow(planes, chartCount);
            planes[iChart] = node.plane;
        }

        chart_t chart = charts[iChart];
        i32 b = chart.nodeCount++;
        Perm_Reserve(chart.nodes, b + 1);
        chart.nodes[b] = node;
        charts[iChart] = chart;
    }

    Mem_Free(planes);
    planes = NULL;

    // split big charts
    for (i32 iChart = 0; iChart < chartCount; ++iChart)
    {
        chart_t chart = charts[iChart];
        if (chart.nodeCount > 1)
        {
            float width = chart_width(chart);
            float density = chart_density(chart);
            if ((width >= maxWidth) || (density < 0.1f))
            {
                chart_t split[CHART_SPLITS] = { 0 };
                chart_split(chart, split);
                for (i32 j = 0; j < NELEM(split); ++j)
                {
                    if (split[j].nodeCount > 0)
                    {
                        ++chartCount;
                        Perm_Reserve(charts, chartCount);
                        charts[chartCount - 1] = split[j];
                    }
                }
                chart_del(&chart);

                charts[iChart] = charts[chartCount - 1];
                --chartCount;
                --iChart;
            }
        }
    }

    // move chart to origin and create mask
    chartmask_t* task = Temp_Calloc(sizeof(*task));
    task->charts = charts;
    task->chartCount = chartCount;
    Task_Run(&task->task, ChartMaskFn, chartCount);

    *countOut = chartCount;
    return charts;
}

pim_inline i32 chart_cmp(const void* plhs, const void* prhs, void* usr)
{
    const chart_t* lhs = plhs;
    const chart_t* rhs = prhs;
    float a = lhs->area;
    float b = rhs->area;
    return ((a > b) ? 1 : 0) - ((b > a) ? 1 : 0);
}

pim_inline void chart_sort(chart_t* charts, i32 chartCount)
{
    QuickSort(charts, chartCount, sizeof(charts[0]), chart_cmp, NULL);
}

pim_inline atlas_t atlas_new(i32 size)
{
    atlas_t atlas = { 0 };
    Mutex_New(&atlas.mtx);
    atlas.mask = mask_new(i2_s(size));
    return atlas;
}

pim_inline void atlas_del(atlas_t* atlas)
{
    if (atlas)
    {
        Mutex_Del(&atlas->mtx);
        mask_del(&atlas->mask);
        memset(atlas, 0, sizeof(*atlas));
    }
}

static bool atlas_search(
    atlas_t* pim_noalias atlases,
    i32 atlasCount,
    chart_t* pim_noalias chart,
    i32* pim_noalias prevAtlas,
    i32* pim_noalias prevRow)
{
    int2 tr;
    for (i32 i = *prevAtlas; i < atlasCount; ++i)
    {
        atlas_t* pim_noalias atlas = &atlases[i];
    retry:
        if (mask_find(atlas->mask, chart->mask, &tr, *prevRow))
        {
            *prevAtlas = i;
            *prevRow = tr.y;
            Mutex_Lock(&atlas->mtx);
            bool fits = mask_fits(atlas->mask, chart->mask, tr);
            if (fits)
            {
                mask_write(atlas->mask, chart->mask, tr);
                chart->translation = tr;
                chart->atlasIndex = i;
                atlas->chartCount++;
            }
            Mutex_Unlock(&atlas->mtx);
            if (fits)
            {
                return true;
            }
            else
            {
                goto retry;
            }
        }
        else
        {
            *prevRow = ROW_RESET;
        }
    }
    return false;
}

static chartnode_t* chartnodes_create(float texelsPerUnit, i32* countOut)
{
    const Entities* drawables = Entities_Get();
    const i32 numDrawables = drawables->count;
    const Material* pim_noalias materials = drawables->materials;
    const MeshId* pim_noalias meshids = drawables->meshes;
    const float4x4* pim_noalias matrices = drawables->matrices;

    chartnode_t* nodes = NULL;
    i32 nodeCount = 0;

    for (i32 d = 0; d < numDrawables; ++d)
    {
        if (materials[d].flags & kUnmappedMaterials)
        {
            continue;
        }

        Mesh const *const mesh = Mesh_Get(meshids[d]);
        if (!mesh)
        {
            continue;
        }

        const float4x4 M = matrices[d];
        const i32 vertCount = mesh->length;
        const float4* pim_noalias positions = mesh->positions;

        i32 nodeBack = nodeCount;
        nodeCount += vertCount / 3;
        Perm_Reserve(nodes, nodeCount);

        for (i32 v = 0; (v + 3) <= vertCount; v += 3)
        {
            float4 A = f4x4_mul_pt(M, positions[v + 0]);
            float4 B = f4x4_mul_pt(M, positions[v + 1]);
            float4 C = f4x4_mul_pt(M, positions[v + 2]);

            nodes[nodeBack++] = chartnode_new(A, B, C, texelsPerUnit, d, v);
        }
    }

    *countOut = nodeCount;
    return nodes;
}

typedef struct atlastask_s
{
    Task task;
    i32 nodeHead;
    i32 chartCount;
    i32 atlasCount;
    chart_t* charts;
    atlas_t* atlases;
} atlastask_t;

static void AtlasFn(void* pbase, i32 begin, i32 end)
{
    atlastask_t* task = (atlastask_t*)pbase;
    chart_t* pim_noalias charts = task->charts;
    const i32 atlasCount = task->atlasCount;
    const i32 chartCount = task->chartCount;
    i32* pim_noalias pHead = &task->nodeHead;
    atlas_t* pim_noalias atlases = task->atlases;

    i32 prevAtlas = 0;
    i32 prevRow = ROW_RESET;
    float prevArea = 1 << 20;
    for (i32 i = begin; i < end; ++i)
    {
        i32 iChart = inc_i32(pHead, MO_Relaxed);
        if (iChart >= chartCount)
        {
            break;
        }
        chart_t chart = charts[iChart];
        chart.atlasIndex = -1;

        if (chart.area < (prevArea * 0.9f))
        {
            prevArea = chart.area;
            prevAtlas = 0;
            prevRow = ROW_RESET;
        }

        while (!atlas_search(
            atlases,
            atlasCount,
            &chart,
            &prevAtlas,
            &prevRow))
        {
            prevArea = chart.area;
            prevAtlas = 0;
            prevRow = ROW_RESET;
        }

        mask_del(&chart.mask);
        charts[iChart] = chart;
    }
}

pim_inline i32 atlas_estimate(i32 atlasSize, const chart_t* charts, i32 chartCount)
{
    i32 areaRequired = 0;
    for (i32 i = 0; i < chartCount; ++i)
    {
        i32 area = (i32)ceilf(chart_area(charts[i]));
        areaRequired += area;
    }
    ASSERT(areaRequired >= 0);

    const i32 areaPerAtlas = atlasSize * atlasSize;
    i32 atlasCount = 0;
    while ((atlasCount * areaPerAtlas) < areaRequired)
    {
        ++atlasCount;
    }

    return i1_max(1, atlasCount);
}

static i32 atlases_create(i32 atlasSize, chart_t* charts, i32 chartCount)
{
    i32 atlasCount = atlas_estimate(atlasSize, charts, chartCount);
    atlas_t* atlases = Perm_Calloc(sizeof(atlases[0]) * atlasCount);
    for (i32 i = 0; i < atlasCount; ++i)
    {
        atlases[i] = atlas_new(atlasSize);
    }

    atlastask_t* task = Temp_Calloc(sizeof(*task));
    task->atlasCount = atlasCount;
    task->atlases = atlases;
    task->charts = charts;
    task->chartCount = chartCount;
    task->nodeHead = 0;
    Task_Run(&task->task, AtlasFn, chartCount);

    i32 usedAtlases = 0;
    for (i32 i = 0; i < atlasCount; ++i)
    {
        if (atlases[i].chartCount > 0)
        {
            ++usedAtlases;
        }
        atlas_del(atlases + i);
    }
    Mem_Free(atlases);

    return usedAtlases;
}

static void chartnodes_assign(
    chart_t* charts,
    i32 chartCount,
    Lightmap* lightmaps,
    i32 lightmapCount)
{
    Entities const *const drawables = Entities_Get();
    const i32 numDrawables = drawables->count;
    MeshId const *const pim_noalias meshids = drawables->meshes;

    for (i32 iChart = 0; iChart < chartCount; ++iChart)
    {
        const chart_t chart = charts[iChart];
        chartnode_t *const pim_noalias nodes = chart.nodes;
        const i32 nodeCount = chart.nodeCount;
        Lightmap *const pim_noalias lightmap = &lightmaps[chart.atlasIndex];
        const i32 lmTexId = lightmap->slot.index;
        const float scale = 1.0f / lightmap->size;
        const float2 tr = i2_f2(chart.translation);

        for (i32 iNode = 0; iNode < nodeCount; ++iNode)
        {
            chartnode_t node = nodes[iNode];
            const i32 iDrawable = node.drawableIndex;
            const i32 iVert = node.vertIndex;
            ASSERT(iDrawable >= 0);
            ASSERT(iDrawable < numDrawables);
            ASSERT(iVert >= 0);
            ASSERT((drawables->materials[iDrawable].flags & kUnmappedMaterials) == 0);

            Mesh *const mesh = Mesh_Get(meshids[iDrawable]);
            ASSERT(mesh);
            if (mesh)
            {
                const i32 vertCount = mesh->length;
                float4* pim_noalias uvs = mesh->uvs;
                int4* pim_noalias texIndices = mesh->texIndices;

                i32 ia = iVert + 0;
                i32 ib = iVert + 1;
                i32 ic = iVert + 2;
                ASSERT(ic < vertCount);

                texIndices[ia].w = lmTexId;
                texIndices[ib].w = lmTexId;
                texIndices[ic].w = lmTexId;
                float2 uvA = f2_mulvs(f2_add(node.triCoord.a, tr), scale);
                float2 uvB = f2_mulvs(f2_add(node.triCoord.b, tr), scale);
                float2 uvC = f2_mulvs(f2_add(node.triCoord.c, tr), scale);
                uvs[ia].z = uvA.x;
                uvs[ia].w = uvA.y;
                uvs[ib].z = uvB.x;
                uvs[ib].w = uvB.y;
                uvs[ic].z = uvC.x;
                uvs[ic].w = uvC.y;
            }
        }
    }

    for (i32 iDraw = 0; iDraw < numDrawables; ++iDraw)
    {
        Mesh_Upload(meshids[iDraw]);
    }
}

typedef struct EmbedTask_s
{
    Task task;
    Lightmap* lightmaps;
} EmbedTask;

static void EmbedTaskFn(void* pbase, i32 begin, i32 end)
{
    EmbedTask *const task = pbase;
    Lightmap *const pim_noalias lightmaps = task->lightmaps;
    const i32 lmSize = lightmaps[0].size;
    const i32 lmLen = lmSize * lmSize;
    const float texelSize = (float)lmSize;

    Entities const *const drawables = Entities_Get();
    const i32 drawablesCount = drawables->count;
    MeshId const *const pim_noalias meshids = drawables->meshes;
    Material const *const pim_noalias materials = drawables->materials;

    for (i32 iWork = begin; iWork < end; ++iWork)
    {
        const i32 iLightmap = iWork / lmLen;
        const i32 iTexel = iWork % lmLen;
        const i32 x = iTexel % lmSize;
        const i32 y = iTexel / lmSize;
        const float2 pxCenter = { x + 0.5f, y + 0.5f };
        Lightmap *const lightmap = &lightmaps[iLightmap];
        const i32 lmTxId = lightmap->slot.index;

        float4 lmPos = f4_0;
        float4 lmNor = f4_0;
        float lmDist = 1 << 23;

        for (i32 iDraw = 0; iDraw < drawablesCount; ++iDraw)
        {
            if (materials[iDraw].flags & kUnmappedMaterials)
            {
                continue;
            }
            Mesh const *const pim_noalias mesh = Mesh_Get(meshids[iDraw]);
            if (!mesh)
            {
                continue;
            }
            const i32 meshLen = mesh->length;
            float4 const *const pim_noalias positions = mesh->positions;
            float4 const *const pim_noalias normals = mesh->normals;
            float4 const *const pim_noalias uvs = mesh->uvs;
            int4 const *const pim_noalias texIndices = mesh->texIndices;
            for (i32 iVert = 0; (iVert + 3) <= meshLen; iVert += 3)
            {
                if (texIndices[iVert].w != lmTxId)
                {
                    continue;
                }
                const i32 a = iVert + 0;
                const i32 b = iVert + 1;
                const i32 c = iVert + 2;

                float2 A = f2_mulvs(f2_v(uvs[a].z, uvs[a].w), texelSize);
                float2 B = f2_mulvs(f2_v(uvs[b].z, uvs[b].w), texelSize);
                float2 C = f2_mulvs(f2_v(uvs[c].z, uvs[c].w), texelSize);

                float dist = sdTriangle2D(A, B, C, pxCenter);
                if (dist < kFillPadding && dist < lmDist)
                {
                    float area = sdEdge2D(A, B, C);
                    ASSERT(area >= 0.0f);
                    area = pim_max(area, 1e-5f);
                    float4 wuv = bary2D(A, B, C, 1.0f / area, pxCenter);
                    //wuv = f4_saturate(wuv);
                    wuv = f4_divvs(wuv, wuv.x + wuv.y + wuv.z);
                    float4 fragPos = f4_blend(positions[a], positions[b], positions[c], wuv);
                    float4 fragNor = f4_normalize3(f4_blend(normals[a], normals[b], normals[c], wuv));

                    lmDist = dist;
                    lmPos = fragPos;
                    lmNor = fragNor;
                }
            }
        }

        if (lmDist < kFillPadding)
        {
            lmNor = f4_normalize3(lmNor);
        }

        lightmap->sampleCounts[iTexel] = lmDist < kFillPadding ? 1.0f : 0.0f;
        lightmap->position[iTexel] = f4_f3(lmPos);
        lightmap->normal[iTexel] = f4_f3(lmNor);
    }
}

static void EmbedAttributes(
    Lightmap* lightmaps,
    i32 lmCount,
    float texelsPerMeter)
{
    if (lmCount > 0)
    {
        EmbedTask* task = Temp_Calloc(sizeof(*task));
        task->lightmaps = lightmaps;
        Task_Run(task, EmbedTaskFn, TexelCount(lightmaps, lmCount));
    }
}

LmPack LmPack_Pack(
    i32 atlasSize,
    float texelsPerUnit,
    float distThresh,
    float degThresh)
{
    ASSERT(atlasSize > 0);

    if (!ms_once)
    {
        ms_once = true;
        cmd_reg(
            "lm_print",
            "",
            "debug print lightmap images",
            CmdPrintLm);
    }

    float maxWidth = atlasSize / 3.0f;

    i32 nodeCount = 0;
    chartnode_t* nodes = chartnodes_create(texelsPerUnit, &nodeCount);

    i32 chartCount = 0;
    chart_t* charts = chart_group(
        nodes, nodeCount, &chartCount, distThresh, degThresh, maxWidth);

    chart_sort(charts, chartCount);

    i32 atlasCount = atlases_create(atlasSize, charts, chartCount);

    LmPack pack = { 0 };
    pack.lmCount = atlasCount;
    pack.lmSize = atlasSize;
    pack.lightmaps = Perm_Calloc(sizeof(pack.lightmaps[0]) * atlasCount);
    SG_Generate(pack.axii, kGiDirections, SGDist_Hemi);
    pack.texelsPerMeter = texelsPerUnit;

    for (i32 i = 0; i < atlasCount; ++i)
    {
        Lightmap_New(pack.lightmaps + i, atlasSize);
    }

    chartnodes_assign(charts, chartCount, pack.lightmaps, atlasCount);

    EmbedAttributes(pack.lightmaps, atlasCount, texelsPerUnit);

    Mem_Free(nodes);
    for (i32 i = 0; i < chartCount; ++i)
    {
        chart_del(&charts[i]);
    }
    Mem_Free(charts);

    return pack;
}

void LmPack_Del(LmPack* pack)
{
    if (pack)
    {
        for (i32 i = 0; i < pack->lmCount; ++i)
        {
            Lightmap_Del(pack->lightmaps + i);
        }
        Mem_Free(pack->lightmaps);
        memset(pack, 0, sizeof(*pack));
    }
}

typedef struct bake_s
{
    Task task;
    PtScene* scene;
    float timeSlice;
    i32 spp;
} bake_t;

static void BakeFn(void* pbase, i32 begin, i32 end)
{
    bake_t *const task = pbase;
    PtScene *const scene = task->scene;
    const float timeSlice = task->timeSlice;
    const i32 spp = task->spp;

    LmPack *const pack = LmPack_Get();
    const i32 lmSize = pack->lmSize;
    const i32 lmLen = lmSize * lmSize;
    const float metersPerTexel = 1.0f / pack->texelsPerMeter;

    PtSampler sampler = PtSampler_Get();
    for (i32 iWork = begin; iWork < end; ++iWork)
    {
        i32 iLightmap = iWork / lmLen;
        i32 iTexel = iWork % lmLen;
        Lightmap lightmap = pack->lightmaps[iLightmap];

        float sampleCount = lightmap.sampleCounts[iTexel];
        if (sampleCount == 0.0f)
        {
            continue;
        }

        if (Pt_Sample1D(&sampler) > timeSlice)
        {
            continue;
        }

        const float4 N = f4_normalize3(
            f3_f4(lightmap.normal[iTexel], 0.0f));
        const float4 P = f4_add(
            f3_f4(lightmap.position[iTexel], 1.0f),
            f4_mulvs(N, kMilli));
        const float3x3 TBN = NormalToTBN(N);

        float4 probes[kGiDirections];
        float4 axii[kGiDirections];
        for (i32 i = 0; i < kGiDirections; ++i)
        {
            probes[i] = lightmap.probes[i][iTexel];
            float4 ax = kGiAxii[i];
            float sharpness = ax.w;
            ax = TbnToWorld(TBN, ax);
            ax.w = sharpness;
            axii[i] = ax;
        }

        for (i32 i = 0; i < spp; ++i)
        {
            float4 Lts = SampleUnitHemisphere(Pt_Sample2D(&sampler));
            float4 rd = TbnToWorld(TBN, Lts);
            float dt = (Pt_Sample1D(&sampler) - 0.5f) * metersPerTexel;
            float db = (Pt_Sample1D(&sampler) - 0.5f) * metersPerTexel;
            float4 ro = P;
            ro = f4_add(ro, f4_mulvs(TBN.c0, dt));
            ro = f4_add(ro, f4_mulvs(TBN.c1, db));
            PtResult result = Pt_TraceRay(&sampler, scene, ro, rd);
            float weight = 1.0f / sampleCount;
            sampleCount += 1.0f;
            SG_Accumulate(
                weight,
                rd,
                f3_f4(result.color, 0.0f),
                axii,
                probes,
                kGiDirections);
        }

        for (i32 i = 0; i < kGiDirections; ++i)
        {
            lightmap.probes[i][iTexel] = probes[i];
        }
        lightmap.sampleCounts[iTexel] = sampleCount;
    }
    PtSampler_Set(sampler);
}

ProfileMark(pm_Bake, LmPack_Bake)
void LmPack_Bake(PtScene* scene, float timeSlice, i32 spp)
{
    ProfileBegin(pm_Bake);
    ASSERT(scene);

    PtScene_Update(scene);

    LmPack const *const pack = LmPack_Get();
    i32 texelCount = TexelCount(pack->lightmaps, pack->lmCount);
    if (texelCount > 0)
    {
        bake_t *const task = Perm_Calloc(sizeof(*task));
        task->scene = scene;
        task->timeSlice = timeSlice;
        task->spp = i1_max(1, spp);
        Task_Run(task, BakeFn, texelCount);
    }

    ProfileEnd(pm_Bake);
}

bool LmPack_Save(Crate* crate, const LmPack* pack)
{
    bool wrote = false;
    ASSERT(pack);

    const i32 lmcount = pack->lmCount;
    const i32 lmsize = pack->lmSize;
    const i32 texelcount = lmsize * lmsize;
    const Lightmap lmNull = { 0 };
    const i32 probesBytes = sizeof(lmNull.probes[0][0]) * texelcount * kGiDirections;
    const i32 positionBytes = sizeof(lmNull.position[0]) * texelcount;
    const i32 normalBytes = sizeof(lmNull.normal[0]) * texelcount;
    const i32 sampleBytes = sizeof(lmNull.sampleCounts[0]) * texelcount;
    const i32 texelBytes = probesBytes + sampleBytes + positionBytes + normalBytes;

    // write pack header
    DiskLmPack dpack = { 0 };
    dpack.version = kLmPackVersion;
    dpack.directions = kGiDirections;
    dpack.lmCount = lmcount;
    dpack.lmSize = pack->lmSize;
    dpack.bytesPerLightmap = texelBytes;
    dpack.texelsPerMeter = pack->texelsPerMeter;

    if (Crate_Set(crate, Guid_FromStr("lmpack"), &dpack, sizeof(dpack)))
    {
        wrote = true;
        for (i32 i = 0; i < lmcount; ++i)
        {
            char name[PIM_PATH] = { 0 };
            SPrintf(ARGS(name), "lightmap_%d", i);
            const Lightmap lm = pack->lightmaps[i];
            wrote &= Crate_Set(crate, Guid_FromStr(name), lm.probes[0], texelBytes);
        }
    }

    return wrote;
}

bool LmPack_Load(Crate* crate, LmPack* pack)
{
    bool loaded = false;
    LmPack_Del(pack);

    DiskLmPack dpack = { 0 };
    if (Crate_Get(crate, Guid_FromStr("lmpack"), &dpack, sizeof(dpack)))
    {
        if ((dpack.version == kLmPackVersion) &&
            (dpack.directions == kGiDirections) &&
            (dpack.lmCount > 0) &&
            (dpack.lmSize > 0))
        {
            loaded = true;

            const i32 lmcount = dpack.lmCount;
            const i32 lmsize = dpack.lmSize;
            const i32 texelcount = lmsize * lmsize;
            const Lightmap lmNull = { 0 };
            const i32 probesBytes = sizeof(lmNull.probes[0][0]) * texelcount * kGiDirections;
            const i32 positionBytes = sizeof(lmNull.position[0]) * texelcount;
            const i32 normalBytes = sizeof(lmNull.normal[0]) * texelcount;
            const i32 sampleBytes = sizeof(lmNull.sampleCounts[0]) * texelcount;
            const i32 texelBytes = probesBytes + sampleBytes + positionBytes + normalBytes;

            pack->lightmaps = Perm_Calloc(sizeof(pack->lightmaps[0]) * lmcount);
            pack->lmCount = lmcount;
            pack->lmSize = dpack.lmSize;
            pack->texelsPerMeter = dpack.texelsPerMeter;

            for (i32 i = 0; i < lmcount; ++i)
            {
                char name[PIM_PATH] = { 0 };
                SPrintf(ARGS(name), "lightmap_%d", i);
                Lightmap lm = { 0 };
                Lightmap_New(&lm, lmsize);
                loaded &= Crate_Get(crate, Guid_FromStr(name), lm.probes[0], texelBytes);
                Lightmap_Upload(&lm);
                pack->lightmaps[i] = lm;
            }
        }
    }

    return loaded;
}

static cmdstat_t CmdPrintLm(i32 argc, const char** argv)
{
    cmdstat_t status = cmdstat_ok;
    const LmPack* pack = LmPack_Get();
    R8G8B8A8_t* dstBuffer = NULL;
    char filename[PIM_PATH] = { 0 };

    for (i32 iPage = 0; iPage < pack->lmCount; ++iPage)
    {
        const Lightmap lm = pack->lightmaps[iPage];
        const i32 len = lm.size * lm.size;
        dstBuffer = Tex_Realloc(dstBuffer, sizeof(dstBuffer[0]) * len);

        for (i32 iDir = 0; iDir < NELEM(lm.probes); ++iDir)
        {
            const float4* pim_noalias srcBuffer = lm.probes[iDir];
            if (srcBuffer)
            {
                for (i32 iTexel = 0; iTexel < len; ++iTexel)
                {
                    float4 v = srcBuffer[iTexel];
                    v = (lm.sampleCounts[iTexel] > 0.0f) ? v : f4_0;
                    v = Color_SceneToSDR(v);
                    v = f4_reinhard_simple(v);
                    R8G8B8A8_t c = GammaEncode_rgba8(v);
                    c.a = 0xff;
                    dstBuffer[iTexel] = c;
                }
                SPrintf(ARGS(filename), "lm_lum_dir%d_pg%d.png", iPage, iDir);
                if (stbi_write_png(filename, lm.size, lm.size, 4, dstBuffer, lm.size * sizeof(dstBuffer[0])))
                {
                    Con_Logf(LogSev_Info, "lm", "Printed lightmap image '%s'", filename);
                }
                else
                {
                    Con_Logf(LogSev_Error, "lm", "Failed to print lightmap image '%s'", filename);
                    status = cmdstat_err;
                    goto cleanup;
                }
            }
        }

        if (lm.position)
        {
            const float3* pim_noalias srcBuffer = lm.position;
            for (i32 iTexel = 0; iTexel < len; ++iTexel)
            {
                float4 v = f3_f4(srcBuffer[iTexel], 1.0f);
                v = (lm.sampleCounts[iTexel] > 0.0f) ? v : f4_0;
                v = f4_frac(v);
                v = f4_saturate(v);
                R8G8B8A8_t c = GammaEncode_rgba8(v);
                c.a = 0xff;
                dstBuffer[iTexel] = c;
            }
            SPrintf(ARGS(filename), "lm_pos_pg%d.png", iPage);
            if (stbi_write_png(filename, lm.size, lm.size, 4, dstBuffer, lm.size * sizeof(dstBuffer[0])))
            {
                Con_Logf(LogSev_Info, "lm", "Printed lightmap image '%s'", filename);
            }
            else
            {
                Con_Logf(LogSev_Error, "lm", "Failed to print lightmap image '%s'", filename);
                status = cmdstat_err;
                goto cleanup;
            }
        }

        if (lm.normal)
        {
            const float3* pim_noalias srcBuffer = lm.normal;
            for (i32 iTexel = 0; iTexel < len; ++iTexel)
            {
                float4 v = f3_f4(srcBuffer[iTexel], 1.0f);
                v = (lm.sampleCounts[iTexel] > 0.0f) ? v : f4_0;
                v = f4_unorm(v);
                v = f4_saturate(v);
                R8G8B8A8_t c = GammaEncode_rgba8(v);
                c.a = 0xff;
                dstBuffer[iTexel] = c;
            }
            SPrintf(ARGS(filename), "lm_nor_pg%d.png", iPage);
            if (stbi_write_png(filename, lm.size, lm.size, 4, dstBuffer, lm.size * sizeof(dstBuffer[0])))
            {
                Con_Logf(LogSev_Info, "lm", "Printed lightmap image '%s'", filename);
            }
            else
            {
                Con_Logf(LogSev_Error, "lm", "Failed to print lightmap image '%s'", filename);
                status = cmdstat_err;
                goto cleanup;
            }
        }

    }

cleanup:
    Mem_Free(dstBuffer);
    return status;
}
