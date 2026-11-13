# Imagina

Imagina 是一个快速的分形渲染与探索工具。

_简体中文 | [English](Readme.md)_

---

**说明：** 本文档提供 [English](Readme.md) 与 [简体中文](Readme.zh-CN.md) 两个版本。
代码注释与提交信息使用英文；Issue 与 Pull Request 中英文均可。

## 目录

- [操作方式](#操作方式)
- [缩放序列导出](#缩放序列导出)
- [语言与字体](#语言与字体)
- [仓库结构](#仓库结构)
- [构建](#构建)
- [前端](#前端)
- [开发流程](#开发流程)
- [更新日志](CHANGELOG.md)
- [许可证](#许可证)

## 操作方式

* 鼠标左键
    * 拖动 —— 平移视图
    * 双击 —— 查找附近特征点并打开「特征」对话框
* 鼠标滚轮 —— 缩放
* A —— 提高色彩密度
* S —— 降低色彩密度
* D —— 提高迭代上限
* F —— 降低迭代上限
* E —— 颜色循环向前
* R —— 颜色循环向后

## 缩放序列导出

「File」→「Export zoom sequence...」会把一段缩放序列的关键帧写到当前中心，每个关键帧
一张无损压缩的原始像素图（`*.rawz`），旁边再加一份导出记录（`*.imz`），记录参数和已
写出的帧。这些帧的计算坐标保存在记录旁边的 `*.im` 位置文件里（「File」→「Save」），
记录只引用它，因此坐标只存一份；「File」→「Resume zoom sequence...」可以从记录继续
被中断的导出。展开帧可以用 `Imagina.exe --decode <frame.rawz> [<frame.raw>]`，或
`tools/rawz.py`（还能输出统计信息和彩色 PNG，见 `python tools/rawz.py --help`）。

关键帧的迭代上限跟随其尺度：从中心轨道（视图的周期）读出来，这样较宽的关键帧不会把
最深一帧的上限全部花在集合内部的像素上。某次导出会使用的上限可以用
`Imagina.exe --period <record.imz>` 查看。

## 语言与字体

SDL 前端支持英文与简体中文。`--lang en|zh-CN` 指定语言；不给时按系统语言
（`SDL_GetPreferredLocales`）选择，运行时也可以在「Help」→「Language」里切换。
字符串都在 `src/Language.cpp`：`Tr("英文原文")` 查表，查不到就回退英文，所以缺失
条目不会变成空白标签。`tools/check-translations.py` 会在源码里用到的字符串没有对应
条目时报错，CI 会运行它。

中文需要带中日韩字形的字体。界面会依次在可执行文件旁、其下的 `fonts/`、工作目录里
查找 `SarasaMonoSC-Regular.ttf`，找不到时回退内置字体并给出警告。字体文件是 Release
资源（`deps-fonts-v1`，见 [fonts/README.md](fonts/README.md)），用
`tools/fetch-fonts.ps1` 或 `tools/fetch-fonts.sh` 下载：

```
$ tools/fetch-fonts.sh
$ ./Imagina --lang zh-CN
```

字体按 18 像素乘以显示缩放光栅化；窗口移到缩放比例不同的显示器时界面会重新缩放。

`cmake --install <构建目录> --prefix <目录>` 会生成发布压缩包的布局：可执行文件、
`LICENSE.txt`、两份 Readme、`CHANGELOG.md`、含图标许可的 `icon/`，以及包含许可文件与
（已下载的）TTF 的 `fonts/` 目录。

## 仓库结构

```
src/                 共享的计算与渲染核心
  platform/          核心使用的平台层（GL 入口、内存、文件对话框）
  win32/             Win32 前端：窗口、菜单、对话框、资源文件
  sdl/               SDL3 + Dear ImGui 前端
  encoder/           自定义公式 JIT 使用的 x86 编码器（可选）
tests/               测试（原始像素图编解码往返测试）
tools/               辅助脚本（rawz.py）
```

## 构建

### 支持平台

| 层级 | 平台 | 验证情况 |
| --- | --- | --- |
| 1 | Windows x86-64（MSVC + vcpkg、MinGW-w64） | CI：两个前端均构建、编解码测试、SDL 前端实际渲染 |
| 1 | Linux x86-64（GCC、Clang） | CI：构建、编解码测试、Xvfb + 软件 OpenGL 无头渲染 |
| 2 | macOS（Apple Silicon、Intel） | 期望可以构建，无 CI，尽力而为 |
| 2 | FreeBSD 及其他 BSD | 期望可以构建（走 POSIX 代码路径），无 CI，未验证 |

只支持 x86-64：计算部分使用 AVX2 内建函数，因此 ARM 与 32 位构建暂时不在范围内。
Tier 2 的意思是「尽力保持可用」，但如果出问题，可能要等到有相应硬件的贡献者来处理。

如果你在 Tier 2 平台（或上表之外的平台）成功构建了 Imagina，欢迎在
[平台报告 Discussion](https://github.com/MarchBeta2087/Imagina/discussions/14) 里留言——
我们会据此决定是否提升某个平台的支持层级。

### 依赖

| 依赖 | Windows（vcpkg） | Linux | macOS |
| --- | --- | --- | --- |
| GMP（或 MPIR） | `gmp` | `libgmp-dev`（会自动改用 GMP） | `brew install gmp` |
| libpng | `libpng` | `libpng-dev` | `brew install libpng` |
| zlib | `zlib` | `zlib1g-dev` | `brew install zlib`（libpng 依赖） |
| glm | `glm` | `libglm-dev` | `brew install glm` |
| glext.h、wglext.h | `opengl-registry` | `libgl1-mesa-dev` | OpenGL framework |
| SDL3、Dear ImGui | `sdl3`、`imgui[sdl3-binding,opengl3-binding]` | 自动获取（或 `libsdl3-dev`） | 自动获取 |

MPIR 是 GMP 的分支，源码两者都支持（`src/GmpCompat.h`）。MPIR 已无维护、也不被任何发行版
打包，所以默认配置（`MPIR_LIBRARY=auto`）使用 GMP；两者写出的 `.im` 文件格式一致。
如果本机已装 MPIR，仍可用 `-DMPIR_LIBRARY=mpir` 构建。

凡是能以源码获取的依赖（glm、SDL3、Dear ImGui）在找不到时都会自动拉取，版本在
`CMakeLists.txt` 里固定（`IMAGINA_GLM_TAG`、`IMAGINA_SDL3_TAG`、`IMAGINA_IMGUI_TAG`）。

### CMake 变量

* `IMAGINA_DEPS_DIR` 存放随仓库分发依赖的目录，其下的 `include/` 与 `lib/`
  （Windows 构建使用；会设置 `INCLUDE_PATH`/`LIBRARY_PATH`）。
* `INCLUDE_PATH`、`LIBRARY_PATH` 优先查找头文件与库的位置。
* `IMAGINA_FRONTEND` `win32` 或 `sdl`。
* `MPIR_USE_DLL` 使用 MPIR 的 DLL 版本。默认 ON（静态链接设为 OFF）。
* `MPIR_LIBRARY`、`MPIRXX_LIBRARY` 默认 `auto`：优先 MPIR，找不到就用 GMP；
  也可以直接给库名或绝对路径。
* `ZLIB_LIBRARY`、`PNG_LIBRARY` 默认 `auto`：Windows 上为 `zlibwapi`/`libpng16`，
  其他平台使用系统的 `ZLIB::ZLIB`/`PNG::PNG`。
* `BUILD_GUI` 构建应用程序（默认 ON）。
* `BUILD_CODEC_TEST` 构建原始像素图编解码往返测试。
* `IMAGINA_ENABLE_LTO` 启用链接时优化（默认关闭，原因见下）。

### 离线构建

首次构建需要联网获取源码。没有网络时，用标准的 FetchContent 变量指向已有的源码目录，
Dear ImGui 也可以用 `IMAGINA_IMGUI_PATH`：

```
cmake -S . -B build   -DFETCHCONTENT_SOURCE_DIR_SDL3=/path/to/SDL   -DFETCHCONTENT_SOURCE_DIR_IMGUI=/path/to/imgui   -DFETCHCONTENT_SOURCE_DIR_GLM=/path/to/glm
```

### Windows

推荐使用 [vcpkg](https://vcpkg.io)：`vcpkg.json` 列出了全部依赖（GMP、libpng、
zlib、glm、`opengl-registry`，以及带 SDL3/OpenGL3 后端的 SDL3 与 Dear ImGui），
manifest 模式会自动获取并构建。

```
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
setx VCPKG_ROOT C:\vcpkg

cmake --preset windows-msvc        # Win32 前端（在 VS 开发者命令行中运行）
cmake --build --preset windows-msvc --parallel

cmake --preset windows-msvc-sdl    # SDL3 + Dear ImGui 前端
cmake --build --preset windows-msvc-sdl --parallel
```

设置了 `VCPKG_ROOT` 时，`build.ps1` 会自动使用 vcpkg 工具链，并自动选择编译器
（`-Compiler msvc|gcc|clang|clang-cl|auto`）：

```
powershell -ExecutionPolicy Bypass -File build.ps1                  # 自动检测 MSVC
powershell -ExecutionPolicy Bypass -File build.ps1 -Compiler gcc    # MinGW-w64 GCC
powershell -ExecutionPolicy Bypass -File build.ps1 -NoVcpkg         # 旧的 `include\`/`lib\` 布局
powershell -ExecutionPolicy Bypass -File build.ps1 -Frontend sdl    # SDL3 + Dear ImGui 前端
```

不使用 vcpkg（或加 `-NoVcpkg`）时，依赖从脚本旁边的 `include\` 和 `lib\` 读取；
`build.ps1` 开头的注释列出了需要哪些文件、MPIR/libpng/zlib 如何构建。MinGW-w64 也可以
直接使用 MSYS2 的包（`mingw-w64-ucrt-x86_64-gmp`、`-libpng`、`-zlib`、`-mesa`），CI 就是
这么做的。

### Linux、macOS、MSYS2

`build.sh` 用 GCC 或 Clang 配置同一个 CMake 工程：

```
./build.sh --compiler clang --test
./build.sh --frontend sdl --compiler clang
./build.sh --clean -- -DENABLE_CUSTOM_FORMULA=ON
```

## 前端

有两个前端，用 `-DIMAGINA_FRONTEND=win32|sdl` 选择（Windows 上默认 `win32`，其他平台
默认 `sdl`）：

* `win32` —— 原有的窗口、菜单和对话框（`src/win32/MainWindow.cpp`、
  `src/win32/Resource.rc`、`src/win32/ZoomExport.cpp`）。仅支持 Windows。
* `sdl` —— SDL3 + Dear ImGui（`src/sdl/SDLMain.cpp`）。可在 Windows、Linux 和 macOS
  上构建，与 Win32 前端共用全部计算与渲染核心，包括文件格式（`src/File.cpp`）。
  缩放序列导出（`ZoomExport.cpp`）目前仍只支持 Win32。

SDL3 与 Dear ImGui 先尝试 `find_package`，找不到再从 GitHub 获取；`-DIMAGINA_IMGUI_PATH=<dir>`
可以指向已有的 Dear ImGui 源码目录，`-DMPIR_LIBRARY`/`-DMPIRXX_LIBRARY`/`-DZLIB_LIBRARY`
用于指定当前工具链的库名。

注意：

* GNU/Clang 的 Release 使用 `-O2` 和 `-mprefer-vector-width=128`。GCC 16 会把 32 字节
  的结构体拷贝编译成对 32 字节对齐地址的 256 位写入，而 Windows 上栈只保证 16 字节
  对齐，于是偶发崩溃（见 issue #6）；把自动向量化限制在 128 位即可避开这些指令，计算
  部分的 `_mm256_*` 内建函数不受影响。链接时优化默认关闭（可加
  `-DIMAGINA_ENABLE_LTO=ON` 试用）。
* `IMAGINA_THREADS=<n>` 限制工作线程数。
* `IMAGINA_CRASH_REPORT=1` 安装异常处理器，崩溃时打印故障模块偏移与栈回溯（Windows）。
* SDL 前端支持 `--frames <n>`（跑 n 帧后退出）、`--screenshot <file.bmp>`、
  `--open <file>`、`--save <file>`（按扩展名决定格式）、`--windows`（打开全部对话框）
  和 `--stress`（每帧缩放），因此渲染、文件格式和界面都能在没有人工点击的情况下测试：

  `--lang en|zh-CN` 选择界面语言（不给时跟随系统语言）；中文界面需要的 CJK 字体不随
  仓库分发，见 [fonts/README.md](fonts/README.md)。

  ```
  ./build/Imagina.exe --frames 90 --screenshot shot.bmp
  ./build/Imagina.exe --windows --frames 30 --screenshot ui.bmp
  ./build/Imagina.exe --open zoom.imt --save zoom.im     # .im/.imt/.kfr/.raw/.png
  ./build/Imagina.exe --open zoom.im --frames 90 --screenshot shot.bmp
  ./build/Imagina.exe --decode frame.rawz frame.raw      # 无需窗口
  ```

## 开发流程

`master` 只保留经过审查的代码；日常开发在 `dev` 分支进行，通过 Pull Request 合并回
`master`。详见 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 版本号

版本号只有一处来源：`CMakeLists.txt` 里的 `project(... VERSION ...)`；`--version`、
「关于」对话框和 Windows 版本资源都由它生成（`vcpkg.json` 里为 manifest 重复了一次）。
目前是 `1.0.0`。发布使用 `vMAJOR.MINOR.PATCH` 形式的标签，每次发布的内容记录在
[CHANGELOG.md](CHANGELOG.md)。

```
$ ./build/Imagina --version
Imagina 1.0.0 (SDL front-end)
```

## 许可证

源代码采用 GNU Affero General Public License v3.0，见 [LICENSE.txt](LICENSE.txt)。

[icon/](icon/) 里的图标（以及由它生成的窗口图标）不属于源代码，已按 CC0 1.0
贡献到公有领域，见 [icon/LICENSE](icon/LICENSE)。
