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

# Source files
set(API_SOURCES
    src/LightEngineAPI.cpp
    src/Metadata.cpp
    src/Constants.cpp
)

set(MODE_SOURCES
    src/Modes/RainbowWheel.cpp
    src/Modes/MovingDots.cpp
    src/Modes/Comets.cpp
    src/Modes/OceanWaves.cpp
    src/Modes/Backgrounds.cpp
    src/Modes/NotesToDrives.cpp
    src/Modes/RemainingModes.cpp
)

# Create shared library
add_library(LightEngine SHARED
    ${API_SOURCES}
    ${MODE_SOURCES}
)

# Include directories
target_include_directories(LightEngine
    PUBLIC
        ${CMAKE_SOURCE_DIR}/../Shared
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
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

install(FILES ${CMAKE_SOURCE_DIR}/../Shared/LightEngineAPI.h
    DESTINATION include
)

# Print configuration info
message(STATUS "Light Engine DLL Configuration:")
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
