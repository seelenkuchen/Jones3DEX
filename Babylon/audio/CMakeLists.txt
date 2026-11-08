# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

babylon_add_module(audio
    SOURCES src/Device.cpp src/NullDevice.cpp
        backends/miniaudio/MiniaudioDevice.cpp
    DEPS log
    LIBS miniaudio)
babylon_add_module_tests(audio
    DEPS log
    SOURCES tests/AudioContractTest.cpp tests/NullDeviceTest.cpp
        backends/miniaudio/tests/MiniaudioDeviceTest.cpp)
