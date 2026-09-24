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
  src/device/numeric_panel.cpp
  src/integrations/codex_client.cpp
  src/integrations/codex_executable.cpp
  src/integrations/home_assistant.cpp
  src/integrations/homepod_media.cpp
  src/integrations/hardware_monitor.cpp
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
target_link_libraries(jonsbo_core PUBLIC hid setupapi)

add_executable(
  jonsbo-display WIN32 src/main.cpp src/app/application.cpp
                       src/app/diagnostics.cpp src/resources/assets.rc)
target_link_libraries(jonsbo-display PRIVATE jonsbo_core)
target_link_options(jonsbo-display PRIVATE /ENTRY:wmainCRTStartup)

option(JONSBO_ENABLE_HARDWARE_MONITOR
       "Build the LibreHardwareMonitor C++/CLI bridge" ON)
set(JONSBO_NUGET_PACKAGES
    "$ENV{LOCALAPPDATA}/JonsboDisplay/packages"
    CACHE PATH
          "Directory containing restored LibreHardwareMonitor NuGet packages")
find_program(
  JONSBO_NUGET_EXE
  NAMES nuget nuget.exe
  HINTS "$ENV{LOCALAPPDATA}/JonsboDisplay/tools")
set(JONSBO_LHM_ASSEMBLY
    "${JONSBO_NUGET_PACKAGES}/LibreHardwareMonitorLib.0.9.6/runtimes/win-x64/lib/net472/LibreHardwareMonitorLib.dll"
)
if(JONSBO_ENABLE_HARDWARE_MONITOR
   AND NOT EXISTS "${JONSBO_LHM_ASSEMBLY}"
   AND JONSBO_NUGET_EXE)
  execute_process(
    COMMAND
      "${JONSBO_NUGET_EXE}" install LibreHardwareMonitorLib -Version 0.9.6
      -Framework net472 -OutputDirectory "${JONSBO_NUGET_PACKAGES}"
      -NonInteractive -DirectDownload
    RESULT_VARIABLE JONSBO_NUGET_RESULT)
endif()
if(JONSBO_ENABLE_HARDWARE_MONITOR AND EXISTS "${JONSBO_LHM_ASSEMBLY}")
  get_filename_component(JONSBO_LHM_ASSEMBLY_DIR "${JONSBO_LHM_ASSEMBLY}"
                         DIRECTORY)
  set(JONSBO_MANAGED_DEPENDENCIES
      "${JONSBO_LHM_ASSEMBLY}"
      "${JONSBO_NUGET_PACKAGES}/BlackSharp.Core.1.0.7/lib/net472/BlackSharp.Core.dll"
      "${JONSBO_NUGET_PACKAGES}/DiskInfoToolkit.1.1.2/lib/net472/DiskInfoToolkit.dll"
      "${JONSBO_NUGET_PACKAGES}/HidSharp.2.6.4/lib/net35/HidSharp.dll"
      "${JONSBO_NUGET_PACKAGES}/RAMSPDToolkit-NDD.1.4.2/lib/net472/RAMSPDToolkit-NDD.dll"
      "${JONSBO_NUGET_PACKAGES}/System.Buffers.4.6.1/lib/net462/System.Buffers.dll"
      "${JONSBO_NUGET_PACKAGES}/System.CodeDom.10.0.2/lib/net462/System.CodeDom.dll"
      "${JONSBO_NUGET_PACKAGES}/System.Management.10.0.2/lib/netstandard2.0/System.Management.dll"
      "${JONSBO_NUGET_PACKAGES}/System.Memory.4.6.3/lib/net462/System.Memory.dll"
      "${JONSBO_NUGET_PACKAGES}/System.Numerics.Vectors.4.6.1/lib/net462/System.Numerics.Vectors.dll"
      "${JONSBO_NUGET_PACKAGES}/System.Runtime.CompilerServices.Unsafe.6.1.2/lib/net462/System.Runtime.CompilerServices.Unsafe.dll"
      "${JONSBO_NUGET_PACKAGES}/System.Security.AccessControl.6.0.0/lib/net461/System.Security.AccessControl.dll"
      "${JONSBO_NUGET_PACKAGES}/System.Security.Principal.Windows.5.0.0/lib/net461/System.Security.Principal.Windows.dll"
      "${JONSBO_NUGET_PACKAGES}/System.Threading.AccessControl.10.0.3/lib/net462/System.Threading.AccessControl.dll"
  )
  add_custom_command(
    TARGET jonsbo-display
    POST_BUILD
    COMMAND
      lib.exe /nologo
      /def:${CMAKE_CURRENT_SOURCE_DIR}/src/hardware_monitor_bridge/mscoree.def
      /machine:x64 /out:$<TARGET_FILE_DIR:jonsbo-display>/mscoree.lib
    COMMAND
      cl.exe /nologo /clr /LD /EHa /MD /std:c++17
      /AI${JONSBO_LHM_ASSEMBLY_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/src/hardware_monitor_bridge/bridge.cpp
      /Fo$<TARGET_FILE_DIR:jonsbo-display>/hardware-monitor-bridge.obj /link
      /libpath:$<TARGET_FILE_DIR:jonsbo-display>
      /out:$<TARGET_FILE_DIR:jonsbo-display>/jonsbo-hardware-monitor.dll
    COMMAND
      ${CMAKE_COMMAND} -E rm -f
            "$<TARGET_FILE_DIR:jonsbo-display>/hardware-monitor-bridge.obj"
            "$<TARGET_FILE_DIR:jonsbo-display>/hardware-monitor-bridge.lib"
            "$<TARGET_FILE_DIR:jonsbo-display>/hardware-monitor-bridge.exp"
      "$<TARGET_FILE_DIR:jonsbo-display>/mscoree.lib"
      "$<TARGET_FILE_DIR:jonsbo-display>/mscoree.exp"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${JONSBO_MANAGED_DEPENDENCIES}
            "$<TARGET_FILE_DIR:jonsbo-display>"
    VERBATIM)
else()
  message(
    WARNING
      "LibreHardwareMonitor bridge disabled: NuGet package is unavailable.")
endif()

install(TARGETS jonsbo-display RUNTIME DESTINATION .)
install(FILES LICENSE LICENSE-SDK-EXCEPTION DESTINATION .)
install(
  FILES assets/lucide/LICENSE
  DESTINATION licenses
  RENAME Lucide-LICENSE)
if(JONSBO_ENABLE_HARDWARE_MONITOR AND EXISTS "${JONSBO_LHM_ASSEMBLY}")
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/jonsbo-hardware-monitor.dll"
                ${JONSBO_MANAGED_DEPENDENCIES} DESTINATION .)
endif()

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
