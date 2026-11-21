# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

babylon_add_module(process
    SOURCES src/NullProcess.cpp backends/sdl/SdlProcess.cpp
    DEPS log
    LIBS ${BABYLON_SDL3_TARGET})
babylon_add_module_tests(process
    DEPS log
    SOURCES tests/NullProcessTest.cpp backends/sdl/tests/SdlProcessTest.cpp)
if (TARGET babylon_process_tests)
    # The SDL backend tests run CMake itself: it exists on every build machine
    # and needs no system-specific tool. Passing the configured path keeps the
    # test independent of PATH.
    target_compile_definitions(babylon_process_tests PRIVATE
        BABYLON_TEST_CMAKE_COMMAND="${CMAKE_COMMAND}")
endif()
