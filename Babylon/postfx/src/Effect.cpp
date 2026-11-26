// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/postfx/Effect.h>

#include <babylon/render/CommandList.h>

#include <babylon/log/Log.h>

namespace babylon::postfx
{

    bool Effect::record(PassContext& ctx)
    {
        if ( ctx.input.isNull() )
        {
            log::warning("postfx: {}: the pass has no input texture", name());
            return false;
        }
        if ( ctx.inputWidth == 0 || ctx.inputHeight == 0 || ctx.outputWidth == 0
            || ctx.outputHeight == 0 )
        {
            log::warning("postfx: {}: the pass has a zero size", name());
            return false;
        }
        if ( ctx.list.inPass() )
        {
            log::warning("postfx: {}: the command list is inside a pass", name());
            return false;
        }
        if ( stage() != Stage::Resolve
            && (ctx.inputWidth != ctx.outputWidth || ctx.inputHeight != ctx.outputHeight) )
        {
            log::warning("postfx: {}: the input and output sizes differ outside the resolve stage",
                name());
            return false;
        }

        return doRecord(ctx);
    }

}
