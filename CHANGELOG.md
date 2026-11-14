# Changelog / 更新日志

All notable changes to Imagina are recorded here.  The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Imagina 的所有重要变更都记录在这里。格式沿用
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/)，版本号遵循
[语义化版本](https://semver.org/lang/zh-CN/)。

## [Unreleased]

### Fixed / 修复

- The optional custom formula build did not compile with GCC or Clang: the
  encoder headers relied on transitive includes, the `and`/`or`/`not` macros
  shadowed C++ keywords, and a function pointer was converted to `void *`
  implicitly.
  可选的自定义公式构建在 GCC/Clang 下无法编译：编码器头文件依赖间接包含，
  `and`/`or`/`not` 宏遮蔽了 C++ 关键字，函数指针被隐式转换成 `void *`。
- Custom formulas are now a Windows-only option; configuring them elsewhere
  stops with a clear message instead of failing inside the JIT sources.
  自定义公式现在仅支持 Windows；在其他平台启用时会在配置阶段给出明确错误，
  而不是在 JIT 源码中编译失败。
- A vcpkg tree next to the sources is found again after switching compilers,
  so the Win32 build no longer stops at `GL/glext.h`.
  切换编译器后仍能找到源码旁的 vcpkg 目录，Win32 构建不再卡在 `GL/glext.h`。
- The GCC and Clang builds link the platform thread library, so Clang on
  MinGW no longer stops at `pthread_mutex_lock`.
  GCC/Clang 构建会链接平台线程库，MinGW 下的 Clang 不再卡在 `pthread_mutex_lock`。
- The About dialog now lists the maintained fork first and is tall enough to
  show all source links instead of clipping them.
  关于对话框把维护者的仓库链接放在最前，并加大了高度，所有源码链接都能显示。
- Saving now writes a temporary file next to the target and renames it into
  place only after the whole location has been written, so a crash or a full
  disk no longer truncates the previous save to zero bytes.
  保存时先写入目标旁的临时文件，写入完成后再替换目标；崩溃或磁盘写满不再把原有
  存档截断为 0 字节。
- Saving a location that does not need a reference orbit (the starting view,
  or an orbit shorter than eight iterations) read an empty orbit array and
  crashed.  These locations are now written without a reference section and
  regenerate it when they are opened.
  保存不需要参考轨道的位置（初始视图，或轨道短于 8 次迭代）时会读取空的轨道
  数组并崩溃；现在这类位置不写入参考轨道，打开时会自动重新生成。
- The transformation dialog wrote one byte past a 32 byte stack buffer
  (`buffer[32] = 0`) and corrupted the return address, so pressing OK crashed.
  Flip imaginary axis only made the bad frame more likely to be hit.
  变换对话框在 32 字节栈缓冲区的末尾越界写入（`buffer[32] = 0`），破坏返回地址，
  按下「确定」即崩溃；勾选「Flip imaginary axis」只是让问题更容易触发。
- Crash reports now go to a writable per-user directory instead of next to the
  executable: `%LOCALAPPDATA%\Imagina\Imagina-crash.txt` on Windows and
  `$XDG_STATE_HOME/imagina/` (or `~/.local/state/imagina/`) on Linux/BSD.  If
  that directory cannot be created or written the report falls back to the
  temporary directory, and the error dialog shows the path actually used.
  崩溃报告改为写入用户可写目录，不再写在可执行文件旁：Windows 为
  `%LOCALAPPDATA%\Imagina\Imagina-crash.txt`，Linux/BSD 为
  `$XDG_STATE_HOME/imagina/`（或 `~/.local/state/imagina/`）。目录创建或写入失败
  时退回临时目录，错误框会显示实际使用的路径。
- The iteration limit and image size dialogs passed no size argument for `%c` to
  `sscanf_s`, which MSVC reports as C4473.
  迭代上限与图像尺寸对话框的 `sscanf_s` 没有给 `%c` 传长度参数，MSVC 会报 C4473。

## [1.0.3] - 2026-11-13

### Fixed / 修复

- Saving a location (File -> Save, `*.im`) crashed when the reference orbit was
  written: the orbit array holds `complex<HRReal>`, but the writer decided on a
  `complex<double>` stride from the stage precision and ran past the end.
  `Reference` now records the element type it was loaded with, and freshly
  computed references are always saved in the `HRReal` form.
  保存位置（文件 → 保存，`*.im`）在写入参考轨道时崩溃：轨道数组是
  `complex<HRReal>`，写入端却按阶段精度当成 `complex<double>` 计算步长而越界。
  现在 `Reference` 记录载入时的元素类型，新计算的参考轨道一律按 `HRReal` 保存。
- The language menu is its own top level entry after Help, and switching works in
  both directions (it could not go back to English).
  语言菜单独立为「帮助」之后的顶级菜单，并且可以来回切换（之前切不回英文）。
- `build.sh` built only the codec test outside Windows; the window application is
  the default on every platform now.
  `build.sh` 在非 Windows 平台只编译编解码测试；现在所有平台默认编译窗口程序。
- The About window lists this fork's repository next to the original ones.
  「关于」窗口在原有仓库链接之外列出本 fork 的仓库。

### Added / 新增

- `Imagina-crash.txt`: a crash report next to the executable with the faulting
  module/offset, the faulting address and a 32 frame stack walk.
  崩溃报告 `Imagina-crash.txt`：可执行文件旁记录出错模块/偏移、出错地址和 32 帧调用栈。

## [1.0.2] - 2026-11-13

### Fixed / 修复

- A `VCPKG_ROOT` build on Windows now works from a clean directory: the vcpkg
  include and library directories are added for the bare `#include`s this project
  uses, the `GL/glext.h` check no longer stops a vcpkg build, and the zlib/libpng
  and GMP/MPIR search no longer caches its fallback into the variables
  `FindZLIB`/`FindPNG` own (which made a first failed configure permanent).
  在 Windows 上用 `VCPKG_ROOT` 现在可以从空目录构建成功：项目用到的裸 `#include`
  会找到 vcpkg 的 include/lib 目录，`GL/glext.h` 检查不再拦住 vcpkg 构建，
  zlib/libpng 与 GMP/MPIR 的查找结果也不再写回 `FindZLIB`/`FindPNG` 自己的缓存变量
  （之前第一次配置失败后会一直失败）。

## [1.0.1] - 2026-11-13

### Fixed / 修复

- A Win32 build without vcpkg failed with `cannot open include file:
  GL/glext.h`; the Windows SDK does not ship the Khronos headers and the
  repository does not carry them.  `tools/fetch-headers.ps1`/`.sh` download them
  into `include/`, and both the preprocessor and the configure step now say so
  instead of failing on a missing include.
  不用 vcpkg 的 Win32 构建会因为缺少 `GL/glext.h` 失败（Windows SDK 不含这些 Khronos
  头文件）。`tools/fetch-headers.ps1`/`.sh` 会把它们下载到 `include/`；预处理器和
  configure 阶段现在都会给出这个提示，而不是一句缺少头文件。

## [1.0.0] - 2026-11-13

### Added / 新增

- The UI language can be switched at run time from "Help" -> "Language" in both
  front-ends, and `--lang en|zh-CN` still selects it at start-up.
  两个前端都可以在运行时从「Help」→「Language」切换界面语言，启动时仍可用
  `--lang en|zh-CN`。
- The whole Win32 front-end is localised: menus, dialog boxes and message boxes
  go through the same string table as the SDL front-end.
  Win32 前端完成本地化：菜单、对话框和消息框都走与 SDL 前端相同的字符串表。
- The export status line and the export errors are translatable format strings.
  导出状态行与导出错误改为可翻译的格式串。
- HiDPI: the ImGui font is rasterised at 18 px times the display scale, the style
  is scaled with it, and both follow the window to a display with another scale.
  HiDPI：ImGui 字体按 18 像素乘以显示缩放光栅化，样式随之缩放，窗口移到缩放比例
  不同的显示器时两者都会跟随。
- `tools/check-translations.py`: fails when a string passed to `Tr()`/`TrW()` has
  no translation.  CI runs it and renders the UI in Chinese.
  `tools/check-translations.py`：源码里用了 `Tr()`/`TrW()` 却没有翻译时报错。CI 会
  运行它并渲染一次中文界面。
- `cmake --install` produces the layout of the release archive: executable,
  licence, Readmes, changelog, and `fonts/` with the licence and the font.
  `cmake --install` 生成发布压缩包的布局：可执行文件、许可、Readme、更新日志，以及
  含许可文件与字体的 `fonts/`。

### Licensing / 许可

- The program icon (`icon/`) is dedicated to the public domain under CC0 1.0; the
  source code stays AGPL-3.0.  See `icon/LICENSE`.
  程序图标（`icon/`）按 CC0 1.0 贡献到公有领域；源代码仍为 AGPL-3.0，见
  `icon/LICENSE`。

### Fixed / 修复

- `ErrorMessage()` of the Win32 front-end shows UTF-8 text as Unicode instead of
  asking the ANSI message box to interpret it.
- The About window finds `LICENSE.txt` next to an installed executable, not only
  at the path the configure step saw.


## [0.2.0] - 2026-11-13

### Added / 新增

- Zoom sequence export in the SDL front-end: a dialog for size, zoom step,
  target and record path, progress and cancel, resuming an interrupted record,
  and `--zoom-export <record>` for headless runs.
  SDL 前端的缩放序列导出：设置尺寸、缩放步长、目标与记录文件的对话框，进度与取消，
  断点续导，以及用于无界面运行的 `--zoom-export <record>`。
- `PlatformKeepDisplayOn()` (SDL keeps the screen saver off while an export runs).
  `PlatformKeepDisplayOn()`：导出期间保持屏幕唤醒。
- The record layer of the export (`ZoomExportRecord`, key frame scheduling,
  `RawMapCodec` glue) lives in the shared core, so both front-ends use it.
  导出的记录层（记录文件、关键帧调度、`RawMapCodec` 对接）下沉到共享核心，两个前端共用。
- Program icon: `icon/imagina.ico` in the Windows resources, and a 128×128 icon
  compiled into the SDL front-end (`tools/make-icon.py` regenerates both).
  程序图标：Windows 资源使用 `icon/imagina.ico`，SDL 前端内嵌 128×128 图标
  （`tools/make-icon.py` 可重新生成两者）。
- CI: the Linux jobs export a zoom sequence with `--zoom-export`, read the first
  key frame back with `--decode` and check that its pixels are real values.
  CI：Linux 任务用 `--zoom-export` 导出序列，再用 `--decode` 读回首帧并校验像素数值。

### Fixed / 修复

- Key frames of an export now really have the configured size: a running export
  owns the resolution, and both front-ends no longer make it follow the window.
  导出的关键帧现在真的是配置的尺寸：导出期间分辨率归导出所有，两个前端不再让它跟随窗口。
- The exported raw maps are validated by reading them back, so a record never
  references a key frame file that does not hold its pixels.
  导出的 raw map 会回读校验，记录文件不会引用没有正确像素的关键帧文件。

## [0.1.0] - 2026-11-13

First pre-release: the portable front-end and the tooling around it.
第一个预发布版：可移植前端及其周边工具。

### Added / 新增

- SDL3 + Dear ImGui front-end (`src/sdl`) next to the Win32 one, sharing the
  whole core; Windows, Linux and the BSDs build it.
  在 Win32 前端之外新增 SDL3 + Dear ImGui 前端（`src/sdl`），共用整个核心；
  Windows、Linux 与各 BSD 均可构建。
- Platform layer (`src/platform`): memory, threads and the file dialogs are no
  longer Win32-specific.
  平台层（`src/platform`）：内存、线程与文件对话框不再依赖 Win32。
- Localisation: `Tr()` string table, `--lang en|zh-CN`, and a CJK font loaded
  with a fallback if it is missing.
  本地化：`Tr()` 字符串表、`--lang en|zh-CN`，以及带回退的 CJK 字体加载。
- `--decode` and `--period` in the portable front-end.
  可移植前端提供 `--decode` 与 `--period`。
- Version numbers generated from `project()` and shown in `--version`, the About
  window and the Windows resources.
  版本号由 `project()` 生成，并在 `--version`、About 窗口和 Windows 资源中显示。
- CMake presets, `vcpkg.json`, `build.sh`, and `-Frontend win32|sdl` in the build
  scripts.
  CMake 预设、`vcpkg.json`、`build.sh`，以及构建脚本中的 `-Frontend win32|sdl`。
- CI: Linux (GCC and Clang, headless render check under Xvfb) and Windows
  (MSVC with vcpkg, MinGW-w64).
  CI：Linux（GCC 与 Clang，Xvfb 下无头渲染校验）和 Windows（MSVC + vcpkg、MinGW-w64）。
- Documentation: bilingual Readme, CONTRIBUTING, issue and PR templates, and the
  platform support tiers.
  文档：双语 Readme、CONTRIBUTING、Issue 与 PR 模板，以及平台支持分级。

### Fixed / 修复

- GCC 16 miscompiled the aligned vectors of the evaluator; GNU and Clang builds
  now cap the vector width at 128 bits.
  GCC 16 会误编译求值器的对齐向量；GNU 与 Clang 构建现在把向量宽度限制在 128 位。
- The released CJK font is fetched from a release asset (`deps-fonts-v1`), since
  the fork cannot hold it through Git LFS.
  由于 fork 无法通过 Git LFS 保存字体，发布的 CJK 字体改为从 Release 资源
  （`deps-fonts-v1`）下载。

[Unreleased]: https://github.com/MarchBeta2087/Imagina/compare/v1.0.3...dev
[1.0.3]: https://github.com/MarchBeta2087/Imagina/compare/v1.0.2...v1.0.3
[1.0.2]: https://github.com/MarchBeta2087/Imagina/compare/v1.0.1...v1.0.2
[1.0.1]: https://github.com/MarchBeta2087/Imagina/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/MarchBeta2087/Imagina/compare/v0.2.0...v1.0.0
[0.2.0]: https://github.com/MarchBeta2087/Imagina/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/MarchBeta2087/Imagina/releases/tag/v0.1.0
