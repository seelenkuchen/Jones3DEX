// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Test shader: writes the interpolated vertex color.

float4 main(float4 color : TEXCOORD0) : SV_Target0
{
    return color;
}
