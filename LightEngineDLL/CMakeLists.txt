cmake_minimum_required(VERSION 3.15)
project(LightEngine VERSION 1.0.0 LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Output directory — place DLL in MIDI-RGB-Lighting/Release/ so it is committed to
# this repo and picked up by DRLightStudio when it updates its submodule pointer.
# Plugin loads from: Resources/MIDI-RGB-Lighting/Release/LightEngine.dll
#
# Use per-config variables to prevent Visual Studio (multi-config generator) from
# appending the config name (e.g. Release/Release/ or Release/Debug/).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/../Release)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_SOURCE_DIR}/../Release)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/../Release)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_SOURCE_DIR}/../Release)

# Platform-specific settings
if(WIN32)
    set(LIBRARY_OUTPUT_NAME "LightEngine.dll")
elseif(APPLE)
    set(LIBRARY_OUTPUT_NAME "LightEngine.dylib")
else()
    set(LIBRARY_OUTPUT_NAME "LightEngine.so")
endif()

# Reference Arduino LightEngine source files (single source of truth)
set(ARDUINO_LIGHT_ENGINE_DIR ${CMAKE_SOURCE_DIR}/../libraries/LightEngine/src)

# Source files - Use the SAME files as Arduino
set(ENGINE_SOURCES
    ${ARDUINO_LIGHT_ENGINE_DIR}/LightEngine.cpp
)

# Create shared library
add_library(LightEngine SHARED
    ${ENGINE_SOURCES}
)

# Include directories - point to Arduino folder
target_include_directories(LightEngine
    PRIVATE
        ${ARDUINO_LIGHT_ENGINE_DIR}
)

# Opt in to dllexport for the C API.  When the same engine source is compiled
# statically into the Light Studio plugin this is NOT defined, so the symbols
# stay internal to that binary.
target_compile_definitions(LightEngine PRIVATE LIGHT_ENGINE_BUILD_DLL)

# Platform-specific compile options
if(MSVC)
    # Windows: Export symbols, disable warnings about C functions
    target_compile_options(LightEngine PRIVATE /W4 /wd4996)
    target_compile_definitions(LightEngine PRIVATE _CRT_SECURE_NO_WARNINGS)
elseif(APPLE)
    # macOS: Enable visibility attributes
    target_compile_options(LightEngine PRIVATE -Wall -Wextra -fvisibility=hidden)
else()
    # Linux: Enable visibility attributes
    target_compile_options(LightEngine PRIVATE -Wall -Wextra -fvisibility=hidden)
endif()

# Set output name
set_target_properties(LightEngine PROPERTIES
    OUTPUT_NAME "LightEngine"
    PREFIX ""
    SUFFIX "${CMAKE_SHARED_LIBRARY_SUFFIX}"
)

# Installation (optional - for system-wide install)
install(TARGETS LightEngine
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES ${ARDUINO_LIGHT_ENGINE_DIR}/LightEngine.h
    DESTINATION include
)

# ============================================================================
# Unit Tests (Catch2 via FetchContent — off by default in production builds)
# ============================================================================
option(LIGHT_ENGINE_BUILD_TESTS "Build LightEngine unit tests" ON)

if(LIGHT_ENGINE_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
    )
    FetchContent_MakeAvailable(Catch2)

    # Compile the engine sources directly into the test binary (no DLL export
    # macro, so the C++ class API is accessible without function pointers).
    add_executable(LightEngineTests
        tests/test_light_engine.cpp
        ${ARDUINO_LIGHT_ENGINE_DIR}/LightEngine.cpp
    )

    target_include_directories(LightEngineTests
        PRIVATE
            ${ARDUINO_LIGHT_ENGINE_DIR}
    )

    target_link_libraries(LightEngineTests
        PRIVATE
            Catch2::Catch2WithMain
    )

    target_compile_options(LightEngineTests PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W3 /wd4996>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wno-unused-parameter>
    )

    target_compile_definitions(LightEngineTests PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
    )

    include(CTest)
    enable_testing()

    # catch_discover_tests requires the cmake extras from Catch2's source tree
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
    include(Catch)
    catch_discover_tests(LightEngineTests)
endif()

# Print configuration info
message(STATUS "Light Engine DLL Configuration:")
message(STATUS "  Source Directory: ${ARDUINO_LIGHT_ENGINE_DIR}")
message(STATUS "  Output Directory: ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
if(WIN32)
    message(STATUS "  Platform: Windows (.dll)")
elseif(APPLE)
    message(STATUS "  Platform: macOS (.dylib)")
else()
    message(STATUS "  Platform: Linux (.so)")
endif()
