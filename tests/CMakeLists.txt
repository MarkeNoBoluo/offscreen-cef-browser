function(add_offscreen_core_test name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE offscreen_core ${ARGN})
  target_compile_features(${name} PRIVATE cxx_std_17)
  target_compile_definitions(${name} PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Debug>>:_HAS_ITERATOR_DEBUGGING=0>
  )
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_offscreen_core_test(app_config_tests app_config_tests.cpp)
add_offscreen_core_test(browser_geometry_tests browser_geometry_tests.cpp)
add_offscreen_core_test(browser_close_state_tests browser_close_state_tests.cpp)
add_offscreen_core_test(browser_window_title_tests browser_window_title_tests.cpp)
add_offscreen_core_test(browser_paint_geometry_tests browser_paint_geometry_tests.cpp)
add_offscreen_core_test(browser_input_mapping_tests browser_input_mapping_tests.cpp)
add_offscreen_core_test(browser_ime_core_tests browser_ime_core_tests.cpp imm32)
add_offscreen_core_test(render_stats_tests render_stats_tests.cpp)
add_offscreen_core_test(gpu_frame_state_tests gpu_frame_state_tests.cpp)
add_offscreen_core_test(gpu_frame_bridge_tests gpu_frame_bridge_tests.cpp
  d3d11 dxgi)
target_sources(gpu_frame_bridge_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/src/browser/gpu_frame_bridge.cpp)
add_offscreen_core_test(osr_render_log_tests osr_render_log_tests.cpp)
add_offscreen_core_test(render_stats_log_tests render_stats_log_tests.cpp)

# Aggregate target referenced by scripts/build.ps1 and validation workflows.
add_custom_target(offscreen_core_tests
  DEPENDS
    app_config_tests
    browser_geometry_tests
    browser_close_state_tests
    browser_window_title_tests
    browser_paint_geometry_tests
    browser_input_mapping_tests
    browser_ime_core_tests
    render_stats_tests
    gpu_frame_state_tests
    gpu_frame_bridge_tests
    osr_render_log_tests
    render_stats_log_tests
)
