cmake_minimum_required(VERSION 3.21)
project(fsr_interop_probe LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The probe targets 64-bit Windows.")
endif()

set(FSR_SDK_ROOT "" CACHE PATH "Root of AMD FidelityFX-SDK v2.4.0")
set(fsr_kit "${FSR_SDK_ROOT}/Kits/FidelityFX")
if(NOT EXISTS "${fsr_kit}/api/include/ffx_api.h" OR
   NOT EXISTS "${fsr_kit}/upscalers/include/ffx_upscale.h")
    message(FATAL_ERROR "Set FSR_SDK_ROOT to the root of FidelityFX-SDK v2.4.0.")
endif()

add_executable(fsr-interop-probe main.cpp fsr_session.cpp game_fixture.cpp)
target_compile_features(fsr-interop-probe PRIVATE cxx_std_17)
target_compile_definitions(fsr-interop-probe PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(fsr-interop-probe SYSTEM PRIVATE
    "${fsr_kit}/api/include" "${fsr_kit}/upscalers/include")
target_link_libraries(fsr-interop-probe PRIVATE d3d11 d3d12 d3dcompiler dxgi)

if(MSVC)
    set_property(TARGET fsr-interop-probe PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(fsr-interop-probe PRIVATE /W4 /permissive-)
elseif(MINGW)
    target_compile_options(fsr-interop-probe PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(fsr-interop-probe PRIVATE -municode -static)
endif()

install(TARGETS fsr-interop-probe RUNTIME DESTINATION .)
install(FILES "Pokreni FSR test.cmd" "Proveri formate Mafia III.cmd" run-probe.ps1 PROCITAJ.txt DESTINATION .)
