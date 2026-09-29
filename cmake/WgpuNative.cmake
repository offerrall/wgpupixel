set(_wgpu_tag "v29.0.1.1")
set(_wgpu_commit "6aed50955d934ac36049ba8d002034841633ae02")
set(_headers_commit "673658bc2bd70ec39fc55ebe6bb0173cf6d0a603")
# wgpu-native-errors.patch adds structured error reporting to wgpu-native. The code it
# modifies is Copyright (c) 2021 The gfx-rs developers, under wgpu-native's MIT license,
# which the installed SDK carries with the runtime.
set(_patch "${CMAKE_CURRENT_LIST_DIR}/wgpu-native-errors.patch")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_patch}")
file(SHA256 "${_patch}" _patch_hash)
set(_marker "${_wgpu_commit}\n${_patch_hash}\n")
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _arch)
if(CMAKE_GENERATOR_PLATFORM)
    string(TOLOWER "${CMAKE_GENERATOR_PLATFORM}" _arch)
elseif(APPLE AND CMAKE_OSX_ARCHITECTURES)
    set(_arch "${CMAKE_OSX_ARCHITECTURES}")
endif()
if(_arch MATCHES "^(x86_64|amd64|x64)$")
    set(_arch x86_64)
elseif(_arch MATCHES "^(aarch64|arm64)$")
    set(_arch aarch64)
else()
    message(FATAL_ERROR "wgpupixel supports native x86_64 or arm64, one architecture per build; got '${_arch}'")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_rust_target "${_arch}-unknown-linux-gnu")
    set(_wgpu_runtime_name libwgpu_native.so)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(_rust_target "${_arch}-apple-darwin")
    set(_wgpu_runtime_name libwgpu_native.dylib)
elseif(WIN32 AND MSVC)
    set(_rust_target "${_arch}-pc-windows-msvc")
    set(_wgpu_runtime_name wgpu_native.dll)
else()
    message(FATAL_ERROR "wgpupixel supports Linux, macOS and Windows with an MSVC-compatible compiler")
endif()

if(WGPUPIXEL_WGPU_ROOT)
    get_filename_component(_wgpu_root "${WGPUPIXEL_WGPU_ROOT}" ABSOLUTE)
    foreach(_file include/webgpu/webgpu.h include/webgpu/wgpu.h
            lib/${_wgpu_runtime_name} wgpupixel-error-contract-v1.txt
            LICENSE.APACHE LICENSE.MIT LICENSE.WEBGPU_HEADERS)
        if(NOT EXISTS "${_wgpu_root}/${_file}")
            message(FATAL_ERROR "Incomplete patched wgpu-native SDK: missing ${_file} in ${_wgpu_root}")
        endif()
    endforeach()
    file(READ "${_wgpu_root}/wgpupixel-error-contract-v1.txt" _actual_marker)
    if(NOT _actual_marker STREQUAL _marker)
        message(FATAL_ERROR "Local wgpu-native SDK must match the pinned commit and error-contract patch")
    endif()
else()
    if(NOT WGPUPIXEL_FETCH_DEPENDENCIES)
        message(FATAL_ERROR "Provide WGPUPIXEL_WGPU_ROOT or enable WGPUPIXEL_FETCH_DEPENDENCIES")
    endif()
    include(FetchContent)
    FetchContent_Declare(wgpupixel_wgpu_source
        URL "https://codeload.github.com/gfx-rs/wgpu-native/tar.gz/${_wgpu_commit}"
        URL_HASH SHA256=bff001ca2f891d13dd9b5bd1fd6efc2003c54cfed9cdb21d3f84fc16d549e335
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE SOURCE_SUBDIR wgpupixel-no-build)
    FetchContent_Declare(wgpupixel_webgpu_headers
        URL "https://codeload.github.com/webgpu-native/webgpu-headers/tar.gz/${_headers_commit}"
        URL_HASH SHA256=6754c6c2465c79a954f7c04c7890c9f00b7ecce8addbe01b5056e860ba3933e6
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE SOURCE_SUBDIR wgpupixel-no-build)
    FetchContent_MakeAvailable(wgpupixel_wgpu_source wgpupixel_webgpu_headers)
    set(_source "${wgpupixel_wgpu_source_SOURCE_DIR}")
    set(_headers "${wgpupixel_webgpu_headers_SOURCE_DIR}")
    file(COPY "${_headers}/webgpu.h" DESTINATION "${_source}/ffi/webgpu-headers")

    set(_applied_hash "")
    if(EXISTS "${_source}/.wgpupixel-patch-sha256")
        file(READ "${_source}/.wgpupixel-patch-sha256" _applied_hash)
    endif()
    if(NOT _applied_hash STREQUAL _patch_hash)
        find_package(Git REQUIRED)
        if(NOT EXISTS "${_source}/src/lib.rs.wgpupixel-original")
            configure_file("${_source}/src/lib.rs" "${_source}/src/lib.rs.wgpupixel-original" COPYONLY)
        endif()
        configure_file("${_source}/src/lib.rs.wgpupixel-original" "${_source}/src/lib.rs" COPYONLY)
        execute_process(COMMAND "${GIT_EXECUTABLE}" apply "${_patch}"
            WORKING_DIRECTORY "${_source}" RESULT_VARIABLE _patch_result ERROR_VARIABLE _patch_error)
        if(NOT _patch_result EQUAL 0)
            message(FATAL_ERROR "Could not apply wgpu-native error-contract patch: ${_patch_error}")
        endif()
        file(READ "${_source}/src/lib.rs" _patched_source)
        file(WRITE "${_source}/src/lib.rs"
            "// Modified by wgpupixel: apply wgpu-native-errors.patch for structured error reporting.\n${_patched_source}")
        file(WRITE "${_source}/.wgpupixel-patch-sha256" "${_patch_hash}")
    endif()

    set(WGPUPIXEL_RUST_TOOLCHAIN "1.93.0" CACHE STRING "Pinned Rust toolchain; empty explicitly selects system Cargo")
    if(WGPUPIXEL_RUST_TOOLCHAIN)
        find_program(WGPUPIXEL_RUSTUP rustup REQUIRED)
        set(_cargo "${WGPUPIXEL_RUSTUP}" run "${WGPUPIXEL_RUST_TOOLCHAIN}" cargo)
        execute_process(COMMAND ${_cargo} --version RESULT_VARIABLE _rust_result
            OUTPUT_QUIET ERROR_QUIET)
        if(NOT _rust_result EQUAL 0)
            message(FATAL_ERROR "Install the pinned toolchain: rustup toolchain install ${WGPUPIXEL_RUST_TOOLCHAIN}; rustup target add --toolchain ${WGPUPIXEL_RUST_TOOLCHAIN} ${_rust_target}")
        endif()
    else()
        find_program(WGPUPIXEL_CARGO cargo REQUIRED)
        set(_cargo "${WGPUPIXEL_CARGO}")
        message(STATUS "wgpu-native uses explicitly selected system Cargo; Rust toolchain is not pinned")
    endif()

    set(_wgpu_root "${CMAKE_CURRENT_BINARY_DIR}/wgpu-sdk")
    file(MAKE_DIRECTORY "${_wgpu_root}/include/webgpu" "${_wgpu_root}/lib")
    configure_file("${_source}/ffi/wgpu.h" "${_wgpu_root}/include/webgpu/wgpu.h" COPYONLY)
    configure_file("${_headers}/webgpu.h" "${_wgpu_root}/include/webgpu/webgpu.h" COPYONLY)
    configure_file("${_headers}/LICENSE" "${_wgpu_root}/LICENSE.WEBGPU_HEADERS" COPYONLY)
    foreach(_license LICENSE.APACHE LICENSE.MIT)
        configure_file("${_source}/${_license}" "${_wgpu_root}/${_license}" COPYONLY)
    endforeach()
    set(_marker_input "${CMAKE_CURRENT_BINARY_DIR}/wgpu-error-contract-v1.txt")
    file(WRITE "${_marker_input}" "${_marker}")
    set(_cargo_out "${CMAKE_CURRENT_BINARY_DIR}/wgpu-cargo")
    set(_cargo_artifacts "${_cargo_out}/${_rust_target}/release")
    set(_extra_build_commands "")
    if(WIN32)
        list(APPEND _extra_build_commands COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_cargo_artifacts}/wgpu_native.dll.lib" "${_wgpu_root}/lib/wgpu_native.dll.lib")
        set(_extra_byproducts "${_wgpu_root}/lib/wgpu_native.dll.lib")
    elseif(APPLE)
        # Cargo's cdylib install name otherwise embeds its build location.
        find_program(_install_name_tool install_name_tool REQUIRED)
        list(APPEND _extra_build_commands COMMAND "${_install_name_tool}"
            -id "@rpath/${_wgpu_runtime_name}" "${_wgpu_root}/lib/${_wgpu_runtime_name}")
    endif()
    add_custom_command(OUTPUT "${_wgpu_root}/lib/${_wgpu_runtime_name}"
        BYPRODUCTS ${_extra_byproducts} "${_wgpu_root}/wgpupixel-error-contract-v1.txt"
        COMMAND ${_cargo} build --locked --release --target "${_rust_target}"
            --target-dir "${_cargo_out}" --manifest-path "${_source}/Cargo.toml"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_cargo_artifacts}/${_wgpu_runtime_name}" "${_wgpu_root}/lib/${_wgpu_runtime_name}"
        ${_extra_build_commands}
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${_marker_input}" "${_wgpu_root}/wgpupixel-error-contract-v1.txt"
        DEPENDS "${_patch}" "${_source}/Cargo.lock" "${_source}/src/lib.rs"
            "${_source}/ffi/wgpu.h" "${_headers}/webgpu.h"
        WORKING_DIRECTORY "${_source}" USES_TERMINAL VERBATIM)
    add_custom_target(wgpupixel_wgpu_build DEPENDS "${_wgpu_root}/lib/${_wgpu_runtime_name}")
endif()

set(_wgpu_runtime "${_wgpu_root}/lib/${_wgpu_runtime_name}")
add_library(wgpupixel::wgpu_native SHARED IMPORTED GLOBAL)
set_target_properties(wgpupixel::wgpu_native PROPERTIES
    IMPORTED_LOCATION "${_wgpu_runtime}" INTERFACE_COMPILE_DEFINITIONS WGPU_SHARED_LIBRARY
    INTERFACE_INCLUDE_DIRECTORIES "${_wgpu_root}/include")
if(TARGET wgpupixel_wgpu_build)
    add_dependencies(wgpupixel::wgpu_native wgpupixel_wgpu_build)
endif()
if(WIN32)
    set(_wgpu_implib "${_wgpu_root}/lib/wgpu_native.dll.lib")
    if(WGPUPIXEL_WGPU_ROOT AND NOT EXISTS "${_wgpu_implib}")
        message(FATAL_ERROR "Missing wgpu-native import library: ${_wgpu_implib}")
    endif()
    set_property(TARGET wgpupixel::wgpu_native PROPERTY IMPORTED_IMPLIB "${_wgpu_implib}")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_property(TARGET wgpupixel::wgpu_native PROPERTY IMPORTED_NO_SONAME TRUE)
endif()

# Installed SDKs carry the pinned backend, its headers and notices; consumers need no build tree.
if(WIN32)
    install(FILES "${_wgpu_runtime}" DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(FILES "${_wgpu_implib}" DESTINATION "${CMAKE_INSTALL_LIBDIR}")
else()
    install(FILES "${_wgpu_runtime}" DESTINATION "${CMAKE_INSTALL_LIBDIR}")
endif()
install(DIRECTORY "${_wgpu_root}/include/webgpu" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES "${_wgpu_root}/LICENSE.APACHE" "${_wgpu_root}/LICENSE.MIT" "${_wgpu_root}/LICENSE.WEBGPU_HEADERS"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/wgpupixel/wgpu-native")
