// Added: the choice-table helpers of the Display settings screen.
#include <Jones3D/Platform/Babylon/JonesUiDisplaySettings.h>

#include <Jones3D/Platform/Babylon/JonesUiContext.h>

#include <std/Babylon/std3DBabylon.h>
#include <std/General/stdUtil.h>

#include <cmath>
#include <cstddef>
#include <string>

#include <Jones3D/Platform/Babylon/JonesUiDisplaySettingsInternal.h>

using namespace JonesUiDisplaySettingsInternal;

namespace JonesUiDisplaySettingsInternal
{
    // The render scale choice nearest to scale; 100% when the value is not finite.
    std::size_t JonesUiDisplay_RenderScaleIndex(float scale)
    {
        if ( !std::isfinite(scale) )
        {
            return kJonesDisplayRenderScaleDefault;
        }

        std::size_t best = 0;
        float bestDistance = std::fabs(scale - kJonesDisplayRenderScaleValues[0]);
        for ( std::size_t cur = 1; cur < STD_ARRAYLEN(kJonesDisplayRenderScaleValues); ++cur )
        {
            const float distance = std::fabs(scale - kJonesDisplayRenderScaleValues[cur]);
            if ( distance < bestDistance )
            {
                best         = cur;
                bestDistance = distance;
            }
        }

        return best;
    }

    // The anti-aliasing choice of a stored value: SMAA for a nonzero one, Off otherwise.
    std::size_t JonesUiDisplay_AntiAliasingIndex(int value)
    {
        return value != 0 ? 1u : 0u;
    }

    // The bloom choice of a stored level; Low when it is not one of the offered ones.
    std::size_t JonesUiDisplay_BloomIndex(int level)
    {
        for ( std::size_t cur = 0; cur < STD_ARRAYLEN(kJonesDisplayBloomValues); ++cur )
        {
            if ( kJonesDisplayBloomValues[cur] == level )
            {
                return cur;
            }
        }

        return kJonesDisplayBloomDefault;
    }

    // The light-shaft choice of a stored level; Low when it is not one of the offered ones.
    std::size_t JonesUiDisplay_LightShaftsIndex(int level)
    {
        for ( std::size_t cur = 0; cur < STD_ARRAYLEN(kJonesDisplayLightShaftsValues); ++cur )
        {
            if ( kJonesDisplayLightShaftsValues[cur] == level )
            {
                return cur;
            }
        }

        return kJonesDisplayLightShaftsDefault;
    }

    // The ambient-occlusion choice of a stored level; Low when it is not one of the offered ones.
    std::size_t JonesUiDisplay_SsaoIndex(int level)
    {
        for ( std::size_t cur = 0; cur < STD_ARRAYLEN(kJonesDisplaySsaoValues); ++cur )
        {
            if ( kJonesDisplaySsaoValues[cur] == level )
            {
                return cur;
            }
        }

        return kJonesDisplaySsaoDefault;
    }

    // The motion-blur choice of a stored level; Low when it is not one of the offered ones.
    std::size_t JonesUiDisplay_MotionBlurIndex(int level)
    {
        for ( std::size_t cur = 0; cur < STD_ARRAYLEN(kJonesDisplayMotionBlurValues); ++cur )
        {
            if ( kJonesDisplayMotionBlurValues[cur] == level )
            {
                return cur;
            }
        }

        return kJonesDisplayMotionBlurDefault;
    }

    // The sharpening choice nearest to strength; Low when the value is not finite.
    std::size_t JonesUiDisplay_SharpeningIndex(float strength)
    {
        if ( !std::isfinite(strength) )
        {
            return kJonesDisplaySharpeningDefault;
        }

        std::size_t best = 0;
        float bestDistance = std::fabs(strength - kJonesDisplaySharpeningValues[0]);
        for ( std::size_t cur = 1; cur < STD_ARRAYLEN(kJonesDisplaySharpeningValues); ++cur )
        {
            const float distance = std::fabs(strength - kJonesDisplaySharpeningValues[cur]);
            if ( distance < bestDistance )
            {
                best         = cur;
                bestDistance = distance;
            }
        }

        return best;
    }

    // The tone-mapping choice of a stored operator; Neutral when it is not one of the offered ones.
    std::size_t JonesUiDisplay_ToneMappingIndex(int oper)
    {
        for ( std::size_t cur = 0; cur < STD_ARRAYLEN(kJonesDisplayToneMappingValues); ++cur )
        {
            if ( kJonesDisplayToneMappingValues[cur] == oper )
            {
                return cur;
            }
        }

        return kJonesDisplayToneMappingDefault;
    }

    // The color-grading choice of a stored preset; Neutral when it is not one of the offered ones.
    std::size_t JonesUiDisplay_ColorGradingIndex(int preset)
    {
        for ( std::size_t cur = 0; cur < STD_ARRAYLEN(kJonesDisplayColorGradingValues); ++cur )
        {
            if ( kJonesDisplayColorGradingValues[cur] == preset )
            {
                return cur;
            }
        }

        return kJonesDisplayColorGradingDefault;
    }

    // The frame-rate choice of a stored value; V-Sync when it is not one of the offered ones.
    std::size_t JonesUiDisplay_FrameLimitIndex(int frameLimit)
    {
        for ( std::size_t cur = 0; cur < STD_ARRAYLEN(kJonesDisplayFrameLimitValues); ++cur )
        {
            if ( kJonesDisplayFrameLimitValues[cur] == frameLimit )
            {
                return cur;
            }
        }

        return kJonesDisplayFrameLimitDefault;
    }

    // The anisotropic choice of a level; the nearest offered step, 8x when it is not usable.
    std::size_t JonesUiDisplay_AnisotropicIndex(int level)
    {
        const int normalized = std3DBabylon_NormalizeAnisotropicFilterLevel(level);
        for ( std::size_t cur = 0; cur < STD_ARRAYLEN(kJonesDisplayAnisotropicValues); ++cur )
        {
            if ( kJonesDisplayAnisotropicValues[cur] == normalized )
            {
                return cur;
            }
        }

        return kJonesDisplayAnisotropicDefault;
    }

    std::string JonesUiDisplay_ResolveLabel(const char* pText)
    {
        return JonesUiContext_ResolveString(pText);
    }
}
