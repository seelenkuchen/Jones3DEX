// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

namespace babylon::audio
{

    // A point or a direction in world space, in the caller's units. The
    // module never interprets the units: only distances, ratios and
    // directions between these values matter. Every component must be
    // finite; NaN and infinity are refused wherever a Vector3 enters.
    struct Vector3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        bool operator==(const Vector3&) const = default;
    };

    // The listener for every 3D voice. position and velocity are world
    // space, in caller units and caller units per second. forward and up are
    // directions, not positions: they need not have unit length, but each
    // must be finite and non-zero and the two may not be parallel, since
    // together they define the listener's orientation. The backend
    // orthonormalizes them.
    //
    // The listener's right is normalize(cross(up, forward)). This fixes the
    // handedness: with forward +z and up +y, cross(up, forward) is +x, so
    // the default orientation has right +x, up +y and forward +z, and
    // (right, up, forward) is a right-handed set. A voice whose direction
    // from the listener has a positive component along right is on the
    // listener's right and pans toward +1; one along -right pans toward -1.
    struct Listener
    {
        Vector3 position;
        Vector3 velocity;
        Vector3 forward{ 0.0f, 0.0f, 1.0f };
        Vector3 up{ 0.0f, 1.0f, 0.0f };

        bool operator==(const Listener&) const = default;
    };

}
