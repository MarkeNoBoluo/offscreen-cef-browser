set(OFFSCREEN_APP_SRCS
  app/app_config.cpp
  app/app_config.h
  app/browser_app.cpp
  app/browser_app.h
  app/browser_window_title.cpp
  app/browser_window_title.h
  app/diagnostic_log.h
  app/main.cpp
  browser/browser_close_state.cpp
  browser/browser_close_state.h
  browser/browser_frame.cpp
  browser/browser_frame.h
  browser/browser_client.cpp
  browser/browser_client.h
  browser/browser_geometry.cpp
  browser/browser_geometry.h
  browser/browser_service.cpp
  browser/browser_service.h
  browser/osr_render_handler.cpp
  browser/osr_render_handler.h
  qt/browser_widget.cpp
  qt/browser_widget.h
)

set(OFFSCREEN_SUBPROCESS_SRCS
  app/browser_app.cpp
  app/browser_app.h
  app/diagnostic_log.h
  subprocess/subprocess_main.cpp
)

add_executable(offscreen_cef_browser ${OFFSCREEN_APP_SRCS})
SET_EXECUTABLE_TARGET_PROPERTIES(offscreen_cef_browser)
target_include_directories(offscreen_cef_browser PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(offscreen_cef_browser
  PRIVATE
    offscreen_core
    Qt5::Widgets
    libcef_lib
    libcef_dll_wrapper
    ${CEF_STANDARD_LIBS}
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

add_executable(offscreen_cef_subprocess ${OFFSCREEN_SUBPROCESS_SRCS})
SET_EXECUTABLE_TARGET_PROPERTIES(offscreen_cef_subprocess)
target_include_directories(offscreen_cef_subprocess PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(offscreen_cef_subprocess
  PRIVATE
    libcef_lib
    libcef_dll_wrapper
    ${CEF_STANDARD_LIBS}
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

COPY_FILES("offscreen_cef_browser" "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "$<TARGET_FILE_DIR:offscreen_cef_browser>")
COPY_FILES("offscreen_cef_browser" "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "$<TARGET_FILE_DIR:offscreen_cef_browser>")

install(TARGETS offscreen_cef_browser offscreen_cef_subprocess RUNTIME DESTINATION .)
install(DIRECTORY "${CEF_BINARY_DIR}/" DESTINATION . FILES_MATCHING PATTERN "*.dll" PATTERN "*.bin" PATTERN "*.json")
install(DIRECTORY "${CEF_RESOURCE_DIR}/" DESTINATION .)
