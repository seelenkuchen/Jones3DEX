// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <string_view>

namespace babylon::platform::detail
{

    // True when pattern is a filter a native picker accepts: either "*", or a
    // semicolon-separated list of extensions made of [A-Za-z0-9_.-], with no
    // empty entry and no leading or trailing separator. Checked before SDL
    // sees the list, because SDL's own validator reads past an empty pattern.
    inline bool isValidFileFilterPattern(std::string_view pattern)
    {
        if ( pattern == "*" )
        {
            return true;
        }
        if ( pattern.empty() )
        {
            return false;
        }
        char previous = ';';
        for ( const char character : pattern )
        {
            const bool allowed = (character >= 'a' && character <= 'z')
                || (character >= 'A' && character <= 'Z')
                || (character >= '0' && character <= '9') || character == '-'
                || character == '_' || character == '.' || character == ';';
            if ( !allowed || (character == ';' && previous == ';') )
            {
                return false;
            }
            previous = character;
        }
        return previous != ';';
    }

}
