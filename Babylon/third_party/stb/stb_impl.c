/* SPDX-License-Identifier: MPL-2.0
   This Source Code Form is subject to the terms of the Mozilla Public
   License, v. 2.0. If a copy of the MPL was not distributed with this
   file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/* The single translation unit that compiles the stb implementations.
   The STB_* and STBI_* options are set by the CMake target so every TU that
   includes the headers agrees on the API surface. */

#define STB_TRUETYPE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO

#include "stb_truetype.h"
#include "stb_image.h"
