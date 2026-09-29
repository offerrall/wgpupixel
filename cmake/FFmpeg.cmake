# One image-only FFmpeg build for the C++ core.
set(_ffmpeg_version 9.0.1)
set(_ffmpeg_sha cf38e0e28c7e5605942c4a77755349b0145804a397af37eb1fb4c77cb237f635)
set(_ffmpeg_options --build-suffix=-wgpupixel --enable-shared --disable-static --disable-everything
    --disable-autodetect --disable-programs --disable-doc --disable-debug --disable-iconv
    --disable-network --disable-avdevice --disable-avfilter --disable-swresample
    --enable-zlib --enable-lzma
    --enable-decoder=mjpeg,png,webp,tiff,exr --enable-encoder=png
    --enable-demuxer=image_jpeg_pipe,image_png_pipe,image_webp_pipe,image_tiff_pipe,image_exr_pipe
    --enable-parser=mjpeg,png,webp --enable-protocol=file)
set(_ffmpeg_components avformat avcodec swscale avutil)
set(_ffmpeg_majors 63 63 10 61)
set(_ffmpeg_profile "${_ffmpeg_version}\n${_ffmpeg_sha}\n${_ffmpeg_options}\n${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}\n")
set(WGPUPIXEL_FFMPEG_ROOT "" CACHE PATH "SDK built with wgpupixel's pinned image-only FFmpeg profile")
if(WGPUPIXEL_FFMPEG_ROOT)
    get_filename_component(_ffmpeg_root "${WGPUPIXEL_FFMPEG_ROOT}" ABSOLUTE)
    if(NOT EXISTS "${_ffmpeg_root}/wgpupixel-ffmpeg.txt")
        message(FATAL_ERROR "FFmpeg SDK lacks wgpupixel-ffmpeg.txt; a system/full FFmpeg is not supported")
    endif()
    file(READ "${_ffmpeg_root}/wgpupixel-ffmpeg.txt" _actual_ffmpeg_profile)
    if(NOT _actual_ffmpeg_profile STREQUAL _ffmpeg_profile)
        message(FATAL_ERROR "FFmpeg SDK must match the pinned version, image-only profile and target architecture")
    endif()
    if(NOT EXISTS "${_ffmpeg_root}/include/libavcodec/avcodec.h")
        message(FATAL_ERROR "Incomplete image-only FFmpeg SDK: missing development headers")
    endif()
else()
    if(NOT WGPUPIXEL_FETCH_DEPENDENCIES)
        message(FATAL_ERROR "Provide WGPUPIXEL_FFMPEG_ROOT or enable WGPUPIXEL_FETCH_DEPENDENCIES")
    endif()
    enable_language(C)
    find_program(WGPUPIXEL_SH NAMES sh bash REQUIRED)
    find_program(WGPUPIXEL_MAKE NAMES gmake make REQUIRED)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i.86)$")
        find_program(WGPUPIXEL_NASM nasm REQUIRED)
        list(APPEND _ffmpeg_toolchain "--x86asmexe=${WGPUPIXEL_NASM}")
    endif()
    if(CMAKE_CROSSCOMPILING)
        message(FATAL_ERROR "Cross builds must supply a matching image-only WGPUPIXEL_FFMPEG_ROOT SDK")
    endif()
    if(MSVC)
        list(APPEND _ffmpeg_toolchain --toolchain=msvc)
    elseif(APPLE)
        list(APPEND _ffmpeg_toolchain --install-name-dir=@rpath)
    else()
        list(APPEND _ffmpeg_toolchain --enable-rpath)
    endif()
    include(FetchContent)
    FetchContent_Declare(wgpupixel_ffmpeg_source
        URL "https://ffmpeg.org/releases/ffmpeg-${_ffmpeg_version}.tar.xz"
        URL_HASH "SHA256=${_ffmpeg_sha}"
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE SOURCE_SUBDIR wgpupixel-no-build)
    FetchContent_MakeAvailable(wgpupixel_ffmpeg_source)
    set(_ffmpeg_root "${CMAKE_CURRENT_BINARY_DIR}/ffmpeg-sdk")
    set(_ffmpeg_build "${CMAKE_CURRENT_BINARY_DIR}/ffmpeg-build")
    file(MAKE_DIRECTORY "${_ffmpeg_build}")
    file(WRITE "${_ffmpeg_build}/profile.txt" "${_ffmpeg_profile}")
endif()
set(WGPUPIXEL_FFMPEG_SDK "${_ffmpeg_root}" CACHE INTERNAL "Resolved image-only FFmpeg SDK" FORCE)
file(MAKE_DIRECTORY "${_ffmpeg_root}/include")
set(_ffmpeg_files)
set(_ffmpeg_targets)
set(_ffmpeg_installed "# Image-only FFmpeg targets shipped with wgpupixel.\n")
foreach(_index RANGE 0 3)
    list(GET _ffmpeg_components ${_index} _name)
    list(GET _ffmpeg_majors ${_index} _major)
    if(WIN32)
        set(_runtime "bin/${_name}-wgpupixel-${_major}.dll")
        set(_library "lib/${_name}-wgpupixel.lib")
    elseif(APPLE)
        set(_runtime "lib/lib${_name}-wgpupixel.${_major}.dylib")
    else()
        set(_runtime "lib/lib${_name}-wgpupixel.so.${_major}")
    endif()
    if(WGPUPIXEL_FFMPEG_ROOT AND NOT EXISTS "${_ffmpeg_root}/${_runtime}")
        message(FATAL_ERROR "Incomplete image-only FFmpeg SDK: ${_ffmpeg_root}/${_runtime}")
    endif()
    list(APPEND _ffmpeg_files "${_ffmpeg_root}/${_runtime}")
    set(_target "wgpupixel::ffmpeg_${_name}")
    list(APPEND _ffmpeg_targets "${_target}")
    add_library(${_target} SHARED IMPORTED GLOBAL)
    set_target_properties(${_target} PROPERTIES IMPORTED_LOCATION "${_ffmpeg_root}/${_runtime}"
        INTERFACE_INCLUDE_DIRECTORIES "${_ffmpeg_root}/include")
    get_filename_component(_runtime_name "${_runtime}" NAME)
    if(WIN32)
        if(WGPUPIXEL_FFMPEG_ROOT AND NOT EXISTS "${_ffmpeg_root}/${_library}")
            message(FATAL_ERROR "Incomplete image-only FFmpeg SDK: ${_ffmpeg_root}/${_library}")
        endif()
        list(APPEND _ffmpeg_files "${_ffmpeg_root}/${_library}")
        set_property(TARGET ${_target} PROPERTY IMPORTED_IMPLIB "${_ffmpeg_root}/${_library}")
        set(_install_dir "${CMAKE_INSTALL_BINDIR}")
        set(_package_install_dir "@PACKAGE_CMAKE_INSTALL_BINDIR@")
    else()
        set(_install_dir "${CMAKE_INSTALL_LIBDIR}")
        set(_package_install_dir "@PACKAGE_CMAKE_INSTALL_LIBDIR@")
    endif()
    string(APPEND _ffmpeg_installed "if(NOT TARGET ${_target})\n  add_library(${_target} SHARED IMPORTED)\n  set_target_properties(${_target} PROPERTIES IMPORTED_LOCATION \"${_package_install_dir}/${_runtime_name}\")\n")
    if(WIN32)
        get_filename_component(_import_name "${_library}" NAME)
        string(APPEND _ffmpeg_installed "  set_property(TARGET ${_target} PROPERTY IMPORTED_IMPLIB \"@PACKAGE_CMAKE_INSTALL_LIBDIR@/${_import_name}\")\n")
        install(FILES "${_ffmpeg_root}/${_library}" DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    endif()
    string(APPEND _ffmpeg_installed "endif()\n")
    # Copy the versioned runtime bytes, not development symlinks or unrelated SDK files.
    install(FILES "${_ffmpeg_root}/${_runtime}" DESTINATION "${_install_dir}")
endforeach()

if(NOT WGPUPIXEL_FFMPEG_ROOT)
    add_custom_command(OUTPUT "${_ffmpeg_root}/wgpupixel-ffmpeg.txt"
        BYPRODUCTS ${_ffmpeg_files}
        COMMAND "${WGPUPIXEL_SH}" "${wgpupixel_ffmpeg_source_SOURCE_DIR}/configure"
            "--prefix=${_ffmpeg_root}" "--cc=${CMAKE_C_COMPILER}" ${_ffmpeg_options} ${_ffmpeg_toolchain}
        COMMAND "${WGPUPIXEL_MAKE}" -j2
        COMMAND "${WGPUPIXEL_MAKE}" install
        COMMAND "${CMAKE_COMMAND}" "-DSDK=${_ffmpeg_root}" "-DSOURCE=${wgpupixel_ffmpeg_source_SOURCE_DIR}"
            "-DSOURCE_URL=https://ffmpeg.org/releases/ffmpeg-${_ffmpeg_version}.tar.xz"
            "-DPROFILE=${_ffmpeg_build}/profile.txt" -P "${CMAKE_CURRENT_LIST_DIR}/FinalizeFFmpeg.cmake"
        DEPENDS "${CMAKE_CURRENT_LIST_FILE}" "${CMAKE_CURRENT_LIST_DIR}/FinalizeFFmpeg.cmake"
        WORKING_DIRECTORY "${_ffmpeg_build}" USES_TERMINAL VERBATIM)
    add_custom_target(wgpupixel_ffmpeg_build DEPENDS "${_ffmpeg_root}/wgpupixel-ffmpeg.txt")
    foreach(_target IN LISTS _ffmpeg_targets)
        add_dependencies(${_target} wgpupixel_ffmpeg_build)
    endforeach()
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/wgpupixelFFmpegTargets.cmake.in" "${_ffmpeg_installed}")
configure_package_config_file("${CMAKE_CURRENT_BINARY_DIR}/wgpupixelFFmpegTargets.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/wgpupixelFFmpegTargets.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/wgpupixel"
    PATH_VARS CMAKE_INSTALL_LIBDIR CMAKE_INSTALL_BINDIR)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/wgpupixelFFmpegTargets.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/wgpupixel")
install(DIRECTORY "${_ffmpeg_root}/licenses/" DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/wgpupixel/ffmpeg")
install(FILES "${_ffmpeg_root}/wgpupixel-ffmpeg.txt"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/wgpupixel/ffmpeg")
