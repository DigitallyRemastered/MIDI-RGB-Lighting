cmake_minimum_required(VERSION 3.15)
project(LightEngine VERSION 1.0.0 LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Output directory (place DLL where JUCE plugin can find it)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/../DRLightStudio/Light\ Studio/Resources)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/../DRLightStudio/Light\ Studio/Resources)

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
