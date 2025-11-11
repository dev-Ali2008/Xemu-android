cmake_minimum_required(VERSION 3.23.1)
project(xanite)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Disable ALL compiler warnings globally
add_compile_options(-w)

# Android specific settings
if(ANDROID)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -fexceptions -frtti")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99")
    
    # Removed -mfpu=neon for arm64-v8a, not needed
    # NEON is enabled by default on arm64
endif()

# Xenia source directories
set(XENIA_SOURCE_DIR ${CMAKE_SOURCE_DIR}/src)
set(XENIA_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)

# Include directories
include_directories(
    ${XENIA_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party/game-activity
    ${CMAKE_SOURCE_DIR}/third_party/game-text-input
    ${CMAKE_SOURCE_DIR}/third_party/fmt/include
    ${CMAKE_SOURCE_DIR}/third_party/pugixml/src
    ${CMAKE_SOURCE_DIR}/third_party/tomlplusplus/include
    ${CMAKE_SOURCE_DIR}/third_party/cxxopts/include
    ${CMAKE_SOURCE_DIR}/third_party/date/include
    ${CMAKE_SOURCE_DIR}/third_party/disruptorplus/include
    ${CMAKE_SOURCE_DIR}/third_party/utfcpp/source
)

# Android native sources
set(ANDROID_SOURCES
    native.cpp
    android_settings.cpp
    game_metadata.cpp
    native_log.cpp
    android_config.cpp
    emu_window/emu_window.cpp
)

# Xenia core sources
set(XENIA_CORE_SOURCES
    xenia/xbox.h
    xenia/base/main_android.cc
    xenia/base/main_android.h
    xenia/base/system_android.cc
    xenia/base/filesystem_android.cc
    xenia/base/cvar_android.cc
    xenia/base/console_app_main_android.cc
    xenia/base/string_util.h
    xenia/base/mutex.cc
    xenia/base/mutex.h
    xenia/base/clock.cc
    xenia/base/clock.h
    xenia/base/arena.cc
    xenia/base/arena.h
    third_party/pugixml/src/pugixml.cpp
)

# Create the native library
add_library(xanite SHARED
    ${ANDROID_SOURCES}
    ${XENIA_CORE_SOURCES}
)

# Disable warnings
target_compile_options(xanite PRIVATE
    -w
    -Wno-deprecated-copy
    -Wno-format-security
    -Wno-bitwise-op-parentheses
    -Wno-logical-op-parentheses
    -Wno-unused-parameter
    -Wno-missing-field-initializers
    -Wno-sign-compare
    -Wno-unused-variable
    -Wno-ignored-qualifiers
    -Wno-tautological-overlap-compare
    -Wno-everything
)

# Include directories for libraries
target_include_directories(xanite PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/game-activity
    ${CMAKE_SOURCE_DIR}/third_party/game-text-input
    ${CMAKE_SOURCE_DIR}/third_party/json
)

# Link Android libraries
target_link_libraries(xanite
    android
    log
    EGL
    GLESv3
    vulkan
)

# CPU features library
find_library(ANDROID_CPU_FEATURES_LIB cpu_features)
if(ANDROID_CPU_FEATURES_LIB)
    target_link_libraries(xanite ${ANDROID_CPU_FEATURES_LIB})
endif()

# Android definitions
if(ANDROID)
    target_compile_definitions(xanite PRIVATE
        XE_PLATFORM_ANDROID=1
        ANDROID=1
        __ANDROID__=1
    )
    
    set_target_properties(xanite PROPERTIES
        LIBRARY_OUTPUT_NAME "xanite"
    )
endif()

# Install configuration
install(TARGETS xanite
    LIBRARY DESTINATION lib/${ANDROID_ABI}
)

# Export compile commands
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

message(STATUS "Xanite Android build configuration complete")
message(STATUS "Android ABI: ${ANDROID_ABI}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Source dir: ${XENIA_SOURCE_DIR}")
message(STATUS "Game-activity include: ${CMAKE_SOURCE_DIR}/third_party/game-activity")
message(STATUS "Game-text-input include: ${CMAKE_SOURCE_DIR}/third_party/game-text-input")