#pragma once

#include "common/macro.h"
#include "math/types.h"
#include "rendering/path_tracer.h"
#include "threading/task.h"

PIM_C_BEGIN

typedef enum
{
    Cubeface_XP,
    Cubeface_XM,
    Cubeface_YP,
    Cubeface_YM,
    Cubeface_ZP,
    Cubeface_ZM,

    Cubeface_COUNT
} Cubeface;

typedef struct Cubemap_s
{
    i32 size;
    i32 mipCount;
    float4* faces[Cubeface_COUNT];
} Cubemap;

typedef struct Cubemaps_s
{
    i32 count;
    u32* names;
    Cubemap* bakemaps;
    Cubemap* convmaps;
    sphere_t* bounds;
} Cubemaps_t;

Cubemaps_t* Cubemaps_Get(void);

i32 Cubemaps_Add(u32 name, i32 size, sphere_t bounds);
bool Cubemaps_Rm(u32 name);
i32 Cubemaps_Find(u32 name);

void Cubemap_New(Cubemap* cm, i32 size);
void Cubemap_Del(Cubemap* cm);

void Cubemap_Cpy(const Cubemap* src, Cubemap* dst);

Cubeface VEC_CALL Cubemap_CalcUv(float4 dir, float2* uvOut);

pim_inline float VEC_CALL Cubemap_Rough2Mip(float roughness)
{
    return roughness * 4.0f;
}

pim_inline float VEC_CALL Cubemap_Mip2Rough(float mip)
{
    return mip / 4.0f;
}

float4 VEC_CALL Cubemap_Read(const Cubemap* cm, float4 dir, float mip);
void VEC_CALL Cubemap_Write(Cubemap* cm, Cubeface face, int2 coord, float4 value);

void VEC_CALL Cubemap_WriteMip(Cubemap* cm, Cubeface face, int2 coord, i32 mip, float4 value);

float4 VEC_CALL Cubemap_CalcDir(i32 size, Cubeface face, int2 coord, float2 Xi);

float4 VEC_CALL Cubemap_FaceDir(Cubeface face);

task_t* Cubemap_Bake(
    Cubemap* cm,
    const pt_scene_t* scene,
    float4 origin,
    float weight,
    i32 bounces);

void Cubemap_Prefilter(
    const Cubemap* src,
    Cubemap* dst,
    u32 sampleCount,
    float weight);

PIM_C_END
