# Build

wgpupixel needs CMake 3.25+ and a C++23 compiler; native builds also need
pkg-config and LittleCMS 2.16+. CMake downloads the pinned WebGPU backend
(wgpu-native, built with Rust 1.93.0 through rustup, and patched with Git) and an
image-only FFmpeg, whose build also needs a C compiler, Make, a POSIX shell, NASM
on x86, and the zlib and liblzma development files.

## In your application

Add the library to your CMake project and link its target:

```cmake
cmake_minimum_required(VERSION 3.25)
project(my_image_app LANGUAGES CXX)
add_subdirectory(wgpupixel)
add_executable(my_image_app main.cpp)
target_link_libraries(my_image_app PRIVATE wgpupixel::wgpupixel)
```

With an installed SDK, use `find_package(wgpupixel CONFIG REQUIRED)` instead of
`add_subdirectory`, adding `COMPONENTS text` to link `wgpupixel::text`.

### With FetchContent

CMake can also download the library for you:

```cmake
include(FetchContent)
FetchContent_Declare(wgpupixel GIT_REPOSITORY https://github.com/offerrall/wgpupixel GIT_TAG v1.0.2)
FetchContent_MakeAvailable(wgpupixel)
target_link_libraries(my_image_app PRIVATE wgpupixel::wgpupixel)
```

The first configure downloads and builds wgpu-native and FFmpeg, which needs the
tools above; pass `-DWGPUPIXEL_WGPU_ROOT=...` and `-DWGPUPIXEL_FFMPEG_ROOT=...`
to reuse SDKs you already built. On Linux and macOS your executable finds their
shared libraries through its build RPATH. On Windows, copy them next to it:

```cmake
add_custom_command(TARGET my_image_app POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy
    $<TARGET_RUNTIME_DLLS:my_image_app> $<TARGET_FILE_DIR:my_image_app> COMMAND_EXPAND_LISTS)
```

### Installing the SDK

To install an SDK for `find_package`:

```sh
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Release
cmake --build build/native --parallel 4
cmake --install build/native --prefix "$PWD/build/install"
```

The SDK includes the backend and FFmpeg runtimes with their notices; the system
libraries above are still needed. To reuse SDKs you already built, point
`WGPUPIXEL_WGPU_ROOT` and `WGPUPIXEL_FFMPEG_ROOT` at them.

### Prebuilt SDK

Every [release](https://github.com/offerrall/wgpupixel/releases) carries a Linux x86_64
SDK, `wgpupixel-X.Y.Z-linux-x86_64.tar.gz`, so neither Rust nor an FFmpeg build is needed:
the shared Release build with typography, its bundled runtimes, the CMake package and the
licenses. Extract it anywhere and point CMake at it:

```sh
sha256sum -c wgpupixel-1.0.2-linux-x86_64.tar.gz.sha256
tar -xzf wgpupixel-1.0.2-linux-x86_64.tar.gz
cmake -S . -B build -DCMAKE_PREFIX_PATH="$PWD/wgpupixel-1.0.2-linux-x86_64"
```

Then `find_package(wgpupixel CONFIG REQUIRED)`, adding `COMPONENTS text` for typography.
Executables in your build tree find its libraries without environment variables; an
installed application needs an RPATH to the SDK's `lib/`. It is built on Ubuntu 26.04, so
it needs a distribution at least as new, and configuring against it needs pkg-config and
the LittleCMS development files (and Pango's, for `text`).

## Options

| Option | Default | What it does |
| --- | --- | --- |
| `WGPUPIXEL_BUILD_TEXT` | `OFF` | Native typography, linked as `wgpupixel::text`. Needs Pango/PangoFT2 1.56+, Cairo 1.18.2+, Fontconfig 2.15+, FreeType and HarfBuzz 2.6+. |
| `WGPUPIXEL_FETCH_DEPENDENCIES` | `ON` | Download the pinned backend and FFmpeg sources. |
| `WGPUPIXEL_RUST_TOOLCHAIN` | `1.93.0` | The rustup toolchain that builds wgpu-native; empty uses the system Cargo. |
| `BUILD_SHARED_LIBS` | `OFF` | Build a shared library. |
| `WGPUPIXEL_BUILD_TESTS` | `OFF` | The GPU correctness and lifetime tests. |
| `WGPUPIXEL_BUILD_BENCHMARKS` | `OFF` | The native benchmark. |
| `WGPUPIXEL_ENABLE_UBSAN` | `OFF` | Undefined-behavior and float-to-integer checks, with GCC or Clang. |

## In the browser

The core and GPU presentation compile with Emscripten; native file I/O and
typography do not. Your application owns the canvas, the input and the image
decoding. Include `wgpupixel.h` rather than `wgpupixel_io.h`, serialize Asyncify
calls, and return to the browser event loop between frames.

```sh
emcmake cmake -S . -B build/browser -DCMAKE_BUILD_TYPE=Release
cmake --build build/browser --parallel 4
# Serve over HTTPS or localhost: WebGPU is required.
```

The browser build compiles but has not been run in a browser yet. Linux is
validated at runtime; Windows and macOS build but are not yet validated.

## Compatibility

Code written for a 1.x release keeps compiling with every later 1.x release.
Binary compatibility holds only between patch releases of one minor version
(1.0.x) built with a compatible toolchain, which is why the shared library is
named `libwgpupixel.so.1.0`: rebuild consumers when moving to a new minor
version. `find_package(wgpupixel 1.0)` accepts any later 1.x.

<details>
<summary>How it works inside</summary>

**Verifying a change.** Before every commit:

```sh
cmake -S . -B build/dev -G Ninja -DCMAKE_BUILD_TYPE=Release -DWGPUPIXEL_BUILD_TESTS=ON
cmake --build build/dev --parallel 4 && (cd build/dev && ctest --output-on-failure -j2)
node tools/examples.mjs --check
```

For the CPU checks, configure a separate build with
`-DWGPUPIXEL_ENABLE_UBSAN=ON -DWGPUPIXEL_BUILD_TESTS=ON`. `tests/install/check.cmake`
builds static and shared, core-only and text consumers against an installed SDK,
including relocated and absolute installation directories:

```sh
cmake -DSOURCE="$PWD" -DWORK="$PWD/build/install-check" \
  -DWGPU_SDK="$PWD/build/native/wgpu-sdk" \
  -DFFMPEG_SDK="$PWD/build/native/ffmpeg-sdk" -P tests/install/check.cmake
```

Its absolute-path case installs into a temporary directory outside the source
tree; `-DABSOLUTE_ROOT=/path` chooses another.

**Examples.** `examples/examples.js` is the only source of examples: each entry
has a title, a description, its code and the operations it `covers`. The
[Operations](operations.md) and [Programs](programs.md) pages are generated from
it, and every preview is a real GPU result. With an installed SDK (and
typography, for the text examples):

```sh
node tools/examples.mjs --emit build/examples
cmake -S build/examples -B build/examples/compiled -DCMAKE_PREFIX_PATH="$PWD/build/install"
cmake --build build/examples/compiled --parallel 4
node tools/examples.mjs --render build/examples/compiled/bin
node tools/examples.mjs --docs
```

`--check` fails when a GPU operation has no example, when a preview is stale
(any change to the headers, sources, shaders or build inputs makes all of them
stale; rebuild and reinstall the SDK before rendering), or when the generated
pages are out of date. Node is only a development tool.

**Repository map.**

| Path | What it holds |
| --- | --- |
| `include/` | The public API and its contracts: ranges, units and approximations are documented there and nowhere else |
| `src/operations/` | One file per operation or area: validation and record creation |
| `src/shaders/` | WGSL kernels and shared helpers, listed in `src/kernel_list.inc` |
| `src/context.cpp` | Device, submission, encoding, batches and the single allocation point |
| `tests/` | Contract, lifetime and memory tests; `tests/algorithms/` holds one CPU-referenced suite per area |
| `examples/`, `tools/` | The examples and the tool that checks, renders and documents them |
| `benchmarks/native/` | The optional native benchmark |

</details>
