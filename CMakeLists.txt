cmake_minimum_required(VERSION 3.21)
project(IrAutoX VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "IrAutoX currently supports Windows only")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Configure IrAutoX with a 32-bit toolchain (-A Win32)")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(
    minhook
    URL https://github.com/TsudaKageyu/minhook/archive/refs/tags/v1.3.3.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(minhook)

add_executable(IrAutoX WIN32 launcher/main.cpp)
target_compile_definitions(IrAutoX PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(IrAutoX PRIVATE comdlg32 shell32 advapi32)

add_library(IrAutoXHook SHARED hook/dllmain.cpp)
target_compile_definitions(IrAutoXHook PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(IrAutoXHook PRIVATE minhook d3d9)
set_target_properties(IrAutoXHook PROPERTIES OUTPUT_NAME "IrAutoXHook")

if(MSVC)
    target_compile_options(IrAutoX PRIVATE /W4 /permissive-)
    target_compile_options(IrAutoXHook PRIVATE /W4 /permissive-)
endif()

install(TARGETS IrAutoX IrAutoXHook RUNTIME DESTINATION . LIBRARY DESTINATION .)
