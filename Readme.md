# Imagina

Imagina is a fast fractal renderer & explorer.

_English | [简体中文](Readme.zh-CN.md)_

---

**Language:** the documentation is available in [English](Readme.md) and
[简体中文](Readme.zh-CN.md).  Code comments and commit messages are in English;
pull request and issue templates accept both languages.

## Contents

- [Controls](#controls)
- [Zoom sequence export](#zoom-sequence-export)
- [Repository layout](#repository-layout)
- [Build](#build)
- [Front-ends](#front-ends)
- [Development](#development)
- [License](#license)

## Controls

* Left mouse button
    * Drag - Pan view
    * Double click - Find nearby feature and open Feature Finder dialog
* Mouse scroll wheel - Zoom
* A - Increase color density
* S - Decrease color density
* D - Increase iteration limit
* F - Decrease iteration limit
* E - Cycle color forward
* R - Cycle color backward

## Zoom sequence export

"File" -> "Export zoom sequence..." writes the key frames of a zoom sequence into the
current centre, one losslessly compressed raw map (*.rawz) per key frame plus an
export record (*.imz) listing the settings and the frames written so far.  The
coordinates the frames are computed at are kept in an *.im location file next to the
record ("File" -> "Save"), which the record names, so that they are stored once;
"File" -> "Resume zoom sequence..." continues an interrupted export from its record.
Frames are expanded again with `Imagina.exe --decode <frame.rawz> [<frame.raw>]`, or
with `tools/rawz.py`, which also reports statistics and writes coloured PNGs
(`python tools/rawz.py --help`).

The iteration limit of a key frame follows its scale: it is read from the orbit
of the centre (the period of the view), so that the wide key frames do not spend
the limit of the deepest one on the pixels which are inside the set.  The limits
an export would use are listed by `Imagina.exe --period <record.imz>`.

## Repository layout

```
src/                 the shared computation and rendering core
  platform/          the platform layer the core uses (GL access, memory, dialogs)
  win32/             Win32 front-end: window, menus, dialogs, resource file
  sdl/               SDL3 + Dear ImGui front-end
  encoder/           x86 encoder used by the custom formula JIT (optional)
tests/               tests (the raw map codec round trip test)
tools/               helper scripts (rawz.py)
```

## Build

### Supported platforms

| Tier | Platform | What is tested |
| --- | --- | --- |
| 1 | Windows x86-64 (MSVC via vcpkg, MinGW-w64) | CI: both front-ends build, codec test, the SDL front-end renders |
| 1 | Linux x86-64 (GCC, Clang) | CI: build, codec test, headless render under Xvfb with software OpenGL |
| 2 | macOS (Apple Silicon, Intel) | expected to build, no CI, best effort |
| 2 | FreeBSD and other BSDs | expected to build (POSIX paths), no CI, untested |

Only x86-64 is supported: the computation uses AVX2 intrinsics, so ARM and 32-bit
builds are out of scope for now.  Tier 2 means the build is kept working as far as
we can, but a bug there may wait for a contributor with the hardware.

If you build Imagina on a Tier 2 platform (or any platform that is not in the
table), please report it in the
[platform reports discussion](https://github.com/MarchBeta2087/Imagina/discussions/14) —
that is how we decide whether a platform can be promoted.

### Dependencies

| Dependency | Windows (vcpkg) | Linux | macOS |
| --- | --- | --- | --- |
| GMP (or MPIR) | `gmp` | `libgmp-dev` | `brew install gmp` |
| libpng | `libpng` | `libpng-dev` | `brew install libpng` |
| zlib | `zlib` | `zlib1g-dev` | `brew install zlib` (part of libpng) |
| glm | `glm` | `libglm-dev` | `brew install glm` |
| glext.h, wglext.h | `opengl-registry` | `libgl1-mesa-dev` | OpenGL framework |
| SDL3, Dear ImGui | `sdl3`, `imgui[sdl3-binding,opengl3-binding]` | fetched (or `libsdl3-dev`, `libimgui-dev`) | fetched |

MPIR is a fork of GMP and the sources accept either (`src/GmpCompat.h`).  MPIR is
unmaintained and not packaged anywhere, so the default configuration
(`MPIR_LIBRARY=auto`) uses GMP; both write the same `.im` files.  A build against
an existing MPIR installation still works (`-DMPIR_LIBRARY=mpir`).

Anything that can be fetched (glm, SDL3, Dear ImGui) is fetched when it is not
installed; the tags are pinned in `CMakeLists.txt`
(`IMAGINA_GLM_TAG`, `IMAGINA_SDL3_TAG`, `IMAGINA_IMGUI_TAG`).

### CMake variables

* `IMAGINA_DEPS_DIR` Directory with `include/` and `lib/` holding the bundled
  dependencies (the Windows build uses this; it sets `INCLUDE_PATH`/`LIBRARY_PATH`).
* `INCLUDE_PATH`, `LIBRARY_PATH` Where headers and libraries are looked up first.
* `IMAGINA_FRONTEND` `win32` or `sdl`.
* `MPIR_USE_DLL` Use the DLL version of MPIR.  Default: ON (static: `OFF`).
* `MPIR_LIBRARY`, `MPIRXX_LIBRARY` `auto` (default) prefers MPIR, falls back to GMP;
  can be set to a library name or an absolute path.
* `ZLIB_LIBRARY`, `PNG_LIBRARY` `auto` by default: `zlibwapi`/`libpng16` on Windows,
  the system `ZLIB::ZLIB`/`PNG::PNG` elsewhere.
* `BUILD_GUI` Build the application (default: ON).
* `BUILD_CODEC_TEST` Build the raw map codec round trip test.
* `IMAGINA_ENABLE_LTO` Build with link time optimisation (off by default, see below).

### Offline builds

Fetching needs network access the first time.  To build without it, point
`FetchContent` at existing checkouts (the standard CMake variables) or use
`IMAGINA_IMGUI_PATH` for Dear ImGui:

```
cmake -S . -B build   -DFETCHCONTENT_SOURCE_DIR_SDL3=/path/to/SDL   -DFETCHCONTENT_SOURCE_DIR_IMGUI=/path/to/imgui   -DFETCHCONTENT_SOURCE_DIR_GLM=/path/to/glm
```

### Windows

The recommended way is [vcpkg](https://vcpkg.io): `vcpkg.json` lists every
dependency (GMP, libpng, zlib, glm, `opengl-registry`, SDL3 and Dear ImGui with the
SDL3/OpenGL3 backends), so a manifest-mode build fetches and builds them.

```
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
setx VCPKG_ROOT C:\vcpkg

cmake --preset windows-msvc        # Win32 front-end (from a VS developer prompt)
cmake --build --preset windows-msvc --parallel

cmake --preset windows-msvc-sdl    # SDL3 + Dear ImGui front-end
cmake --build --preset windows-msvc-sdl --parallel
```

`build.ps1` uses the vcpkg toolchain automatically when `VCPKG_ROOT` is set, and
picks the compiler (-Compiler msvc|gcc|clang|clang-cl|auto):

```
powershell -ExecutionPolicy Bypass -File build.ps1                  # MSVC (detected)
powershell -ExecutionPolicy Bypass -File build.ps1 -Compiler gcc    # MinGW-w64 GCC
powershell -ExecutionPolicy Bypass -File build.ps1 -NoVcpkg         # old include\lib\ layout
powershell -ExecutionPolicy Bypass -File build.ps1 -Frontend sdl    # SDL3 + Dear ImGui front-end
```

Without vcpkg (or with `-NoVcpkg`) the dependencies are taken from `include\` and
`lib\` next to the script; `build.ps1`'s header lists what is needed and how the
MPIR/libpng/zlib sources are built.  MinGW-w64 can also use the MSYS2 packages
(`mingw-w64-ucrt-x86_64-gmp`, `-libpng`, `-zlib`, `-mesa`) — that is what the CI
job does.

### Linux, macOS, MSYS2

`build.sh` configures the same CMake project with GCC or Clang:

```
./build.sh --compiler clang --test
./build.sh --frontend sdl --compiler clang
./build.sh --clean -- -DENABLE_CUSTOM_FORMULA=ON
```

## Front-ends

There are two front-ends, selected with `-DIMAGINA_FRONTEND=win32|sdl` (the default is
`win32` on Windows and `sdl` everywhere else):

* `win32` - the original window, menus and dialogs (`src/win32/MainWindow.cpp`,
  `src/win32/Resource.rc`, `src/win32/ZoomExport.cpp`). Windows only.
* `sdl` - SDL3 + Dear ImGui (`src/sdl/SDLMain.cpp`). Builds on Windows, Linux and
  macOS and shares the whole computation and rendering core, including the file
  formats (`src/File.cpp`). Zoom sequence export (`ZoomExport.cpp`) is still Win32
  only.

SDL3 and Dear ImGui are looked up with `find_package` first and fetched from GitHub
otherwise; `-DIMAGINA_IMGUI_PATH=<dir>` points at an existing Dear ImGui checkout and
`-DMPIR_LIBRARY`/`-DMPIRXX_LIBRARY`/`-DZLIB_LIBRARY` at the library names of the
toolchain in use.

Notes:

* GNU/Clang releases are compiled with `-O2` and `-mprefer-vector-width=128`.
  GCC 16 miscompiles 32-byte struct copies into aligned 256-bit moves on a stack
  that is only 16-byte aligned, which crashed intermittently (issue #6); 128-bit
  auto-vectorisation avoids those instructions.  The `_mm256_*` intrinsics of the
  computation are unaffected.  Link time optimisation is off by default
  (`-DIMAGINA_ENABLE_LTO=ON` to try it).
* `IMAGINA_THREADS=<n>` limits the worker pool.
* `IMAGINA_CRASH_REPORT=1` installs an exception handler that prints the faulting
  module offset and a stack walk (Windows).
* The SDL front-end accepts `--frames <n>` (quit after n frames),
  `--screenshot <file.bmp>`, `--open <file>`, `--save <file>` (the extension picks
  the format), `--windows` (open every dialog) and `--stress` (zoom every frame),
  which makes the renderer, the file formats and the UI testable without a dialog:

  `--lang en|zh-CN` selects the interface language (the system locale is used when
  it is missing); the CJK font the Chinese UI needs is not part of the repository,
  see [fonts/README.md](fonts/README.md).

  ```
  ./build/Imagina.exe --frames 90 --screenshot shot.bmp
  ./build/Imagina.exe --windows --frames 30 --screenshot ui.bmp
  ./build/Imagina.exe --open zoom.imt --save zoom.im     # .im/.imt/.kfr/.raw/.png
  ./build/Imagina.exe --open zoom.im --frames 90 --screenshot shot.bmp
  ./build/Imagina.exe --decode frame.rawz frame.raw      # no window needed
  ```

## Development

`master` only contains reviewed work; development happens on `dev` and is merged
back with a pull request.  See [CONTRIBUTING.md](CONTRIBUTING.md).

## Versioning

The version lives in one place, the `project(... VERSION ...)` command in
`CMakeLists.txt`; `--version`, the About dialog and the Windows version resource
all come from it (`vcpkg.json` repeats it for the manifest).  It is `0.1.0` now and
stays `0.x` until the SDL front-end reaches parity with the Win32 one.  Releases use
`vMAJOR.MINOR.PATCH` tags.

```
$ ./build/Imagina --version
Imagina 0.1.0 (SDL front-end)
```

## License

GNU Affero General Public License v3.0, see [LICENSE.txt](LICENSE.txt).
