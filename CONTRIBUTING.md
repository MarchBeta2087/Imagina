# Contributing / 贡献指南

English | [简体中文](#简体中文)

Thanks for taking the time to contribute.  This document is short on purpose: it
describes the branch layout, what a good change looks like and how to test it.

## Branches

* `master` holds reviewed, working code.  Nothing is pushed to it directly.
* `dev` is the integration branch.  Feature branches are cut from `dev` and merged
  back into `dev` with a pull request.
* `master` is updated by a pull request from `dev` when a set of changes is ready
  for a release.

```
mainline:  master  <---------------------- PR ----------------------+
                     \                                             /
integration:          dev <-- PR -- dev <-- PR -- dev   (daily work)
                        \                     /
feature:                 +-- feature/x -----+
```

Please base your work on the latest `dev`:

```
git switch dev
git pull
git switch -c feature/short-description
```

## What a good change looks like

* Keep the change focused; separate refactoring from behaviour changes.
* Match the style of the file you are editing: tabs for indentation, `PascalCase`
  for types, `camelCase` for functions and members, `//` comments in English.
* The shared core in `src/` must stay free of platform APIs.  Anything that needs
  the Win32 API belongs in `src/win32/`, anything that needs SDL in `src/sdl/`;
  add a hook in `src/platform/Platform.h` (and implement it in both front-ends)
  when the core needs to call into the platform.
* Do not add new global state to the core without a reason; the front-ends own the
  UI state (`WindowState.h`, `Global.h`).
* Keep the two front-ends in sync when you add a user visible feature, or note in
  the pull request why one of them does not have it yet.
* Update `Readme.md` **and** `Readme.zh-CN.md` when you change behaviour, options or
  the build instructions.

## Testing

At minimum, make sure the code builds and the codec test passes:

```
./build.sh --test            # GCC or Clang, Linux/macOS/MSYS2
powershell -ExecutionPolicy Bypass -File build.ps1 -Test    # Windows
./build/RawMapCodecTest
```

For changes to the renderer, the UI or the file formats, the SDL front-end can be
driven without clicking:

```
./build/Imagina.exe --frames 90 --screenshot shot.bmp
./build/Imagina.exe --windows --frames 30 --screenshot ui.bmp
./build/Imagina.exe --open zoom.imt --save zoom.im
./build/Imagina.exe --stress --frames 120        # zoom/cancel stress test
```

`IMAGINA_CRASH_REPORT=1` prints a stack walk if it crashes (see issue #6 for the
currently known intermittent crash).

The Win32 front-end can only be built on Windows; say so in the pull request if you
did not build it.

Tier 1 platforms are Windows x86-64 and Linux x86-64 (the CI covers Linux with GCC and
Clang, including a headless render check).  macOS is Tier 2: the build is kept working
as far as possible, but there is no CI, so say so if a change only builds there.

## Issues and pull requests

* Bug reports and feature requests use the templates in `.github/ISSUE_TEMPLATE/`.
* Pull requests target `dev`, not `master`.
* Please describe **what** changed, **why**, and **how it was tested**; screenshots
  help for anything visual.
* English and Chinese are both fine.

---

## 简体中文

感谢你愿意贡献代码。这份文档很短，只说明分支模型、代码要求和验证方式。

### 分支

* `master` 只放经过审查、确认可用的代码，不直接推送。
* `dev` 是集成分支。功能分支从 `dev` 切出，通过 Pull Request 合回 `dev`。
* 一批改动准备好发布时，用 `dev` → `master` 的 Pull Request 更新 `master`。

请把工作建立在最新的 `dev` 上：

```
git switch dev
git pull
git switch -c feature/简短描述
```

### 代码要求

* 一个改动只做一件事，重构和行为变更分开提交。
* 跟随所在文件的风格：缩进用制表符，类型用 `PascalCase`，函数和成员用
  `camelCase`，注释用英文 `//`。
* `src/` 里的共享核心不得依赖平台 API。需要 Win32 的放到 `src/win32/`，需要 SDL 的
  放到 `src/sdl/`；如果核心需要调用平台功能，在 `src/platform/Platform.h` 加钩子，
  并在两个前端各实现一次。
* 不要无故给核心增加全局状态；界面状态归前端所有（`WindowState.h`、`Global.h`）。
* 新增用户可见功能时尽量两个前端都跟上；如果暂时只做一个，请在 PR 里说明原因。
* 修改行为、选项或构建说明时，请同时更新 `Readme.md` **和** `Readme.zh-CN.md`。

### 验证

至少保证能编译通过、编解码测试通过：

```
./build.sh --test            # GCC 或 Clang，Linux/macOS/MSYS2
powershell -ExecutionPolicy Bypass -File build.ps1 -Test    # Windows
./build/RawMapCodecTest
```

涉及渲染、界面或文件格式的改动，SDL 前端可以无人值守地跑：

```
./build/Imagina.exe --frames 90 --screenshot shot.bmp
./build/Imagina.exe --windows --frames 30 --screenshot ui.bmp
./build/Imagina.exe --open zoom.imt --save zoom.im
./build/Imagina.exe --stress --frames 120        # 缩放/取消压力测试
```

崩溃时设置 `IMAGINA_CRASH_REPORT=1` 会打印栈回溯（当前已知的偶发崩溃见 issue #6）。

Win32 前端只能在 Windows 上构建；如果没有构建它，请在 PR 里注明。

Tier 1 平台是 Windows x86-64 与 Linux x86-64（CI 覆盖 Linux 的 GCC/Clang，并做无头渲染
检查）。macOS 属于 Tier 2：尽量保持可构建，但没有 CI，如果某个改动只能在那里构建，
请在 PR 里说明。

### Issue 与 Pull Request

* 缺陷报告和功能建议请使用 `.github/ISSUE_TEMPLATE/` 中的模板。
* Pull Request 的目标分支是 `dev`，不是 `master`。
* 请说明**改了什么**、**为什么**、**怎么验证的**；涉及界面的改动附截图更好。
* 中英文都可以。
