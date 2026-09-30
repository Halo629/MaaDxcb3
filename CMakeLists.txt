cmake_minimum_required(VERSION 3.16)
project(MaaDxcb3 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    add_compile_options(/utf-8 /W3 /FS /WX-)
    add_definitions(-D_UNICODE -DUNICODE)
else()
    add_compile_options(-Wno-error -Wno-unused-parameter -Wno-unused-variable)
endif()

# Release 也生成 PDB（调试符号），否则 POST_BUILD 拷贝 PDB 时报"文件不存在"
if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG")
endif()

# ─── MaaFramework 安装目录（头文件 + 导入库 + DLL）───────────────
set(MAA_ROOT "G:/workspace/maaframework/MAA-win-x86_64-v5.13.0" CACHE PATH "MaaFramework install prefix")

# ─── meojson（header-only）───────────────────────────────────────
set(MEOJSON_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/third_party" CACHE PATH "meojson include dir")

# ─── 源文件 ──────────────────────────────────────────────────────
file(GLOB CUSTOM_SRCS
    "src/*.cpp"
)
list(REMOVE_ITEM CUSTOM_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/src/Main.cpp")

add_executable(cpp-service
    src/Main.cpp
    ${CUSTOM_SRCS}
)

# 构建后把产物拷贝到 agent/；Debug 配置输出 cpp-service-debug.exe，其余输出 cpp-service.exe。
add_custom_command(TARGET cpp-service POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:cpp-service>"
        "${CMAKE_CURRENT_SOURCE_DIR}/agent/cpp-service$<$<CONFIG:Debug>:-debug>.exe"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_PDB_FILE:cpp-service>"
        "${CMAKE_CURRENT_SOURCE_DIR}/agent/cpp-service$<$<CONFIG:Debug>:-debug>.pdb"
)

target_include_directories(cpp-service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    "${MAA_ROOT}/include"
    ${MEOJSON_INCLUDE}
)

target_link_directories(cpp-service PRIVATE "${MAA_ROOT}/lib")

target_link_libraries(cpp-service PRIVATE
    MaaAgentServer
    MaaFramework
)
