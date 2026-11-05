# Added: The Null platform runs its tests natively; the top-level project disables testing there,
# so this directory enables it for j3dcore.Crt.
enable_testing()

if (JONES3D_BUILD_TESTS)
    set(J3DCORE_TEST_SOURCES
        j3dMacroTest.c
    )

    # Added: The Null platform builds j3dCrtTest.c as its own executable below (it provides its
    # own main there), so it must not also be linked into j3dcoreTests.

    add_executable(j3dcoreTests
        ${J3DCORE_TEST_SOURCES}
    )

    target_link_libraries(j3dcoreTests PRIVATE
        j3dcore
        unity
    )

    set_target_properties(j3dcoreTests PROPERTIES
        FOLDER "Tests/Libs/j3dcore"
    )

    add_test(NAME j3dcore.Macros COMMAND j3dcoreTests)
    set_tests_properties(j3dcore.Macros PROPERTIES
        LABELS "unit;j3dcore"
    )
endif()

# Added: Null platform: native executable for the j3dcrt Microsoft CRT shim (j3dCrtTest.c
# provides its own main only under J3D_PLATFORM_NULL).
add_executable(j3dCrtTests
    j3dCrtTest.c
)

target_link_libraries(j3dCrtTests PRIVATE
    j3dcore
    unity
)

set_target_properties(j3dCrtTests PROPERTIES
    FOLDER "Tests/Libs/j3dcore"
)

add_test(NAME j3dcore.Crt COMMAND j3dCrtTests)
set_tests_properties(j3dcore.Crt PROPERTIES
    LABELS "unit;j3dcore"
)
