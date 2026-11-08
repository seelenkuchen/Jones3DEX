# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

babylon_add_module(ui
    SOURCES src/Ui.cpp src/NullPainter.cpp
    DEPS log
    PUBLIC_DEPS cvars platform)
babylon_add_module_tests(ui
    DEPS log cvars platform
    SOURCES tests/UiTest.cpp)
