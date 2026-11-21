cmake_minimum_required(VERSION 3.24)
project(IrAutoX VERSION 2.0.0 LANGUAGES CXX)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
 message(FATAL_ERROR "IrAutoX must be built for 32-bit Windows (Win32)")
endif()
include(FetchContent)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.91.9b GIT_SHALLOW TRUE)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
 FetchContent_Populate(imgui)
endif()
FetchContent_Declare(nlohmann_json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.12.0 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(nlohmann_json)
add_library(imgui STATIC
 "${imgui_SOURCE_DIR}/imgui.cpp"
 "${imgui_SOURCE_DIR}/imgui_draw.cpp"
 "${imgui_SOURCE_DIR}/imgui_tables.cpp"
 "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
 "${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp"
 "${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp")
target_include_directories(imgui PUBLIC "${imgui_SOURCE_DIR}")
target_compile_definitions(imgui PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
add_executable(IrAutoX WIN32 launcher/native_main.cpp launcher/discord_presence.cpp launcher/discord_presence.h)
target_include_directories(IrAutoX PRIVATE "${imgui_SOURCE_DIR}")
target_compile_definitions(IrAutoX PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(IrAutoX PRIVATE /W4 /permissive- /O2 /Gy /Gw)
target_link_options(IrAutoX PRIVATE /OPT:REF /OPT:ICF /LTCG)
target_link_libraries(IrAutoX PRIVATE imgui nlohmann_json::nlohmann_json d3d11 dxgi d3dcompiler winhttp windowscodecs comctl32 comdlg32 shlwapi shell32 advapi32 ole32 dwmapi)
