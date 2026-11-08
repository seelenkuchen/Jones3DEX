/* SPDX-License-Identifier: MPL-2.0
   This Source Code Form is subject to the terms of the Mozilla Public
   License, v. 2.0. If a copy of the MPL was not distributed with this
   file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/* The single translation unit that compiles miniaudio's implementation.
   MINIAUDIO_IMPLEMENTATION and the MA_NO_* options are set by the CMake
   target so every TU that includes miniaudio.h agrees on the API surface. */

#include "miniaudio.h"
