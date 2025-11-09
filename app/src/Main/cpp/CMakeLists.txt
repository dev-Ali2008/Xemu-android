cmake_minimum_required(VERSION 3.23.1)
project(xanite)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Android 特定设置
if(ANDROID)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -fexceptions -frtti")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99")
    
    # 为 ARM 启用 NEON
    if(ARM)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mfpu=neon")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mfpu=neon")
    endif()
endif()

# Xenia 源目录
set(XENIA_SOURCE_DIR ${CMAKE_SOURCE_DIR}/src)
set(XENIA_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)

# 包含目录
include_directories(
    ${XENIA_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}
)

# Android native sources
set(ANDROID_SOURCES
    native.cpp
    native.h
    native_config.cpp
    android_settings.cpp
    android_settings.h
    game_metadata.cpp
    game_metadata.h
    native_log.cpp
    native_log.h
    android_config.cpp
    android_config.h
    native_input.cpp
    emu_window/emu_window.cpp
    emu_window/emu_window.h
)

# Xenia核心源代码（您需要在此处添加实际的Xenia源代码文件）
set(XENIA_CORE_SOURCES
    # Add Xenia source files here
    # ${XENIA_SOURCE_DIR}/xenia/main.cc
    # ${XENIA_SOURCE_DIR}/xenia/emulator.cc
    # ... etc
)

# Create the native library
add_library(xanite SHARED
    ${ANDROID_SOURCES}
    ${XENIA_CORE_SOURCES}
)

# Android specific libraries
target_link_libraries(xanite
    android
    log
    EGL
    GLESv3
    vulkan
    game-activity
    gametextinput
)

# Android CPU 功能库
find_library(ANDROID_CPU_FEATURES_LIB android)
if(ANDROID_CPU_FEATURES_LIB)
    target_link_libraries(xanite ${ANDROID_CPU_FEATURES_LIB})
endif()

# Xenia 依赖项（简化版 - 您还需要链接实际的 Xenia 库）
target_link_libraries(xanite
    # Xenia core libraries would go here
    # xe-core
    # xe-kernel
    # xe-gpu
    # xe-apu
    # xe-hid
    # xe-ui
    # xe-vfs
)

# 编译器特定选项
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(xanite PRIVATE
        -Wall
        -Wextra
        -Wno-unused-parameter
        -Wno-missing-field-initializers
        -Wno-sign-compare
        -Wno-unused-variable
    )
endif()

# Android 特定定义
if(ANDROID)
    target_compile_definitions(xanite PRIVATE
        XE_PLATFORM_ANDROID=1
        ANDROID=1
        __ANDROID__=1
    )
    
    # Set the shared library name
    set_target_properties(xanite PROPERTIES
        LIBRARY_OUTPUT_NAME "xanite"
        PREFIX "lib"
    )
endif()

# 安装配置
install(TARGETS xanite
    LIBRARY DESTINATION lib/${ANDROID_ABI}
    RUNTIME DESTINATION bin
)

# 导出编译命令以支持 IDE
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add subdirectories for Xenia components
# add_subdirectory(src/xenia)
# add_subdirectory(third_party)

message(STATUS "Xanite Android 构建配置完成")
message(STATUS "Android ABI: ${ANDROID_ABI}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Source dir: ${XENIA_SOURCE_DIR}")
message(STATUS "Install dir: ${CMAKE_INSTALL_PREFIX}")
