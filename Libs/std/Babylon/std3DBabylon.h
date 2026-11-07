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

#endif // STD_STD3DBABYLON_H
