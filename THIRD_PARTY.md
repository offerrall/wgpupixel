# Third-party notices

wgpupixel is MIT licensed. External dependencies retain their own licenses.

| Dependency | Authors | License / notices |
| --- | --- | --- |
| [wgpu-native](https://github.com/gfx-rs/wgpu-native/tree/6aed50955d934ac36049ba8d002034841633ae02) | The gfx-rs developers | MIT or Apache-2.0 |
| [webgpu-headers](https://github.com/webgpu-native/webgpu-headers/tree/673658bc2bd70ec39fc55ebe6bb0173cf6d0a603) | WebGPU-Native developers | BSD-3-Clause |
| [Emdawnwebgpu](https://dawn.googlesource.com/dawn/+/31e25af254ab572c77054edec4946d2244e184dd/src/emdawnwebgpu/) | Emscripten and Dawn contributors | MIT / BSD-3-Clause by component |
| [FFmpeg](https://ffmpeg.org/) | FFmpeg developers | LGPL-2.1-or-later for the selected image-only configuration |
| [LittleCMS](https://www.littlecms.com/) | Marti Maria and contributors | MIT |
| [Pango / PangoCairo / PangoFT2](https://www.pango.org/) | Pango contributors | LGPL-2.1-or-later |
| [Cairo](https://www.cairographics.org/) | Cairo contributors | LGPL-2.1 or MPL-1.1 |
| [Fontconfig](https://www.freedesktop.org/wiki/Software/fontconfig/) | Fontconfig contributors | Permissive Fontconfig license |
| [FreeType](https://freetype.org/license.html) | The FreeType Project | FreeType License or GPLv2 |
| [HarfBuzz](https://github.com/harfbuzz/harfbuzz) | HarfBuzz contributors | Old MIT |

Typography has transitive dependencies including GLib; their closure and notices
depend on the supplied SDK. Native typography SDK libraries are not bundled by
wgpupixel. Browser consumers link the bridge from their Emscripten SDK.

The shared image-only FFmpeg build excludes GPL components and retains the source
URL, build profile and original licenses in its SDK. zlib and liblzma provide
compression. Configuration is in `cmake/FFmpeg.cmake`. Dependency distributors
must retain the corresponding notices and satisfy the selected licenses; see
[FFmpeg distribution information](https://ffmpeg.org/legal.html).

The installed SDK ships the wgpu-native runtime built with
`cmake/wgpu-native-errors.patch`, which adds structured error reporting, together
with its headers and the wgpu-native and webgpu-headers licenses. The upstream
context in the patch is Copyright (c) 2021 The gfx-rs developers, under
wgpu-native's MIT license, which the SDK installs with the runtime.
Historical pyimagecuda attribution remains in [LICENSE.pyimagecuda](LICENSE.pyimagecuda).

Font fixtures are not installed as library runtime fonts. Their provenance and
licenses remain in [tests/data/fonts/NOTICE.md](tests/data/fonts/NOTICE.md):
Noto and Amstelvar use SIL OFL 1.1; the synthetic Google color-font fixture uses
Apache-2.0.
