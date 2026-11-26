// Added: the configuration read and write helpers of the Display settings screen.
#include <Jones3D/Platform/Babylon/JonesUiDisplaySettings.h>

#include <std/General/stdConfig.h>

#include <Jones3D/Platform/Babylon/JonesUiDisplaySettingsInternal.h>

using namespace JonesUiDisplaySettingsInternal;

namespace JonesUiDisplaySettingsInternal
{
    int JonesUiDisplay_ReadConfigInt(const char* pKey, int defaultValue)
    {
        return stdConfig_HasStarted() ? stdConfig_GetInt(pKey, defaultValue) : defaultValue;
    }

    bool JonesUiDisplay_ReadConfigBool(const char* pKey, bool defaultValue)
    {
        return stdConfig_HasStarted() ? stdConfig_GetBool(pKey, defaultValue) : defaultValue;
    }

    float JonesUiDisplay_ReadConfigFloat(const char* pKey, float defaultValue)
    {
        return stdConfig_HasStarted() ? stdConfig_GetFloat(pKey, defaultValue) : defaultValue;
    }

    void JonesUiDisplay_SetConfigInt(const char* pKey, int value)
    {
        if ( stdConfig_HasStarted() )
        {
            static_cast<void>(stdConfig_SetInt(pKey, value));
        }
    }

    void JonesUiDisplay_SetConfigBool(const char* pKey, bool value)
    {
        if ( stdConfig_HasStarted() )
        {
            static_cast<void>(stdConfig_SetBool(pKey, value));
        }
    }

    void JonesUiDisplay_SetConfigFloat(const char* pKey, float value)
    {
        if ( stdConfig_HasStarted() )
        {
            static_cast<void>(stdConfig_SetFloat(pKey, value));
        }
    }

    void JonesUiDisplay_SetConfigString(const char* pKey, const char* pValue)
    {
        if ( stdConfig_HasStarted() && pValue )
        {
            static_cast<void>(stdConfig_SetString(pKey, pValue));
        }
    }
}
