#pragma once

#include "common/macro.h"
#include "math/types.h"
#include "rendering/texture.h"

PIM_C_BEGIN

typedef enum
{
    matflag_emissive = 1 << 0,
    matflag_sky = 1 << 1,
    matflag_water = 1 << 2,
    matflag_slime = 1 << 3,
    matflag_lava = 1 << 4,
    matflag_refractive = 1 << 5,
    matflag_warped = 1 << 6,        // uv animated
    matflag_animated = 1 << 7,      // keyframe animated
    matflag_underwater = 1 << 8,    // SURF_UNDERWATER
} matflags_t;

typedef struct material_s
{
    float4 st;              // uv scale and translation
    u32 flatAlbedo;         // rgba8 srgb (albedo, alpha)
    u32 flatRome;           // rgba8 srgb (roughness, occlusion, metallic, emission)
    textureid_t albedo;     // rgba8 srgb (albedo, alpha)
    textureid_t rome;       // rgba8 srgb (roughness, occlusion, metallic, emission)
    textureid_t normal;     // rgba8 (tangent space xyz, packed as unorm)
    u32 flags;
    float ior;              // index of refraction
} material_t;

typedef struct dmaterial_s
{
    float4 st;
    dtextureid_t albedo;
    dtextureid_t rome;
    dtextureid_t normal;
    u32 flatAlbedo;
    u32 flatRome;
    u32 flags;
    float ior;
} dmaterial_t;

PIM_C_END
