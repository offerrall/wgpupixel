add_executable(wgpupixel_benchmark main.cpp)
target_link_libraries(wgpupixel_benchmark PRIVATE wgpupixel::wgpupixel)
target_compile_definitions(wgpupixel_benchmark PRIVATE
    WGPUPIXEL_BUILD_CONFIGURATION="$<CONFIG>"
    WGPUPIXEL_BUILD_COMPILER="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
    WGPUPIXEL_BUILD_WGPU_REVISION="${_wgpu_commit}")

foreach(probe stroke_continuation viewport_dirty)
    add_executable(wgpupixel_benchmark_${probe} ${probe}.cpp)
    target_link_libraries(wgpupixel_benchmark_${probe} PRIVATE wgpupixel::wgpupixel)
endforeach()
target_compile_definitions(wgpupixel_benchmark_viewport_dirty PRIVATE WGPUPIXEL_VIEWPORT_DIRTY)
