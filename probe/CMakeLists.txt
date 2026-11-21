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

add_executable(fsr-interop-probe main.cpp fsr_session.cpp game_fixture.cpp live_fsr_bridge.cpp)
target_compile_features(fsr-interop-probe PRIVATE cxx_std_17)
target_compile_definitions(fsr-interop-probe PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(fsr-interop-probe SYSTEM PRIVATE
    "${fsr_kit}/api/include" "${fsr_kit}/upscalers/include")
target_link_libraries(fsr-interop-probe PRIVATE d3d11 d3d12 d3dcompiler dxgi)

# Real-frame AMD backend DLL, *not* an injection mod or F8 renderer yet.
add_library(mafia_live_fsr_backend SHARED
    live_fsr_api.cpp live_fsr_bridge.cpp fsr_session.cpp game_fixture.cpp)
target_compile_features(mafia_live_fsr_backend PRIVATE cxx_std_17)
target_compile_definitions(mafia_live_fsr_backend PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX MAFIA_FSR_BACKEND_BUILD)
target_include_directories(mafia_live_fsr_backend SYSTEM PRIVATE
    "${fsr_kit}/api/include" "${fsr_kit}/upscalers/include")
target_link_libraries(mafia_live_fsr_backend PRIVATE d3d11 d3d12 d3dcompiler dxgi)
set_target_properties(mafia_live_fsr_backend PROPERTIES OUTPUT_NAME mafia_live_fsr_backend)
enable_testing()
add_executable(test_live_fsr_abi test_live_fsr_abi.cpp)
target_compile_features(test_live_fsr_abi PRIVATE cxx_std_17)
target_link_libraries(test_live_fsr_abi PRIVATE mafia_live_fsr_backend)
add_test(NAME live_fsr_abi_guard COMMAND test_live_fsr_abi)



if(MSVC)
    set_property(TARGET fsr-interop-probe PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(fsr-interop-probe PRIVATE /W4 /permissive-)
elseif(MINGW)
    target_compile_options(fsr-interop-probe PRIVATE -Wall -Wextra -Wpedantic)
    target_link_options(fsr-interop-probe PRIVATE -municode -static)
endif()

install(TARGETS fsr-interop-probe mafia_live_fsr_backend RUNTIME DESTINATION .)
install(FILES "Pokreni FSR test.cmd" "Proveri formate Mafia III.cmd" run-probe.ps1 PROCITAJ.txt live_fsr_api.h LIVE_BACKEND_README.md DESTINATION .)
