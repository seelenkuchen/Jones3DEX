// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A depth-only caster writes no color output. The render module's pipelines
// always carry a fragment stage, so this empty one satisfies it while the
// pass has only a depth attachment.

void main()
{
}
