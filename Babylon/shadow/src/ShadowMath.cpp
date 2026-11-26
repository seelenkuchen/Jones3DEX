// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/shadow/ShadowMath.h>

#include <cmath>

namespace babylon::shadow
{
namespace
{

    float dot3(const float a[3], const float b[3])
    {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    }

    void cross3(const float a[3], const float b[3], float out[3])
    {
        out[0] = a[1] * b[2] - a[2] * b[1];
        out[1] = a[2] * b[0] - a[0] * b[2];
        out[2] = a[0] * b[1] - a[1] * b[0];
    }

    bool normalize3(float v[3])
    {
        const float length = std::sqrt(dot3(v, v));
        if ( !(length > 0.0f) || !std::isfinite(length) )
        {
            return false;
        }
        v[0] /= length;
        v[1] /= length;
        v[2] /= length;
        return true;
    }

    bool finiteBounds(const Bounds& bounds)
    {
        for ( int axis = 0; axis < 3; ++axis )
        {
            // A zero extent on one axis is allowed: a margin gives a flat box
            // volume before it is projected.
            if ( !std::isfinite(bounds.min[axis]) || !std::isfinite(bounds.max[axis])
                || bounds.max[axis] < bounds.min[axis] )
            {
                return false;
            }
        }
        return true;
    }

}

    Matrix4 identity()
    {
        Matrix4 result{};
        result.m[0]  = 1.0f;
        result.m[5]  = 1.0f;
        result.m[10] = 1.0f;
        result.m[15] = 1.0f;
        return result;
    }

    Matrix4 multiply(const Matrix4& a, const Matrix4& b)
    {
        Matrix4 result{};
        for ( int row = 0; row < 4; ++row )
        {
            for ( int column = 0; column < 4; ++column )
            {
                float sum = 0.0f;
                for ( int k = 0; k < 4; ++k )
                {
                    sum += a.m[row * 4 + k] * b.m[k * 4 + column];
                }
                result.m[row * 4 + column] = sum;
            }
        }
        return result;
    }

    void transformPoint(const Matrix4& m, const float in[3], float out[4])
    {
        for ( int row = 0; row < 4; ++row )
        {
            out[row] = m.m[row * 4 + 0] * in[0] + m.m[row * 4 + 1] * in[1]
                + m.m[row * 4 + 2] * in[2] + m.m[row * 4 + 3];
        }
    }

    bool fitDirectional(const float towardLight[3], const Bounds& casters, float margin,
        std::uint32_t mapSize, Matrix4* pViewToShadowClip, float towardLightReach)
    {
        if ( pViewToShadowClip == nullptr || towardLight == nullptr || mapSize <= 2
            || !std::isfinite(margin) || margin < 0.0f || !std::isfinite(towardLightReach)
            || towardLightReach < 0.0f || !finiteBounds(casters) )
        {
            return false;
        }

        // The current light points toward the light; its view looks the other
        // way. A zero or non-finite direction can't define a light.
        float forward[3] = { -towardLight[0], -towardLight[1], -towardLight[2] };
        if ( !normalize3(forward) )
        {
            return false;
        }

        // An up vector that isn't parallel to the view, so the basis is always
        // defined. (0,0,1) is the fallback for a straight-down light.
        float up[3] = { 0.0f, 1.0f, 0.0f };
        if ( std::fabs(dot3(up, forward)) > 0.999f )
        {
            up[0] = 0.0f;
            up[1] = 0.0f;
            up[2] = 1.0f;
        }

        float right[3];
        cross3(up, forward, right);
        if ( !normalize3(right) )
        {
            return false;
        }
        float lightUp[3];
        cross3(forward, right, lightUp);

        // Expand the box by margin on every side, then project its eight
        // corners into light space.
        float boxMin[3];
        float boxMax[3];
        for ( int axis = 0; axis < 3; ++axis )
        {
            boxMin[axis] = casters.min[axis] - margin;
            boxMax[axis] = casters.max[axis] + margin;
            if ( !std::isfinite(boxMin[axis]) || !std::isfinite(boxMax[axis])
                || boxMax[axis] <= boxMin[axis] )
            {
                return false;
            }
        }

        float lightMin[3] = { INFINITY, INFINITY, INFINITY };
        float lightMax[3] = { -INFINITY, -INFINITY, -INFINITY };
        for ( int corner = 0; corner < 8; ++corner )
        {
            const float point[3] = {
                (corner & 1) != 0 ? boxMax[0] : boxMin[0],
                (corner & 2) != 0 ? boxMax[1] : boxMin[1],
                (corner & 4) != 0 ? boxMax[2] : boxMin[2],
            };
            const float projected[3] = { dot3(point, right), dot3(point, lightUp),
                dot3(point, forward) };
            for ( int axis = 0; axis < 3; ++axis )
            {
                lightMin[axis] = projected[axis] < lightMin[axis] ? projected[axis]
                                                                 : lightMin[axis];
                lightMax[axis] = projected[axis] > lightMax[axis] ? projected[axis]
                                                                 : lightMax[axis];
            }
        }

        float extent[3];
        // Extend the depth range toward the light, leaving the far side at the
        // dynamic casters' far side plus margin.
        lightMin[2] -= towardLightReach;
        for ( int axis = 0; axis < 3; ++axis )
        {
            extent[axis] = lightMax[axis] - lightMin[axis];
            if ( !(extent[axis] > 0.0f) || !std::isfinite(extent[axis]) )
            {
                return false;
            }
        }

        // x and y cover whole texels: a world texel is the axis extent over
        // mapSize - 2, so the range rounds out to mapSize texels and has a
        // texel of slack on each side. That keeps every corner inside -1..1
        // while a whole-texel step of the snapped origin moves a fixed point
        // by exactly one texel.
        float texel[2] = { extent[0] / static_cast<float>(mapSize - 2),
            extent[1] / static_cast<float>(mapSize - 2) };
        float origin[2] = { std::floor(lightMin[0] / texel[0]) * texel[0],
            std::floor(lightMin[1] / texel[1]) * texel[1] };
        float projectedExtent[2] = { texel[0] * static_cast<float>(mapSize),
            texel[1] * static_cast<float>(mapSize) };
        float center[2] = { origin[0] + projectedExtent[0] * 0.5f,
            origin[1] + projectedExtent[1] * 0.5f };

        const float scaleX = 2.0f / projectedExtent[0];
        const float scaleY = 2.0f / projectedExtent[1];
        const float scaleZ = 1.0f / extent[2];

        Matrix4 result{};
        result.m[0]  = scaleX * right[0];
        result.m[1]  = scaleX * right[1];
        result.m[2]  = scaleX * right[2];
        result.m[3]  = -scaleX * center[0];
        result.m[4]  = scaleY * lightUp[0];
        result.m[5]  = scaleY * lightUp[1];
        result.m[6]  = scaleY * lightUp[2];
        result.m[7]  = -scaleY * center[1];
        result.m[8]  = scaleZ * forward[0];
        result.m[9]  = scaleZ * forward[1];
        result.m[10] = scaleZ * forward[2];
        result.m[11] = -scaleZ * lightMin[2];
        result.m[12] = 0.0f;
        result.m[13] = 0.0f;
        result.m[14] = 0.0f;
        result.m[15] = 1.0f;

        *pViewToShadowClip = result;
        return true;
    }

    Matrix4 shadowClipToTexture()
    {
        Matrix4 result{};
        result.m[0]  = 0.5f;
        result.m[5]  = -0.5f;
        result.m[10] = 1.0f;
        result.m[3]  = 0.5f;
        result.m[7]  = 0.5f;
        result.m[15] = 1.0f;
        return result;
    }

}
