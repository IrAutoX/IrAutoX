cmake_minimum_required(VERSION 3.24)
project(IrAutoX VERSION 1.0.0 LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "IrAutoX must be built for 32-bit Windows (Win32)")
endif()

if(NOT WEBVIEW2_ROOT)
    message(FATAL_ERROR "WEBVIEW2_ROOT must point to the Microsoft.Web.WebView2 NuGet package")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(IrAutoX WIN32 launcher/main.cpp)
target_include_directories(IrAutoX PRIVATE "${WEBVIEW2_ROOT}/build/native/include")
target_link_directories(IrAutoX PRIVATE "${WEBVIEW2_ROOT}/build/native/x86")
target_compile_definitions(IrAutoX PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(IrAutoX PRIVATE /W4 /permissive- /O1 /Gy /Gw)
target_link_options(IrAutoX PRIVATE /OPT:REF /OPT:ICF /LTCG)
target_link_libraries(IrAutoX PRIVATE WebView2LoaderStatic.lib comctl32 comdlg32 shlwapi shell32 advapi32 ole32 dwmapi)

add_custom_command(TARGET IrAutoX POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/ui" "$<TARGET_FILE_DIR:IrAutoX>/ui"
)
