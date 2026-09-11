message("-- Generating Engine Library")

# Load CPython
message("-- Load CPython")
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
include_directories(${Python3_INCLUDE_DIRS})
message("-- Python3 include path: ${Python3_INCLUDE_DIRS}")
message("-- Python3 libraries: ${Python3_LIBRARIES}")

file(GLOB_RECURSE ENGINE_SOURCE_FILES
    CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

# macOS 平台下添加 Objective-C++ 文件
if(APPLE)
    list(APPEND ENGINE_SOURCE_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/Platform/PlatformMacOS.mm
    )
endif()

add_library(engine SHARED ${ENGINE_SOURCE_FILES})

# Output .dll to bin directly.
set_target_properties(engine PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${EXECUTABLE_OUTPUT_PATH}
)

target_compile_definitions(engine PUBLIC -DDEXPORT) 
message("-- STB Image Path: ${STB_DIR}")

target_include_directories(
    engine 
    PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${STB_DIR}
    ${Python3_INCLUDE_DIRS}
)
# 说明：UL::Logger / assimp::assimp / nlohmann_json::nlohmann_json 的 include 目录
# 由各自的 target 通过 target_link_libraries 自动传递，不能当作目录名写进这里。

# Vulkan 与 shaderc
# shaderc::shaderc 由顶层 CMakeLists.txt 统一发现并暴露，
# 具体是 vcpkg 拆分库还是 Vulkan SDK 的 shaderc_combined 由环境决定。
target_link_libraries(engine PUBLIC Vulkan::Vulkan)
target_link_libraries(engine PUBLIC shaderc::shaderc)

if(APPLE)
    # MacOS screen
    target_link_libraries(engine PUBLIC "-framework CoreFoundation")
    target_link_libraries(engine PUBLIC "-framework Metal")
    target_link_libraries(engine PUBLIC "-framework QuartzCore")
    target_link_libraries(engine PUBLIC "-framework Cocoa")
    target_link_libraries(engine PUBLIC "-framework AppKit")
    target_link_libraries(engine PUBLIC "-framework CoreData")

    # other frameworks
    # target_link_libraries(program PUBLIC "-framework ")

    set_target_properties(engine PROPERTIES LINK_FLAGS "-Wl,-F/Library/Frameworks")
endif()

target_link_libraries(engine PUBLIC 
    ${Python3_LIBRARIES} 
    UL::Logger 
    nlohmann_json::nlohmann_json
    assimp::assimp
)

if(WIN32)
    add_custom_command(TARGET engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:assimp::assimp>
        $<TARGET_FILE_DIR:engine>)
endif()

message("-- Generated Engine Library\n")
