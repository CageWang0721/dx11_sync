cmake_minimum_required(VERSION 3.20)
project(dx11_sync VERSION 3.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# UTF-8
add_compile_options(/utf-8)

# 静态 CRT = 单文件
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Win10+ SDK
add_definitions(-D_WIN32_WINNT=0x0A00 -DNTDDI_VERSION=0x0A00000B -DWIN32_LEAN_AND_MEAN)

# 不嵌入默认清单，改用外部 app.manifest（管理员权限 + DPI）
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /MANIFEST:NO")

add_executable(dx11_sync WIN32
    src/main.cpp
    src/app_window.cpp
    src/sync_engine.cpp
    src/window_manager.cpp
    src/key_blacklist.cpp
    resource.rc
)
target_include_directories(dx11_sync PRIVATE src)
target_link_libraries(dx11_sync PRIVATE user32 gdi32 dwmapi d2d1 dwrite comctl32)

# 把 app.manifest 复制到 exe 旁边
add_custom_command(TARGET dx11_sync POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/app.manifest"
        "$<TARGET_FILE_DIR:dx11_sync>/dx11_sync.exe.manifest"
)
