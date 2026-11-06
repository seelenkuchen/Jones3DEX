# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

babylon_add_module(render
    SOURCES src/CommandList.cpp src/Device.cpp src/NullDevice.cpp
    DEPS log)
babylon_add_module_tests(render
    DEPS log
    SOURCES tests/RenderContractTest.cpp tests/NullDeviceTest.cpp)
