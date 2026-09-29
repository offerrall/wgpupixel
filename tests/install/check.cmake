# Run against already-built dependency SDKs, without downloads or backend rebuilds.
# cmake -DSOURCE=... -DWORK=... -DWGPU_SDK=... -DFFMPEG_SDK=... -P check.cmake
cmake_minimum_required(VERSION 3.25)
foreach(_required SOURCE WORK WGPU_SDK FFMPEG_SDK)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "Missing -D${_required}=...")
    endif()
endforeach()
function(run)
    execute_process(COMMAND ${ARGV} COMMAND_ERROR_IS_FATAL ANY)
endfunction()
function(consumer name sdk text)
    run("${CMAKE_COMMAND}" -S "${SOURCE}/tests/install" -B "${WORK}/${name}-consumer" -G Ninja
        "-Dwgpupixel_DIR=${sdk}" "-DWGPUPIXEL_CONSUMER_TEXT=${text}")
    run("${CMAKE_COMMAND}" --build "${WORK}/${name}-consumer" --parallel 4)
    run("${CMAKE_CTEST_COMMAND}" --test-dir "${WORK}/${name}-consumer" --output-on-failure)
endfunction()

foreach(_name static-core shared-core shared-text)
    set(_shared OFF)
    set(_text OFF)
    if(_name MATCHES "^shared")
        set(_shared ON)
    endif()
    if(_name MATCHES "text$")
        set(_text ON)
    endif()
    set(_build "${WORK}/${_name}")
    set(_sdk "${WORK}/${_name}-sdk")
    run("${CMAKE_COMMAND}" -S "${SOURCE}" -B "${_build}" -G Ninja
        -DCMAKE_BUILD_TYPE=Release "-DBUILD_SHARED_LIBS=${_shared}"
        "-DWGPUPIXEL_BUILD_TEXT=${_text}" -DWGPUPIXEL_BUILD_TESTS=OFF
        -DWGPUPIXEL_FETCH_DEPENDENCIES=OFF "-DWGPUPIXEL_WGPU_ROOT=${WGPU_SDK}"
        "-DWGPUPIXEL_FFMPEG_ROOT=${FFMPEG_SDK}" -DCMAKE_INSTALL_LIBDIR=lib
        -DCMAKE_INSTALL_INCLUDEDIR=include -DCMAKE_INSTALL_BINDIR=bin)
    run("${CMAKE_COMMAND}" --build "${_build}" --parallel 4)
    run("${CMAKE_COMMAND}" --install "${_build}" --prefix "${_sdk}")
    # Exercise relocation as well as the library/component combinations.
    set(_moved "${WORK}/${_name}-relocated")
    if(EXISTS "${_moved}")
        file(REMOVE_RECURSE "${_moved}")
    endif()
    file(RENAME "${_sdk}" "${_moved}")
    consumer("${_name}" "${_moved}/lib/cmake/wgpupixel" "${_text}")
endforeach()

# Reuse the static core compilation; only installation paths change. Absolute
# directories outside the prefix must remain absolute in imported targets. Keep
# them outside SOURCE too: CMake rejects exported include paths into the source tree.
if(NOT DEFINED ABSOLUTE_ROOT)
    set(_temporary "$ENV{TMPDIR}")
    if(NOT _temporary)
        set(_temporary "$ENV{TEMP}")
    endif()
    if(NOT _temporary)
        set(_temporary /tmp)
    endif()
    string(SHA256 _work_hash "${WORK}")
    string(SUBSTRING "${_work_hash}" 0 16 _work_hash)
    set(ABSOLUTE_ROOT "${_temporary}/wgpupixel-install-${_work_hash}")
endif()
set(_absolute "${ABSOLUTE_ROOT}")
run("${CMAKE_COMMAND}" -S "${SOURCE}" -B "${WORK}/static-core"
    "-DCMAKE_INSTALL_INCLUDEDIR=${_absolute}/headers"
    "-DCMAKE_INSTALL_LIBDIR=${_absolute}/libraries"
    "-DCMAKE_INSTALL_BINDIR=${_absolute}/executables")
run("${CMAKE_COMMAND}" --build "${WORK}/static-core" --parallel 4)
run("${CMAKE_COMMAND}" --install "${WORK}/static-core" --prefix "${_absolute}/prefix")
consumer(absolute "${_absolute}/libraries/cmake/wgpupixel" OFF)
