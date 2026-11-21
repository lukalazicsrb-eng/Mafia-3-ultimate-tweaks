cmake_minimum_required(VERSION 3.21)
project(mafia_f8_bridge LANGUAGES CXX)
if(MSVC)
    # Temporary beta binaries should not require a separate VC++ redistributable.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(mafia_menu_state STATIC menu_state.cpp)
target_include_directories(mafia_menu_state PUBLIC .)
target_compile_features(mafia_menu_state PUBLIC cxx_std_17)

add_executable(test_mafia_menu_state test_menu.cpp)
target_link_libraries(test_mafia_menu_state PRIVATE mafia_menu_state)
enable_testing()
add_test(NAME f8_menu_state COMMAND test_mafia_menu_state)

if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_library(mafia_f8_experimental SHARED native_f8_adapter.cpp)
    target_compile_features(mafia_f8_experimental PRIVATE cxx_std_17)
    target_link_libraries(mafia_f8_experimental PRIVATE mafia_menu_state bcrypt)
    target_compile_definitions(mafia_f8_experimental PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

    # Read-only TAA input probe. No AMD upscaling or texture writes.
    add_library(mafia_taa_trace SHARED taa_trace.cpp)
    target_compile_features(mafia_taa_trace PRIVATE cxx_std_17)
    target_link_libraries(mafia_taa_trace PRIVATE bcrypt d3d11 dxgi)

    # The only DLL that replaces the original on an explicitly opt-in beta install.
    add_library(mafia_input_proxy SHARED dinput8_proxy.cpp)
    set_target_properties(mafia_input_proxy PROPERTIES OUTPUT_NAME "dinput8")
    target_compile_features(mafia_input_proxy PRIVATE cxx_std_17)
endif()
