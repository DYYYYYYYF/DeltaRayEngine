cmake_minimum_required(VERSION 3.21)
project(DuckDuck 
    VERSION 1.0.0
    DESCRIPTION "A simple game engine"
    LANGUAGES CXX
)

# C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 生成Cmake指令
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 输出目录
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/Bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/Bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/Lib)

set(STB_DIR ${PROJECT_SOURCE_DIR}/3rd/stb)

# 多配置生成器
foreach(CONFIG ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${CONFIG} CONFIG_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER} ${CMAKE_BINARY_DIR}/Bin/${CONFIG})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG_UPPER} ${CMAKE_BINARY_DIR}/Lib/${CONFIG})
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_UPPER} ${CMAKE_BINARY_DIR}/Lib/${CONFIG})
endforeach()

# 构建选项
option(ENABLE_PLUGINS_AUDIO "Enable audio module" OFF)
option(GENERATE_TEST_PROGRAME "Generate test module" ON)

if(MSVC)
    # 强制所有目标使用统一的警告级别（包含第三方库）
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /permissive- /MP /wd4201 /wd4324 /wd4100")
    string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

    add_compile_definitions(
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )

else()
    add_compile_options(${PROJECT_NAME} PRIVATE
        -Wall -Wextra -Wpedantic
    )
endif()

# 第三方库
message(STATUS "Configuring Third-Party Libraries...")
add_subdirectory(3rd)

## Set build type
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
    message("-- Use default build type.")
 endif()

if (NOT LOG_LEVEL)
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(LOG_LEVEL "DEBUG")
    else()
        set(LOG_LEVEL "ERROR")
    endif()
endif()

if(${LOG_LEVEL} STREQUAL "INFO")
    add_definitions(-DLEVEL_INFO -DLEVEL_DEBUG -DLEVEL_WARN -DLEVEL_FATAL -DLEVEL_ERROR)
elseif(${LOG_LEVEL}  STREQUAL "DEBUG")
    add_definitions(-DLEVEL_DEBUG -DLEVEL_WARN -DLEVEL_FATAL -DLEVEL_ERROR)
elseif(${LOG_LEVEL}  STREQUAL "WARN")
    add_definitions(-DLEVEL_WARN -DLEVEL_FATAL -DLEVEL_ERROR)
elseif(${LOG_LEVEL}  STREQUAL "ERROR")
    add_definitions(-DLEVEL_ERROR -DLEVEL_FATAL)
elseif(${LOG_LEVEL}  STREQUAL "FATAL")
    add_definitions(-DLEVEL_FATAL)
endif()

if(APPLE)
    message(STATUS "Current environment: macOS")

elseif(WIN32)
    message(STATUS "Current environment: Windows")

elseif(UNIX)
    message(STATUS "Current environment: Linux")

else()
    message(FATAL_ERROR "Unsupported platform.")
endif()

find_package(Vulkan REQUIRED)

get_filename_component(
    Vulkan_LIBRARY_DIR
    "${Vulkan_LIBRARY}"
    DIRECTORY
)

# ============================================================
# shaderc（GLSL/HLSL -> SPIR-V 编译器）发现逻辑
#   不同机器上 shaderc 的提供方式不同，这里统一发现后
#   暴露成 shaderc::shaderc 目标，供各子目录直接链接：
#     候选 1：LunarG Vulkan SDK 自带的合并库（优先）
#             shaderc_combined[d].lib / libshaderc_combined.a
#             自包含、由 SDK 方统一构建，不受本机 MSVC 工具集版本差异影响
#     候选 2：vcpkg 的 shaderc port（拆分库，导出 CMake 包
#             unofficial-shaderc -> unofficial::shaderc::shaderc）
#             注意：vcpkg 预编译库必须与本机 VC++ 工具集版本一致，否则会出现
#             __std_xxx 之类的 STL 符号链接错误
#     候选 3：系统安装的 shaderc（shaderc_combined / shaderc::shaderc）
#   不再假设“shaderc 库一定和 vulkan-1.lib 在同一目录”，
#   从而避免 find_package(Vulkan) 解析到 vcpkg 的 vulkan-1.lib
#   时找不到 shaderc_combinedd.lib 的链接错误。
# ============================================================
set(SHADERC_PROVIDER "")

# ---- 候选 1：Vulkan SDK 自带的合并库（自包含，建议优先） ----
set(_shaderc_lib_dirs "${Vulkan_LIBRARY_DIR}")
set(_shaderc_inc_dirs "")
foreach(_sdk_root "$ENV{VULKAN_SDK}" "$ENV{VK_SDK_PATH}")
    if(_sdk_root)
        list(APPEND _shaderc_lib_dirs "${_sdk_root}/Lib" "${_sdk_root}/lib")
        list(APPEND _shaderc_inc_dirs "${_sdk_root}/Include" "${_sdk_root}/include")
    endif()
endforeach()
find_library(SHADERC_COMBINED_LIB_RELEASE
    NAMES shaderc_combined
    PATHS ${_shaderc_lib_dirs}
    NO_DEFAULT_PATH
)
find_library(SHADERC_COMBINED_LIB_DEBUG
    NAMES shaderc_combinedd
    PATHS ${_shaderc_lib_dirs}
    NO_DEFAULT_PATH
)
# 部分 SDK 只提供 Release 版，Debug 缺失时回退使用 Release 库
if(NOT SHADERC_COMBINED_LIB_DEBUG)
    set(SHADERC_COMBINED_LIB_DEBUG "${SHADERC_COMBINED_LIB_RELEASE}")
endif()

# 由库文件反推 SDK 根目录，确保头文件与库来自同一个 SDK（版本一致）；
# 使用独立变量名，避免复用历史构建目录里可能已过期的 SHADERC_INCLUDE_DIR 缓存。
if(SHADERC_COMBINED_LIB_RELEASE)
    get_filename_component(_shaderc_lib_dir "${SHADERC_COMBINED_LIB_RELEASE}" DIRECTORY)
    get_filename_component(_shaderc_sdk_root "${_shaderc_lib_dir}" DIRECTORY)
    list(INSERT _shaderc_inc_dirs 0
        "${_shaderc_sdk_root}/Include" "${_shaderc_sdk_root}/include")
endif()

if(_shaderc_inc_dirs)
    find_path(SHADERC_SDK_INCLUDE_DIR
        NAMES shaderc/shaderc.hpp
        PATHS ${_shaderc_inc_dirs}
        NO_DEFAULT_PATH
    )
endif()

if(SHADERC_SDK_INCLUDE_DIR AND SHADERC_COMBINED_LIB_RELEASE)
    set(SHADERC_PROVIDER "Vulkan SDK combined lib")
else()
    # ---- 候选 2：vcpkg / 系统包提供的 shaderc ----
    find_package(unofficial-shaderc CONFIG QUIET)
    if(TARGET unofficial::shaderc::shaderc)
        set(SHADERC_PROVIDER "vcpkg unofficial-shaderc (split libs)")
    else()
        find_package(shaderc CONFIG QUIET)
        if(TARGET shaderc_combined)
            set(SHADERC_PROVIDER "system shaderc_combined target")
        elseif(TARGET shaderc::shaderc)
            set(SHADERC_PROVIDER "system shaderc::shaderc")
        endif()
    endif()
endif()

if(NOT SHADERC_PROVIDER)
    message(FATAL_ERROR
        "找不到 shaderc：未找到 Vulkan SDK 自带的 shaderc_combined[d] 库，也未检测到 vcpkg 的 "
        "unofficial-shaderc 或系统 shaderc 包。请安装 LunarG Vulkan SDK（需包含 shaderc 组件），"
        "或执行 `vcpkg install shaderc` 后重新配置。")
endif()

# ---- 统一暴露 shaderc::shaderc ----
add_library(shaderc_interface INTERFACE)

if(SHADERC_PROVIDER STREQUAL "Vulkan SDK combined lib")
    target_include_directories(shaderc_interface INTERFACE "${SHADERC_SDK_INCLUDE_DIR}")
    target_link_libraries(shaderc_interface INTERFACE
        debug "${SHADERC_COMBINED_LIB_DEBUG}"
        optimized "${SHADERC_COMBINED_LIB_RELEASE}"
    )
    message(STATUS "shaderc include dir : ${SHADERC_SDK_INCLUDE_DIR}")
    message(STATUS "shaderc debug lib   : ${SHADERC_COMBINED_LIB_DEBUG}")
    message(STATUS "shaderc release lib : ${SHADERC_COMBINED_LIB_RELEASE}")
elseif(SHADERC_PROVIDER STREQUAL "vcpkg unofficial-shaderc (split libs)")
    target_link_libraries(shaderc_interface INTERFACE unofficial::shaderc::shaderc)
elseif(SHADERC_PROVIDER STREQUAL "system shaderc_combined target")
    target_link_libraries(shaderc_interface INTERFACE shaderc_combined)
else()
    target_link_libraries(shaderc_interface INTERFACE shaderc::shaderc)
endif()

add_library(shaderc::shaderc ALIAS shaderc_interface)
message(STATUS "shaderc: ${SHADERC_PROVIDER}")

message(STATUS "Vulkan found:")
message(STATUS "  Version  : ${Vulkan_VERSION}")
message(STATUS "  Include  : ${Vulkan_INCLUDE_DIRS}")
message(STATUS "  Library  : ${Vulkan_LIBRARIES}")
message(STATUS "  Library_Path  : ${Vulkan_LIBRARY_DIR}")

# 注意：目录名为 Engine（首字母大写），在 Linux/macOS 等大小写敏感的文件系统上
# 写成 engine 会导致 add_subdirectory 找不到目录而配置失败。
add_subdirectory(Engine)
add_subdirectory(Plugins)
add_subdirectory(Editor)

# Unit test
if (GENERATE_TEST_PROGRAME)
    add_subdirectory(Tests)
endif()

# Copy necessary dll
if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../bin/engine.dll)
	file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/../bin/engine.dll DESTINATION ${EXECUTABLE_OUTPUT_PATH})
endif()

message("\n-- Log level: ${LOG_LEVEL}")
message("-- Build target mode: ${CMAKE_BUILD_TYPE}")
message("-- Add executable program to ${EXECUTABLE_OUTPUT_PATH}")
message("-- Generate project successful.\n")
