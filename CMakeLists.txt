# SPDX-License-Identifier: GPL-3.0-only Additional permission: see
# LICENSE-SDK-EXCEPTION.
cmake_minimum_required(VERSION 3.24)
project(
  JonsboDisplay
  VERSION 0.2.0
  LANGUAGES CXX RC)
if(NOT WIN32
   OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8
   OR NOT MSVC)
  message(FATAL_ERROR "Requires Windows x64 and MSVC.")
endif()

add_library(
  jonsbo_core STATIC
  src/core/utilities.cpp
  src/app/desktop_panel.cpp
  src/device/display_session.cpp
  src/device/display_transport.cpp
  src/integrations/codex_client.cpp
  src/integrations/codex_executable.cpp
  src/integrations/home_assistant.cpp
  src/integrations/picture_in_picture.cpp
  src/integrations/system_media.cpp
  src/integrations/system_telemetry.cpp
  src/rendering/renderer.cpp
  src/rendering/drawing.cpp
  src/rendering/pages.cpp
  src/rendering/media.cpp
  src/rendering/desktop.cpp)
target_compile_features(jonsbo_core PUBLIC cxx_std_20)
set_target_properties(jonsbo_core PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(jonsbo_core PUBLIC src)
target_compile_definitions(jonsbo_core PUBLIC UNICODE _UNICODE
                                              WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(jonsbo_core PUBLIC /utf-8 /EHsc)
target_link_libraries(
  jonsbo_core
  PUBLIC d3d11
         dxgi
         d2d1
         dcomp
         dxguid
         dwrite
         windowscodecs
         ole32
         user32
         gdi32
         iphlpapi
         pdh
         powrprof
         windowsapp
         shlwapi
         dwmapi
         winhttp
         advapi32)

add_executable(
  jonsbo-display WIN32 src/main.cpp src/app/application.cpp
                       src/app/diagnostics.cpp src/resources/assets.rc)
target_link_libraries(jonsbo-display PRIVATE jonsbo_core)
target_link_options(jonsbo-display PRIVATE /ENTRY:wmainCRTStartup)
install(TARGETS jonsbo-display RUNTIME DESTINATION .)
install(FILES LICENSE LICENSE-SDK-EXCEPTION DESTINATION .)
install(
  FILES assets/lucide/LICENSE
  DESTINATION licenses
  RENAME Lucide-LICENSE)

include(CTest)
if(BUILD_TESTING)
  add_executable(jonsbo-model-tests tests/model_tests.cpp)
  target_link_libraries(jonsbo-model-tests PRIVATE jonsbo_core)
  add_test(NAME models COMMAND jonsbo-model-tests)
endif()
find_program(CLANG_FORMAT_EXE clang-format)
if(CLANG_FORMAT_EXE)
  file(GLOB_RECURSE PROJECT_CPP CONFIGURE_DEPENDS src/*.cpp src/*.hpp
       tests/*.cpp)
  add_custom_target(
    format
    COMMAND ${CLANG_FORMAT_EXE} -i ${PROJECT_CPP}
    VERBATIM)
  add_custom_target(
    format-check
    COMMAND ${CLANG_FORMAT_EXE} --dry-run --Werror ${PROJECT_CPP}
    VERBATIM)
endif()
