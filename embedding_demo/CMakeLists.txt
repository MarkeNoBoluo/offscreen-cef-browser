function(add_embedding_demo target source)
  add_executable(${target} ${source})
  SET_EXECUTABLE_TARGET_PROPERTIES(${target})
  target_link_libraries(${target} PRIVATE offscreen_cef::widgets)
  target_compile_features(${target} PRIVATE cxx_std_17)
  target_compile_definitions(${target} PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Debug>>:_HAS_ITERATOR_DEBUGGING=0>
  )
  target_link_options(${target} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/MANIFEST:EMBED>
    $<$<CXX_COMPILER_ID:MSVC>:/MANIFESTINPUT:${CMAKE_SOURCE_DIR}/src/windows_compatibility.manifest>
  )
  offscreen_cef_deploy(${target})
  install(TARGETS ${target} RUNTIME DESTINATION .)
endfunction()

add_embedding_demo(embedding_demo_webview webview/main.cpp)
add_embedding_demo(embedding_demo_tabbed_browser tabbed_browser/main.cpp)
