# Changelog / 更新日志

All notable changes to Imagina are recorded here.  The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Imagina 的所有重要变更都记录在这里。格式沿用
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/)，版本号遵循
[语义化版本](https://semver.org/lang/zh-CN/)。

## [Unreleased]

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

[Unreleased]: https://github.com/MarchBeta2087/Imagina/compare/v0.2.0...dev
[0.2.0]: https://github.com/MarchBeta2087/Imagina/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/MarchBeta2087/Imagina/releases/tag/v0.1.0
