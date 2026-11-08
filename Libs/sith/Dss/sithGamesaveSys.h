#ifndef SITH_SITHGAMESAVESYS_H
#define SITH_SITHGAMESAVESYS_H

#include <sith/Dss/sithGamesave.h>
#include <std/General/stdColor.h>

J3D_EXTERN_C_START

#define SITHSAVEGAME_THUMBCOLORFORMAT stdColor_cfRGB888 // Note, changed encoding format due to little-endian fix in stdColor_ColorConvertOneRow
// Added: the thumbnail is RGB888, three bytes per pixel; the byte count is an integer constant expression
// so it can size the in-memory thumbnail and be checked against the format below.
#define SITHSAVEGAME_THUMB_BYTES_PER_PIXEL 3
#define SITHSAVEGAME_THUMBSIZE (SITHSAVEGAME_THUMB_WIDTH * SITHSAVEGAME_THUMB_HEIGHT * SITHSAVEGAME_THUMB_BYTES_PER_PIXEL)
//static_assert(SITHSAVEGAME_THUMBSIZE == 0x9000, "SITHSAVEGAME_THUMBSIZE must be  0x9000 bytes");

#define SITHSAVEGAME_FILEVERSION  13

// The save game thumbnail state, shared between sithGamesave.c and the portable thumbnail code in
// sithGamesaveThumbnail.c. hBmpThumbnail points at the in-memory thumbnail bytes.
extern tVBuffer* sithGamesave_pThumbnailImage;
extern int sithGamesave_bThumbnail;
extern tSysBitmap sithGamesave_hBmpThumbnail;

// Releases the thumbnail state exactly as sithGamesave_Close does; a safe no-op when not set.
void sithGamesaveSys_ReleaseThumbnail(void); // Implemented in Libs/sith/Dss/sithGamesaveThumbnail.c

J3D_EXTERN_C_END
#endif // SITH_SITHGAMESAVESYS_H
