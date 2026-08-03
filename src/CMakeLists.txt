set(CMAKE_AUTOMOC ON)

set(OFFSCREEN_RUNTIME_SRCS
  app/browser_app.cpp
  app/browser_app.h
  app/diagnostic_log.h
  runtime/cef_runtime.cpp
  offscreen_cef/cef_runtime.h
)

set(OFFSCREEN_WIDGET_SRCS
  browser/browser_frame.cpp
  browser/browser_frame.h
  browser/browser_client.cpp
  browser/browser_client.h
  browser/browser_geometry.cpp
  browser/browser_geometry.h
  browser/browser_ime_handler.cpp
  browser/browser_ime_handler.h
  browser/browser_service.cpp
  browser/browser_service.h
  browser/osr_render_handler.cpp
  browser/osr_render_handler.h
  browser/tab_manager.cpp
  browser/tab_manager.h
  qt/browser_widget.cpp
  qt/browser_widget.h
  qt/cef_tabbed_browser.cpp
  qt/cef_web_view.cpp
  offscreen_cef/cef_tabbed_browser.h
  offscreen_cef/cef_web_view.h
)

add_library(offscreen_cef_runtime STATIC ${OFFSCREEN_RUNTIME_SRCS})
add_library(offscreen_cef::runtime ALIAS offscreen_cef_runtime)
target_include_directories(offscreen_cef_runtime
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CEF_INCLUDE_PATH}
)
target_link_libraries(offscreen_cef_runtime
  PUBLIC
    Qt6::Widgets
    libcef_lib
    libcef_dll_wrapper
    ${CEF_STANDARD_LIBS}
    imm32
)
target_compile_features(offscreen_cef_runtime PUBLIC cxx_std_17)
target_compile_definitions(offscreen_cef_runtime PRIVATE
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Debug>>:_HAS_ITERATOR_DEBUGGING=0>
)

add_library(offscreen_cef_widgets STATIC ${OFFSCREEN_WIDGET_SRCS})
add_library(offscreen_cef::widgets ALIAS offscreen_cef_widgets)
target_include_directories(offscreen_cef_widgets
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${CEF_INCLUDE_PATH}
)
target_link_libraries(offscreen_cef_widgets
  PUBLIC
    offscreen_core
    offscreen_cef_runtime
)
target_compile_features(offscreen_cef_widgets PUBLIC cxx_std_17)
target_compile_definitions(offscreen_cef_widgets PRIVATE
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Debug>>:_HAS_ITERATOR_DEBUGGING=0>
)

add_executable(offscreen_cef_browser app/main.cpp)
SET_EXECUTABLE_TARGET_PROPERTIES(offscreen_cef_browser)
target_include_directories(offscreen_cef_browser PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(offscreen_cef_browser
  PRIVATE
    offscreen_cef_widgets
)
target_compile_features(offscreen_cef_browser PRIVATE cxx_std_17)
target_compile_definitions(offscreen_cef_browser PRIVATE
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Debug>>:_HAS_ITERATOR_DEBUGGING=0>
)
target_link_options(offscreen_cef_browser PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/MANIFEST:EMBED>
  $<$<CXX_COMPILER_ID:MSVC>:/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/windows_compatibility.manifest>
)

add_executable(offscreen_cef_subprocess subprocess/subprocess_main.cpp)
SET_EXECUTABLE_TARGET_PROPERTIES(offscreen_cef_subprocess)
target_include_directories(offscreen_cef_subprocess PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(offscreen_cef_subprocess
  PRIVATE
    offscreen_cef_runtime
)
target_compile_features(offscreen_cef_subprocess PRIVATE cxx_std_17)
target_compile_definitions(offscreen_cef_subprocess PRIVATE
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Debug>>:_HAS_ITERATOR_DEBUGGING=0>
)
target_link_options(offscreen_cef_subprocess PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/MANIFEST:EMBED>
  $<$<CXX_COMPILER_ID:MSVC>:/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/windows_compatibility.manifest>
)

add_dependencies(offscreen_cef_browser libcef_dll_wrapper offscreen_cef_subprocess)
add_dependencies(offscreen_cef_subprocess libcef_dll_wrapper)

function(offscreen_cef_deploy target)
  if(NOT TARGET ${target})
    message(FATAL_ERROR "offscreen_cef_deploy requires an existing target: ${target}")
  endif()

  add_dependencies(${target} offscreen_cef_subprocess)
  COPY_FILES("${target}" "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "$<TARGET_FILE_DIR:${target}>")
  COPY_FILES("${target}" "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "$<TARGET_FILE_DIR:${target}>")
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:offscreen_cef_subprocess>"
      "$<TARGET_FILE_DIR:${target}>/offscreen_cef_subprocess.exe"
    VERBATIM
  )
endfunction()

offscreen_cef_deploy(offscreen_cef_browser)

install(TARGETS offscreen_cef_browser offscreen_cef_subprocess RUNTIME DESTINATION .)
install(DIRECTORY "${CEF_BINARY_DIR}/" DESTINATION . FILES_MATCHING PATTERN "*.dll" PATTERN "*.bin" PATTERN "*.json")
install(DIRECTORY "${CEF_RESOURCE_DIR}/" DESTINATION .)
