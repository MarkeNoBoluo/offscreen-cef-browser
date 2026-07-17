cmake_minimum_required(VERSION 3.21)

if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()

project(offscreen_cef_browser VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "V1 Stage 1 supports Windows x64 only")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "V1 Stage 1 supports Windows x64 only")
endif()

if(MSVC)
  if(MSVC_VERSION LESS 1910 OR MSVC_VERSION GREATER_EQUAL 1920)
    message(FATAL_ERROR "V1 Stage 1 requires VS2017/MSVC 19.1x")
  endif()
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
  message(FATAL_ERROR "V1 Stage 1 requires VS2017/MSVC 19.1x")
endif()

option(OFFSCREEN_BUILD_APP "Build the Qt + CEF browser application" ON)
option(OFFSCREEN_BUILD_TESTS "Build unit tests" ON)

set(CMAKE_CONFIGURATION_TYPES Debug Release CACHE STRING "Supported configurations" FORCE)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/msvc-2017-x64")

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
)
target_include_directories(offscreen_core PUBLIC src)
target_compile_features(offscreen_core PUBLIC cxx_std_17)
target_compile_definitions(offscreen_core PRIVATE
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Debug>>:_HAS_ITERATOR_DEBUGGING=0>
)

if(OFFSCREEN_BUILD_TESTS)
  enable_testing()

  add_executable(app_config_tests
    tests/app_config_tests.cpp
  )
  target_link_libraries(app_config_tests PRIVATE offscreen_core)
  target_compile_features(app_config_tests PRIVATE cxx_std_17)
  target_compile_definitions(app_config_tests PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Debug>>:_HAS_ITERATOR_DEBUGGING=0>
  )

  add_executable(browser_geometry_tests
    tests/browser_geometry_tests.cpp
  )
  target_link_libraries(browser_geometry_tests PRIVATE offscreen_core)
  target_compile_features(browser_geometry_tests PRIVATE cxx_std_17)
  target_compile_definitions(browser_geometry_tests PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Debug>>:_HAS_ITERATOR_DEBUGGING=0>
  )

  add_executable(browser_close_state_tests
    tests/browser_close_state_tests.cpp
  )
  target_link_libraries(browser_close_state_tests PRIVATE offscreen_core)
  target_compile_features(browser_close_state_tests PRIVATE cxx_std_17)
  target_compile_definitions(browser_close_state_tests PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Debug>>:_HAS_ITERATOR_DEBUGGING=0>
  )

  add_executable(browser_window_title_tests
    tests/browser_window_title_tests.cpp
  )
  target_link_libraries(browser_window_title_tests PRIVATE offscreen_core)
  target_compile_features(browser_window_title_tests PRIVATE cxx_std_17)
  target_compile_definitions(browser_window_title_tests PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Debug>>:_HAS_ITERATOR_DEBUGGING=0>
  )

  add_executable(browser_paint_geometry_tests
    tests/browser_paint_geometry_tests.cpp
  )
  target_link_libraries(browser_paint_geometry_tests PRIVATE offscreen_core)
  target_compile_features(browser_paint_geometry_tests PRIVATE cxx_std_17)
  target_compile_definitions(browser_paint_geometry_tests PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Debug>>:_HAS_ITERATOR_DEBUGGING=0>
  )

  add_executable(browser_input_mapping_tests
    tests/browser_input_mapping_tests.cpp
  )
  target_link_libraries(browser_input_mapping_tests PRIVATE offscreen_core)
  target_compile_features(browser_input_mapping_tests PRIVATE cxx_std_17)
  target_compile_definitions(browser_input_mapping_tests PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Debug>>:_HAS_ITERATOR_DEBUGGING=0>
  )

  add_executable(browser_ime_core_tests
    tests/browser_ime_core_tests.cpp
  )
  target_link_libraries(browser_ime_core_tests PRIVATE offscreen_core imm32)
  target_compile_features(browser_ime_core_tests PRIVATE cxx_std_17)
  target_compile_definitions(browser_ime_core_tests PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Debug>>:_HAS_ITERATOR_DEBUGGING=0>
  )

  add_custom_target(offscreen_core_tests
    DEPENDS app_config_tests browser_geometry_tests browser_close_state_tests browser_window_title_tests browser_paint_geometry_tests browser_input_mapping_tests browser_ime_core_tests
  )

  add_test(NAME app_config_tests COMMAND app_config_tests)
  add_test(NAME browser_geometry_tests COMMAND browser_geometry_tests)
  add_test(NAME browser_close_state_tests COMMAND browser_close_state_tests)
  add_test(NAME browser_window_title_tests COMMAND browser_window_title_tests)
  add_test(NAME browser_paint_geometry_tests COMMAND browser_paint_geometry_tests)
  add_test(NAME browser_input_mapping_tests COMMAND browser_input_mapping_tests)
  add_test(NAME browser_ime_core_tests COMMAND browser_ime_core_tests)
endif()

if(OFFSCREEN_BUILD_APP)
  if(NOT CEF_ROOT)
    message(FATAL_ERROR "CEF_ROOT must point to a Windows x64 CEF binary distribution")
  endif()

  file(TO_CMAKE_PATH "${CEF_ROOT}" CEF_ROOT)
  if(NOT CEF_ROOT MATCHES "(^|/)cef_binary_[^/]*windows64_vs2017([^/]*)(/|$)")
    message(FATAL_ERROR "V1 Stage 1 requires a CEF windows64_vs2017 binary distribution")
  endif()
  list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
  find_package(CEF REQUIRED)

  file(STRINGS "${CEF_INCLUDE_PATH}/include/cef_version.h" CEF_VERSION_MAJOR_LINE REGEX "#define CEF_VERSION_MAJOR [0-9]+")
  string(REGEX REPLACE ".*CEF_VERSION_MAJOR ([0-9]+).*" "\\1" CEF_VERSION_MAJOR_VALUE "${CEF_VERSION_MAJOR_LINE}")
  if(NOT CEF_VERSION_MAJOR_VALUE EQUAL 96)
    message(FATAL_ERROR "V1 Stage 1 requires CEF major version 96")
  endif()
  if(MSVC AND CEF_VERSION_MAJOR_VALUE GREATER_EQUAL 142 AND MSVC_VERSION LESS 1930)
    message(FATAL_ERROR
      "CEF ${CEF_VERSION_MAJOR_VALUE} requires Visual Studio 2022 or a compatible newer MSVC toolchain. "
      "Use a CEF binary distribution compatible with Qt 5.14.2 MSVC2017 64bit, "
      "or move V1 to a newer MSVC toolchain.")
  elseif(MSVC AND CEF_VERSION_MAJOR_VALUE GREATER_EQUAL 102 AND MSVC_VERSION LESS 1920)
    message(FATAL_ERROR
      "CEF ${CEF_VERSION_MAJOR_VALUE} requires Visual Studio 2019 or newer according to its binary distribution CMake files. "
      "V1 is fixed to Qt 5.14.2 MSVC2017 64bit, so use a CEF binary distribution built for VS2017/x64.")
  endif()

  find_package(Qt5 5.14.2 EXACT REQUIRED COMPONENTS Widgets)
  file(TO_CMAKE_PATH "${Qt5_DIR}" QT5_CMAKE_DIR)
  if(NOT QT5_CMAKE_DIR MATCHES "(^|/)msvc2017_64(/|$)")
    message(FATAL_ERROR "V1 Stage 1 requires the Qt 5.14.2 msvc2017_64 package")
  endif()

  add_subdirectory("${CEF_LIBCEF_DLL_WRAPPER_PATH}" "${CMAKE_BINARY_DIR}/libcef_dll_wrapper")
  if(MSVC)
    target_compile_options(libcef_dll_wrapper PRIVATE /permissive-)
  endif()
  ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")
  SET_CEF_TARGET_OUT_DIR()

  add_subdirectory(src)
endif()
