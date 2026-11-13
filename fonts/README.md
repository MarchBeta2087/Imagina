# fonts/

The SDL front-end can display CJK text, but Dear ImGui's built-in font has no CJK
glyphs, so a font file has to be provided next to the executable (or in this
directory for a build tree).

**The `.ttf` is not distributed with this repository.**  This project is a fork of
`haveyouwantto/Imagina`, and GitHub refuses to accept Git LFS objects for forks, so
the 25 MB font cannot be checked in.  Download it yourself:

1. Get `SarasaMonoSC-Regular.ttf` from the Sarasa Gothic releases
   <https://github.com/be5invis/Sarasa-Gothic/releases> (the file is inside
   `Sarasa-TTF-<version>.7z` or the `SarasaMonoSC` asset of a newer release).
2. Put it here as `fonts/SarasaMonoSC-Regular.ttf`, or next to the executable.
3. Nothing has to be configured: the front-end looks for the file relative to the
   executable directory first, then in `fonts/` below the working directory, and
   falls back to the built-in font (with a warning in the console) when it is
   missing.

Licence: Sarasa Gothic is licensed under the SIL Open Font License 1.1, see
[LICENSE](LICENSE) in this directory (which ships with the repository).  The font
must keep its reserved font names; the OFL permits bundling it with this
(A)GPL-3.0 program.

---

## 简体中文

SDL 前端可以显示中日韩文字，但 Dear ImGui 内置字体没有 CJK 字形，所以需要提供字体文件
（放在可执行文件旁边，或者构建目录下的 `fonts/`）。

**`.ttf` 不随仓库分发。** 本项目是 `haveyouwantto/Imagina` 的 fork，GitHub 不允许向
fork 上传 LFS 对象，25 MB 的字体没法入库。请自行下载：

1. 从 Sarasa Gothic 的 releases 获取 `SarasaMonoSC-Regular.ttf`：
   <https://github.com/be5invis/Sarasa-Gothic/releases>（在
   `Sarasa-TTF-<版本>.7z` 里，或较新版本的 `SarasaMonoSC` 资源）。
2. 放到本目录并命名为 `fonts/SarasaMonoSC-Regular.ttf`，或放在可执行文件旁边。
3. 不需要额外配置：前端先找可执行文件目录，再找工作目录下的 `fonts/`，都找不到时回退到
   内置字体（并在控制台给出提示）。

许可：Sarasa Gothic 使用 SIL Open Font License 1.1，本目录的 [LICENSE](LICENSE) 随仓库
提供。字体需保留其保留字体名称；OFL 允许与采用 (A)GPL-3.0 的本程序一起分发。
