// Added: what the Babylon column's std3D backend (std3DBabylon.cpp) makes of the renderer's opaque
// cached texture type, for the adapter files and their tests. C++ only.
#ifndef STD_STD3DBABYLON_H
#define STD_STD3DBABYLON_H
#ifndef __cplusplus
#error "std3DBabylon.h is C++ only: it names babylon::render types."
#endif

#include <babylon/render/Handle.h>

#include <std/types.h>

// tSystemTexture::pCachedTexture: the render texture std3D_AddToTextureCache uploaded the system
// texture's mip levels to. Owned by std3D: it is freed, and the texture destroyed, when the system
// texture leaves the cache (std3D_ClearSystemTexture, std3D_ResetTextureCache, a cache purge or
// std3D_Close).
struct sSysTexture
{
    babylon::render::TextureHandle texture;
};

// True while a scene is between std3D_StartScene and std3D_EndScene. The modal screen loop uses it
// because a message can open from inside the game's frame: it ends the caller's scene before it
// captures the frame and draws its own, then starts a new scene for the caller before returning.
bool std3D_IsInScene(void);

// The level of graphics.anisotropicFilter: 1 (off), 2, 4, 8 or 16. An old boolean value
// (true/false) reads as 8/1; a missing or invalid one as J3D_QOL_VALUE(8, 1). std3D_Open reads it.
int std3DBabylon_ReadAnisotropicFilterLevel(void);

// Snaps a level to the offered steps, so 3 is 2 and 12 is 8; anything below 2 is 1.
int std3DBabylon_NormalizeAnisotropicFilterLevel(int level);

// engine.renderer.texturePixelated: nearest min and mag filtering for world and model textures.
bool std3DBabylon_ReadTexturePixelated(void);

#ifdef J3D_TEST
// Test support: the same state std3D_Open reads from the config, without starting one.
void std3DBabylon_SetAnisotropicFilterForTest(int level);
void std3DBabylon_SetTexturePixelatedForTest(bool bEnabled);

// Test support: the vertices recorded this frame by draws that carried STD3D_RS_SHADOW_CASTER.
// The flag stays for M11.3's receiver mask; casters themselves now come through
// std3D_AddShadowCasterPolygon.
uint32_t std3DBabylon_GetCasterVertexCountForTest(void);
#endif

#endif // STD_STD3DBABYLON_H
