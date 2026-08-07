cmake_minimum_required(VERSION 3.21)

if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()

project(offscreen_cef_browser VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "This project supports Windows only")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(OFFSCREEN_ARCH "x64")
  set(OFFSCREEN_CEF_DISTRIBUTION "windows64_minimal")
  set(OFFSCREEN_QT_PACKAGE "msvc2022_64")
else()
  message(FATAL_ERROR "CEF 100 configuration supports x64 only")
endif()

if(MSVC)
  if(MSVC_VERSION LESS 1930 OR MSVC_VERSION GREATER_EQUAL 1950)
    message(FATAL_ERROR "This configuration requires VS2022/MSVC 19.3x or 19.4x")
  endif()
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  # Source files include Chinese comments; keep MSVC from interpreting UTF-8 as the active ANSI code page.
  add_compile_options(/utf-8)
else()
  message(FATAL_ERROR "This configuration requires VS2022/MSVC 19.3x or 19.4x")
endif()

option(OFFSCREEN_BUILD_APP "Build the Qt + CEF browser application" ON)
option(OFFSCREEN_BUILD_TESTS "Build unit tests" OFF)
option(OFFSCREEN_BUILD_EMBEDDING_DEMOS "Build the CEF embedding demos" ON)

set(CMAKE_CONFIGURATION_TYPES Debug Release CACHE STRING "Supported configurations" FORCE)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/msvc-2022-${OFFSCREEN_ARCH}")

add_library(offscreen_core STATIC
  src/app/app_config.cpp
  src/app/app_config.h
  src/app/browser_window_title.cpp
  src/app/browser_window_title.h
  src/browser/browser_close_state.cpp
  src/browser/browser_close_state.h
  src/browser/browser_geometry.cpp
  src/browser/browser_geometry.h
  src/browser/browser_input_mapping.cpp
  src/browser/browser_input_mapping.h
  src/browser/browser_ime_core.cpp
  src/browser/browser_ime_core.h
  src/browser/browser_paint_geometry.cpp
  src/browser/browser_paint_geometry.h
  src/browser/osr_render_log.cpp
  src/browser/osr_render_log.h
  src/browser/render_stats.cpp
  src/browser/render_stats.h
  src/browser/render_stats_log.cpp
  src/browser/render_stats_log.h
  src/browser/run_lifecycle.cpp
  src/browser/run_lifecycle.h
  src/utils_log/FileLog.cpp
  src/utils_log/FileLog.h
  src/utils_log/Logger.cpp
  src/utils_log/Logger.h
  src/utils_log/LogStream.cpp
  src/utils_log/LogStream.h
  src/utils_log/TTime.h
  src/utils_log/NoCopyable.h
)
target_include_directories(offscreen_core PUBLIC src)
target_compile_features(offscreen_core PUBLIC cxx_std_17)
target_link_libraries(offscreen_core PRIVATE psapi)
target_compile_definitions(offscreen_core PRIVATE
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Debug>>:_HAS_ITERATOR_DEBUGGING=0>
)

if(OFFSCREEN_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(OFFSCREEN_BUILD_APP)
  if(NOT CEF_ROOT)
    message(FATAL_ERROR "CEF_ROOT must point to a Windows ${OFFSCREEN_ARCH} CEF binary distribution")
  endif()

  file(TO_CMAKE_PATH "${CEF_ROOT}" CEF_ROOT)
  if(NOT CEF_ROOT MATCHES "(^|/)cef_binary_[^/]*${OFFSCREEN_CEF_DISTRIBUTION}(/|$)")
    message(FATAL_ERROR "This build requires a CEF ${OFFSCREEN_CEF_DISTRIBUTION} binary distribution")
  endif()
  list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
  set(CEF_RUNTIME_LIBRARY_FLAG "/MD" CACHE STRING "CEF runtime library flag" FORCE)
  find_package(CEF REQUIRED)

  file(STRINGS "${CEF_INCLUDE_PATH}/include/cef_version.h" CEF_VERSION_MAJOR_LINE REGEX "#define CEF_VERSION_MAJOR [0-9]+")
  string(REGEX REPLACE ".*CEF_VERSION_MAJOR ([0-9]+).*" "\\1" CEF_VERSION_MAJOR_VALUE "${CEF_VERSION_MAJOR_LINE}")
  if(NOT CEF_VERSION_MAJOR_VALUE EQUAL 100)
    message(FATAL_ERROR "This build requires CEF major version 100")
  endif()

  find_package(Qt6 6.9.3 EXACT REQUIRED COMPONENTS Widgets)
  file(TO_CMAKE_PATH "${Qt6_DIR}" QT6_CMAKE_DIR)
  if(NOT QT6_CMAKE_DIR MATCHES "(^|/)${OFFSCREEN_QT_PACKAGE}(/|$)")
    message(FATAL_ERROR "This build requires the Qt 6.9.3 ${OFFSCREEN_QT_PACKAGE} package")
  endif()
  get_filename_component(OFFSCREEN_QT_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
  find_program(OFFSCREEN_WINDEPLOYQT_EXECUTABLE
    NAMES windeployqt.exe windeployqt
    HINTS "${OFFSCREEN_QT_PREFIX}/bin"
    NO_DEFAULT_PATH
  )
  if(NOT OFFSCREEN_WINDEPLOYQT_EXECUTABLE)
    message(WARNING "windeployqt was not found under ${OFFSCREEN_QT_PREFIX}/bin; install output will not include Qt runtime files")
  endif()

  add_subdirectory("${CEF_LIBCEF_DLL_WRAPPER_PATH}" "${CMAKE_BINARY_DIR}/libcef_dll_wrapper")
  if(MSVC)
    target_compile_options(libcef_dll_wrapper PRIVATE /permissive-)
  endif()
  ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")
  SET_CEF_TARGET_OUT_DIR()

  add_subdirectory(src)

  if(OFFSCREEN_BUILD_EMBEDDING_DEMOS)
    add_subdirectory(embedding_demo)
  endif()
endif()
