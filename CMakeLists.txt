cmake_minimum_required(VERSION 3.21)

project(GhostClock VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(CTest)

add_library(ghostclock_core STATIC
    src/core/core.c
    src/core/profile.c
)

target_include_directories(ghostclock_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(ghostclock_core PRIVATE /W4)
else()
    target_compile_options(ghostclock_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    add_library(ghostclock_windows STATIC
        src/platform/windows/windows.c
    )

    target_include_directories(ghostclock_windows
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_compile_definitions(ghostclock_windows
        PUBLIC
            WIN32_LEAN_AND_MEAN
            _WIN32_WINNT=0x0A00
    )
    target_link_libraries(ghostclock_windows
        PUBLIC
            ghostclock_core
            psapi
    )

    add_executable(ghostclock
        src/cli/main.c
    )
    target_compile_definitions(ghostclock PRIVATE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
    target_link_libraries(ghostclock PRIVATE ghostclock_core ghostclock_windows)

    if(MSVC)
        target_compile_options(ghostclock_windows PRIVATE /W4)
        target_compile_options(ghostclock PRIVATE /W4)
    else()
        target_compile_options(ghostclock_windows PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(ghostclock PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    install(TARGETS ghostclock RUNTIME DESTINATION bin)
else()
    message(STATUS "Non-Windows host detected: building portable core and tests only")
endif()

if(BUILD_TESTING)
    add_executable(ghostclock_tests tests/test_core.c)
    target_link_libraries(ghostclock_tests PRIVATE ghostclock_core)
    if(NOT MSVC)
        target_link_libraries(ghostclock_tests PRIVATE m)
    endif()
    add_test(NAME ghostclock_core_tests COMMAND ghostclock_tests)

    if(WIN32)
        add_executable(ghostclock_windows_integration_tests tests/test_windows_integration.c)
        target_compile_definitions(
            ghostclock_windows_integration_tests
            PRIVATE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
        target_link_libraries(
            ghostclock_windows_integration_tests
            PRIVATE ghostclock_core ghostclock_windows)
        if(MSVC)
            target_compile_options(ghostclock_windows_integration_tests PRIVATE /W4)
        else()
            target_compile_options(
                ghostclock_windows_integration_tests
                PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        add_test(
            NAME ghostclock_windows_priority_rollback
            COMMAND ghostclock_windows_integration_tests)
    endif()
endif()
