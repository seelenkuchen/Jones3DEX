# Third-party code in Babylon

Every dependency of the Babylon Engine is recorded here: vendored code lives in
a directory below with its license text, fetched code is not committed and gets
a row with its source and version (ADR-0017).

| Name | Version | License | Source | Files |
|------|---------|---------|--------|-------|
| doctest | 2.5.3 | MIT | <https://github.com/doctest/doctest/releases/tag/v2.5.3> | `doctest/doctest.h`, `doctest/LICENSE.txt` |
| miniaudio | 0.11.25 | public domain | <https://github.com/mackron/miniaudio/releases/tag/0.11.25> | `miniaudio/miniaudio.h`, `miniaudio/LICENSE` |
| stb | 1.28 (truetype), 2.32 (image) | MIT | <https://github.com/nothings/stb> | `stb/stb_truetype.h`, `stb/stb_image.h`, `stb/LICENSE` |
| SDL3 | 3.4.18 | zlib | <https://github.com/libsdl-org/SDL/releases/tag/release-3.4.18> | fetched at configure time, not vendored |
| SDL_shadercross | commit 1ff05bec573988a98ef9e0260b4da44f512b8367 (2026-09-03) | zlib | <https://github.com/libsdl-org/SDL_shadercross> | build tool, fetched at configure time (`cmake/BabylonShaders.cmake`), not vendored |
| SPIRV-Cross | commit 1a6169566c73d3da552748fc372fe2bbb856e46e (pinned by SDL_shadercross) | Apache-2.0 | <https://github.com/KhronosGroup/SPIRV-Cross> | build tool, fetched with SDL_shadercross, not vendored |
| DirectX Shader Compiler (DXC) | 1.9.2609 | NCSA | <https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.9.2609> | build tool, prebuilt release fetched at configure time by SHA-256, not vendored; only `dxcompiler` is used. The package's dxil validator library (`libdxil.so`, `dxil.dll`) is deleted unused: it comes under a Microsoft EULA (`LICENSE-MS.txt`, "solely for use on Windows"), and DXC 1.9 validates and signs DXIL internally without it |

Every new dependency needs a row here and must have a permissive license: zlib,
MIT, BSD, Apache-2.0 or public domain. No GPL or AGPL code (see ADR-0008).
