These synthetic test vectors are the DirectX 9 rendering masks, kept as references for the future
renderer tests. They use no original game assets.

- The scenes that produced them are in `std3DX9SystemTest.c` and `std3DSystemTestSupport.c`, removed in
  M6.3, see `git log -- Libs/std/Win95/Tests/std3DX9SystemTest.c`.
- `std3D/dx9` contains 24-bit BMP masks for the Direct3D 9 system tests. Most are 320x240. The DX9
  automatic-mipmap cube and the combined DX9 mipmap/MSAA/anisotropic cube sequences are 640x480. Both
  explicit and automatically generated mip chains have combined DX9 sequences. Magenta pixels are ignored
  by the comparison helpers; other pixels are expected rendered pixels.
- `std3DTestVectorTest.c` audited all rendering BMPs during the deterministic `stdTests` run of the
  Windows build: 355 DX9 BMPs and one OpenGL BMP, including filenames, dimensions, uncompressed 24-bit
  headers, exact payload bounds, nonblank content, and animation-frame progression. It was removed with
  the Windows build in M6.3.
- The shared cube masks cover vertices, wireframe, white solid faces, textured faces, textured faces with
  vertex color, and solid faces with interpolated RGB vertex colors across nine 10-degree rotation frames.
- `std3D/dx9/mipmap_lod_mask.bmp`,
  `std3D/dx9/anisotropic_texture_filter_mask.bmp`,
  `std3D/dx9/anisotropic_texture_filter_cube_mask.bmp`, and
  `std3D/dx9/msaa_triangle_mask.bmp` cover simple feature probes for mip-chain
  LOD selection, DX9 anisotropic textured rendering, and DX9 MSAA rendering.
- `std3D/dx9/mipmap_lod_cube_frame*_mask.bmp` and
  `std3D/dx9/msaa_*_cube_frame*_mask.bmp` cover full rotating feature cubes across 32 frames. The
  sequence rotates near the camera, moves backward while rotating, rotates at the far distance, then
  moves forward while rotating. DX9 MSAA cube masks cover wireframe, white solid, textured, textured
  with vertex color, and solid with interpolated RGB vertex color. That is 160 MSAA cube masks, plus
  the simpler triangle MSAA probe. The tests requested 16x MSAA and used the highest supported fallback
  from 8x, 4x, or 2x.
- `std3D/dx9/mipmap_msaa_anisotropic_cube_frame*_mask.bmp` contains the
  32-frame 640x480 DX9 sequence that combines explicit mip levels, trilinear
  mip filtering when supported, anisotropic minification, and the selected
  MSAA level.
- `std3D/dx9/mipmap_autogen_cube_frame*_mask.bmp` contains the 32-frame
  640x480 DX9 sequence generated from a single 512x512 source level using
  `D3DUSAGE_AUTOGENMIPMAP`.
- `std3D/dx9/mipmap_autogen_msaa_anisotropic_cube_frame*_mask.bmp` contains
  the 32-frame 640x480 DX9 sequence that combines automatic mipmap generation,
  anisotropic minification, and the selected MSAA level.
- The mipmap LOD cube vectors use an explicit UV-grid-derived mip chain so the full texture is projected
  once onto each visible cube face. DX9 can generate mipmaps at runtime, but these vectors keep the
  levels explicit for stable mask comparisons. Each child level is generated from its parent with a
  deterministic 2x2 box filter. DX9 uses all ten levels from 512x512 through 1x1.
- `std3D/dx9/uv_grid_directx.bmp` is a 512x512 conversion of the Three.js
  DirectX UV-grid texture. `std3D/opengl/uv_grid_opengl.bmp` is kept for the future OpenGL backend test
  vectors. Their MIT license notice is kept next to the std3D assets.
- The system tests saved new masks with `JONES3D_SYSTEM_TEST_UPDATE_VECTORS=1`, only for an intentional
  golden update.
