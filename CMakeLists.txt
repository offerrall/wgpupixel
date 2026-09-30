cmake_minimum_required(VERSION 3.25)
project(wgpupixel VERSION 1.1.0
    DESCRIPTION "GPU image engine for editors: layers, selections, brushes, adjustments, filters and transforms, with WebGPU"
    HOMEPAGE_URL "https://offerrall.github.io/wgpupixel/"
    LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
find_package(Threads REQUIRED)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(WGPUPIXEL_FETCH_DEPENDENCIES "Download pinned native WebGPU and image-only FFmpeg sources" ON)
set(WGPUPIXEL_WGPU_ROOT "" CACHE PATH "Local patched wgpu-native v29.0.1.1 SDK (offline)")
if(EMSCRIPTEN)
    add_library(wgpupixel_webgpu INTERFACE)
    add_library(wgpupixel::wgpu_native ALIAS wgpupixel_webgpu)
    set(_webgpu_port "--use-port=emdawnwebgpu:cpp_bindings=false")
    target_compile_options(wgpupixel_webgpu INTERFACE "${_webgpu_port}" -fexceptions)
    target_link_options(wgpupixel_webgpu INTERFACE "${_webgpu_port}" -fexceptions
        "-sDISABLE_EXCEPTION_CATCHING=0" "-sASYNCIFY=1" "-sASYNCIFY_STACK_SIZE=65536")
else()
    include(cmake/WgpuNative.cmake)
endif()
include(cmake/EmbedShaders.cmake)

file(GLOB _operation_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/operations/*.cpp")
add_library(wgpupixel
    ${_operation_sources}
    src/operation_sizes.cpp src/context.cpp src/platform.cpp src/commands.cpp src/utils/commands.cpp src/resources.cpp src/error.cpp src/presentation.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/shaders.cpp")
add_library(wgpupixel::wgpupixel ALIAS wgpupixel)
target_compile_features(wgpupixel PUBLIC cxx_std_23)
target_include_directories(wgpupixel
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(wgpupixel PUBLIC wgpupixel::wgpu_native PRIVATE Threads::Threads)
target_compile_definitions(wgpupixel PRIVATE WGPUPIXEL_BUILDING_LIBRARY)
set_target_properties(wgpupixel PROPERTIES POSITION_INDEPENDENT_CODE ON
    CXX_EXTENSIONS OFF CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON
    VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR})
if(BUILD_SHARED_LIBS)
    target_compile_definitions(wgpupixel PUBLIC WGPUPIXEL_SHARED)
endif()
if(NOT EMSCRIPTEN)
    find_package(PkgConfig REQUIRED)
    include(cmake/FFmpeg.cmake)
    pkg_check_modules(LCMS2 REQUIRED IMPORTED_TARGET lcms2>=2.16)
    target_sources(wgpupixel PRIVATE src/io.cpp)
    target_link_libraries(wgpupixel PRIVATE ${_ffmpeg_targets} PkgConfig::LCMS2)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT BUILD_SHARED_LIBS)
        # Static consumers that never call file I/O need not load its shared libraries.
        target_link_options(wgpupixel INTERFACE "LINKER:--as-needed")
    endif()
endif()
option(WGPUPIXEL_BUILD_TEXT "Build native CPU typography with Pango and Cairo" OFF)
if(WGPUPIXEL_BUILD_TEXT)
    if(EMSCRIPTEN)
        message(FATAL_ERROR "WGPUPIXEL_BUILD_TEXT requires native Pango/Fontconfig/Cairo; disable it for browser builds")
    endif()
    pkg_check_modules(TEXT REQUIRED IMPORTED_TARGET
        pangocairo>=1.56 pangoft2>=1.56 cairo>=1.18.2 fontconfig>=2.15 freetype2 harfbuzz>=2.6)
    add_library(wgpupixel_text src/text.cpp)
    add_library(wgpupixel::text ALIAS wgpupixel_text)
    target_link_libraries(wgpupixel_text PUBLIC wgpupixel::wgpupixel PRIVATE PkgConfig::TEXT)
    target_compile_definitions(wgpupixel_text PRIVATE WGPUPIXEL_BUILDING_TEXT)
    target_compile_options(wgpupixel_text PUBLIC "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
    set_target_properties(wgpupixel_text PROPERTIES EXPORT_NAME text
        POSITION_INDEPENDENT_CODE ON CXX_EXTENSIONS OFF CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR})
    if(BUILD_SHARED_LIBS)
        target_compile_definitions(wgpupixel_text PUBLIC WGPUPIXEL_TEXT_SHARED)
    endif()
endif()
option(WGPUPIXEL_ENABLE_UBSAN "Instrument native C++ with undefined-behavior checks" OFF)
if(WGPUPIXEL_ENABLE_UBSAN)
    if(EMSCRIPTEN OR NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "WGPUPIXEL_ENABLE_UBSAN requires a native GCC or Clang build")
    endif()
    foreach(_target wgpupixel wgpupixel_text)
        if(TARGET ${_target})
            # PUBLIC also instruments inline public helpers in consumer/test TUs.
            target_compile_options(${_target} PUBLIC
                -fsanitize=undefined,float-cast-overflow -fno-sanitize-recover=all)
            target_link_options(${_target} PUBLIC -fsanitize=undefined,float-cast-overflow)
        endif()
    endforeach()
endif()
option(WGPUPIXEL_BUILD_TESTS "Build GPU correctness and lifetime tests" OFF)
if(WGPUPIXEL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
option(WGPUPIXEL_BUILD_BENCHMARKS "Build the native benchmark executable" OFF)
if(WGPUPIXEL_BUILD_BENCHMARKS AND NOT EMSCRIPTEN)
    add_subdirectory(benchmarks/native)
endif()
if(EMSCRIPTEN)
    # Emscripten consumers currently use add_subdirectory; no native SDK package.
    return()
elseif(APPLE)
    set_property(TARGET wgpupixel PROPERTY INSTALL_RPATH "@loader_path")
    if(WGPUPIXEL_BUILD_TEXT)
        set_property(TARGET wgpupixel_text PROPERTY INSTALL_RPATH "@loader_path")
    endif()
elseif(UNIX)
    set_property(TARGET wgpupixel PROPERTY INSTALL_RPATH "$ORIGIN")
    if(WGPUPIXEL_BUILD_TEXT)
        set_property(TARGET wgpupixel_text PROPERTY INSTALL_RPATH "$ORIGIN")
    endif()
endif()

install(TARGETS wgpupixel EXPORT wgpupixelTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES include/wgpupixel.h include/wgpupixel_io.h include/wgpupixel_webgpu.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
if(WGPUPIXEL_BUILD_TEXT)
    install(TARGETS wgpupixel_text EXPORT wgpupixelTextTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES include/wgpupixel_text.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(EXPORT wgpupixelTextTargets NAMESPACE wgpupixel::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/wgpupixel")
endif()
install(FILES LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/wgpupixel)

set(_package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/wgpupixel")
configure_package_config_file(cmake/wgpupixelConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/wgpupixelConfig.cmake"
    INSTALL_DESTINATION "${_package_dir}"
    PATH_VARS CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_LIBDIR CMAKE_INSTALL_BINDIR)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/wgpupixelConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(EXPORT wgpupixelTargets NAMESPACE wgpupixel:: DESTINATION "${_package_dir}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/wgpupixelConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/wgpupixelConfigVersion.cmake" DESTINATION "${_package_dir}")
