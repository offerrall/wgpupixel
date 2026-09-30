# Generous enough for software Vulkan (lavapipe in CI), which runs tens of times slower
# than a GPU; a hang still fails. filter_large gets more below.
set(_timeout 600)

foreach(suite operations contracts lifetime transfers masks masked_filters regions revisions)
    add_executable(wgpupixel_test_${suite} ${suite}.cpp)
    target_link_libraries(wgpupixel_test_${suite} PRIVATE wgpupixel::wgpupixel)
    add_test(NAME wgpupixel.${suite} COMMAND wgpupixel_test_${suite})
    set_tests_properties(wgpupixel.${suite} PROPERTIES TIMEOUT ${_timeout})
endforeach()

add_executable(wgpupixel_test_presentation presentation.cpp)
target_link_libraries(wgpupixel_test_presentation PRIVATE wgpupixel::wgpupixel wgpupixel::wgpu_native)
add_test(NAME wgpupixel.presentation COMMAND wgpupixel_test_presentation)
set_tests_properties(wgpupixel.presentation PROPERTIES TIMEOUT ${_timeout})

add_executable(wgpupixel_test_viewport viewport.cpp)
target_link_libraries(wgpupixel_test_viewport PRIVATE wgpupixel::wgpupixel wgpupixel::wgpu_native)
add_test(NAME wgpupixel.viewport COMMAND wgpupixel_test_viewport)
set_tests_properties(wgpupixel.viewport PROPERTIES TIMEOUT ${_timeout})

if(NOT EMSCRIPTEN)
    add_executable(wgpupixel_test_ffmpeg ffmpeg.cpp)
    target_link_libraries(wgpupixel_test_ffmpeg PRIVATE wgpupixel::wgpupixel ${_ffmpeg_targets})
    add_test(NAME wgpupixel.ffmpeg COMMAND wgpupixel_test_ffmpeg)
    add_executable(wgpupixel_test_io io.cpp)
    target_link_libraries(wgpupixel_test_io PRIVATE wgpupixel::wgpupixel)
    target_compile_definitions(wgpupixel_test_io PRIVATE
        WGPUPIXEL_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
    add_test(NAME wgpupixel.io COMMAND wgpupixel_test_io)
    set_tests_properties(wgpupixel.io PROPERTIES TIMEOUT ${_timeout})
endif()

if(WGPUPIXEL_BUILD_TEXT)
    foreach(suite text text_gpu)
        add_executable(wgpupixel_test_${suite} ${suite}.cpp)
        target_link_libraries(wgpupixel_test_${suite} PRIVATE wgpupixel::text)
        target_compile_definitions(wgpupixel_test_${suite} PRIVATE
            WGPUPIXEL_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
        add_test(NAME wgpupixel.${suite} COMMAND wgpupixel_test_${suite})
        set_tests_properties(wgpupixel.${suite} PROPERTIES TIMEOUT ${_timeout})
    endforeach()
endif()

# Observe native resource counts without exposing backend handles in the public API.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT BUILD_SHARED_LIBS)
    target_compile_definitions(wgpupixel_test_viewport PRIVATE WGPUPIXEL_TEST_FAILURE_INJECTION)
    target_link_options(wgpupixel_test_viewport PRIVATE
        "LINKER:--wrap=wgpuQueueSubmitForIndex" "LINKER:--wrap=wgpuQueueOnSubmittedWorkDone")
    add_executable(wgpupixel_test_recording recording.cpp)
    target_link_libraries(wgpupixel_test_recording PRIVATE wgpupixel::wgpupixel)
    target_include_directories(wgpupixel_test_recording PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_options(wgpupixel_test_recording PRIVATE "LINKER:--wrap=wgpuDeviceGetLimits"
        "LINKER:--wrap=wgpuDeviceCreateBuffer")
    add_test(NAME wgpupixel.recording COMMAND wgpupixel_test_recording)
    set_tests_properties(wgpupixel.recording PROPERTIES TIMEOUT ${_timeout})
    add_executable(wgpupixel_test_memory memory.cpp)
    target_link_libraries(wgpupixel_test_memory PRIVATE wgpupixel::wgpupixel wgpupixel::wgpu_native)
    target_link_options(wgpupixel_test_memory PRIVATE "LINKER:--wrap=wgpuCreateInstance"
        "LINKER:--wrap=wgpuDeviceCreateBuffer" "LINKER:--wrap=wgpuDeviceCreateCommandEncoder"
        "LINKER:--wrap=wgpuDeviceCreateComputePipeline" "LINKER:--wrap=wgpuQueueOnSubmittedWorkDone"
        "LINKER:--wrap=wgpuCommandEncoderCopyBufferToBuffer")
    add_test(NAME wgpupixel.memory COMMAND wgpupixel_test_memory)
    set_tests_properties(wgpupixel.memory PROPERTIES TIMEOUT ${_timeout})
    add_executable(wgpupixel_test_filter_storage filter_storage.cpp)
    target_link_libraries(wgpupixel_test_filter_storage PRIVATE wgpupixel::wgpupixel wgpupixel::wgpu_native)
    target_link_options(wgpupixel_test_filter_storage PRIVATE "LINKER:--wrap=wgpuCreateInstance"
        "LINKER:--wrap=wgpuDeviceCreateBuffer")
    add_test(NAME wgpupixel.filter_storage COMMAND wgpupixel_test_filter_storage)
    set_tests_properties(wgpupixel.filter_storage PROPERTIES TIMEOUT ${_timeout})
endif()

file(GLOB _algorithm_tests CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/algorithms/*.cpp")
foreach(_test IN LISTS _algorithm_tests)
    get_filename_component(_name "${_test}" NAME_WE)
    if(EMSCRIPTEN AND _name STREQUAL "io_precision")
        continue()
    endif()
    add_executable(wgpupixel_test_${_name} "${_test}")
    target_link_libraries(wgpupixel_test_${_name} PRIVATE wgpupixel::wgpupixel)
    add_test(NAME wgpupixel.${_name} COMMAND wgpupixel_test_${_name})
    set_tests_properties(wgpupixel.${_name} PROPERTIES TIMEOUT ${_timeout})
endforeach()
# The radius 100/500 exact medians take about 4 minutes on software Vulkan (lavapipe,
# 4 threads) against under a second on a GPU.
set_tests_properties(wgpupixel.filter_large PROPERTIES TIMEOUT 900)
if(NOT EMSCRIPTEN)
    target_compile_definitions(wgpupixel_test_io_precision PRIVATE
        WGPUPIXEL_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT BUILD_SHARED_LIBS)
        target_compile_definitions(wgpupixel_test_io_precision PRIVATE WGPUPIXEL_TEST_EXIF_FAILURE)
        target_link_libraries(wgpupixel_test_io_precision PRIVATE ${_ffmpeg_targets})
        target_link_options(wgpupixel_test_io_precision PRIVATE
            "LINKER:--wrap=av_exif_parse_buffer" "LINKER:--wrap=av_exif_free")
    endif()
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT BUILD_SHARED_LIBS)
    target_compile_definitions(wgpupixel_test_stroke_region PRIVATE WGPUPIXEL_TEST_STROKE_COST)
    target_link_libraries(wgpupixel_test_stroke_region PRIVATE wgpupixel::wgpu_native)
    target_link_options(wgpupixel_test_stroke_region PRIVATE
        "LINKER:--wrap=wgpuDeviceCreateComputePipeline"
        "LINKER:--wrap=wgpuComputePassEncoderSetPipeline"
        "LINKER:--wrap=wgpuComputePassEncoderDispatchWorkgroups"
        "LINKER:--wrap=wgpuCommandEncoderCopyBufferToBuffer")
endif()
# affine compares runtime and constant evaluation of the same IEEE operations bit for
# bit. GCC contracts a*b - c*d into FMA by default when the target has it (x86-64-v3,
# Ubuntu's amd64v3 toolchain), which constant evaluation never does.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(wgpupixel_test_affine PRIVATE -ffp-contract=off)
endif()

# Viewport algorithm tests read native presentation targets and count reduction work.
target_link_libraries(wgpupixel_test_viewport_dirty PRIVATE wgpupixel::wgpu_native)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT BUILD_SHARED_LIBS)
    target_compile_definitions(wgpupixel_test_viewport_dirty PRIVATE WGPUPIXEL_TEST_DISPATCH_COUNTS)
    target_link_options(wgpupixel_test_viewport_dirty PRIVATE
        "LINKER:--wrap=wgpuDeviceCreateBuffer"
        "LINKER:--wrap=wgpuComputePassEncoderDispatchWorkgroups"
        "LINKER:--wrap=wgpuQueueSubmitForIndex" "LINKER:--wrap=wgpuQueueOnSubmittedWorkDone")
endif()
