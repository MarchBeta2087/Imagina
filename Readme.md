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

### Dependencies

* mpir
* libpng
* zlib (used by libpng)
* glm
* glext.h, wglext.h, KHR/khrplatform.h

### CMake variables

* `INCLUDE_PATH` Path to include files.
* `LIBRARY_PATH` Path to libraries.
* `MPIR_USE_DLL` Use dll version of MPIR. Default: ON (To use statically linked version, set this to OFF)
* `ZLIB_LIBRARY`, `PNG_LIBRARY`, `MPIR_LIBRARY`, `MPIRXX_LIBRARY` Library names, for toolchains that use different ones (`zlibwapi`/`z`, import libraries vs. `lib*.a`)
* `IMAGINA_FRONTEND` `win32` or `sdl`
* `BUILD_GUI` Build the application (default: ON)
* `BUILD_CODEC_TEST` Build the raw map codec round trip test
* `IMAGINA_ENABLE_LTO` Build with link time optimisation (off by default, see below)

### Windows

Put the dependency headers into `include\` and their libraries into `lib\`, then run
`build.ps1`; its comments list what is needed and how the dependencies are built.

```
powershell -ExecutionPolicy Bypass -File build.ps1                  # MSVC (detected)
powershell -ExecutionPolicy Bypass -File build.ps1 -Compiler gcc    # MinGW-w64 GCC
powershell -ExecutionPolicy Bypass -File build.ps1 -Compiler clang  # LLVM/Clang
```

### Linux, macOS, MSYS2

`build.sh` configures the same CMake project with GCC or Clang:

```
./build.sh --compiler clang --test
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

* GNU/Clang releases are compiled with `-O2`. The `-O3` build of the computation
  crashes intermittently (see issue #6) and link time optimisation is off by default
  (`-DIMAGINA_ENABLE_LTO=ON` to try it).
* `IMAGINA_THREADS=<n>` limits the worker pool.
* `IMAGINA_CRASH_REPORT=1` installs an exception handler that prints the faulting
  module offset and a stack walk (Windows).
* The SDL front-end accepts `--frames <n>` (quit after n frames),
  `--screenshot <file.bmp>`, `--open <file>`, `--save <file>` (the extension picks
  the format), `--windows` (open every dialog) and `--stress` (zoom every frame),
  which makes the renderer, the file formats and the UI testable without a dialog:

  ```
  ./build/Imagina.exe --frames 90 --screenshot shot.bmp
  ./build/Imagina.exe --windows --frames 30 --screenshot ui.bmp
  ./build/Imagina.exe --open zoom.imt --save zoom.im     # .im/.imt/.kfr/.raw/.png
  ./build/Imagina.exe --open zoom.im --frames 90 --screenshot shot.bmp
  ```

## Development

`master` only contains reviewed work; development happens on `dev` and is merged
back with a pull request.  See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

GNU Affero General Public License v3.0, see [LICENSE.txt](LICENSE.txt).
