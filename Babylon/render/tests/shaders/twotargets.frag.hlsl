// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Test shader for two color targets: the interpolated vertex color to the
// first, the color with red and blue swapped and opaque alpha to the second.

struct Output
{
    float4 first : SV_Target0;
    float4 second : SV_Target1;
};

Output main(float4 color : TEXCOORD0)
{
    Output output;
    output.first = color;
    output.second = float4(color.b, color.g, color.r, 1.0);
    return output;
}
