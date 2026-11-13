# fonts/

The SDL front-end can display CJK text, but Dear ImGui's built-in font has no CJK
glyphs, so a font file has to be provided next to the executable (or in this
directory for a build tree).

**The `.ttf` is not distributed with this repository.**  This project is a fork of
`haveyouwantto/Imagina`, and GitHub refuses to accept Git LFS objects for forks, so
the 25 MB font is published as a release asset instead:

<https://github.com/MarchBeta2087/Imagina/releases/tag/deps-fonts-v1>

Fetch it with the helper script, which also falls back to the GitHub CLI when a
network cannot reach the host GitHub redirects release downloads to:

```
tools\fetch-fonts.ps1            # Windows
./tools/fetch-fonts.sh           # Linux, macOS, MSYS2
```

Either way the file ends up here as `fonts/SarasaMonoSC-Regular.ttf`.  Then nothing
has to be configured: the front-end looks for the file next to the executable
first, then in `fonts/` below the executable directory and below the working
directory, and falls back to the built-in font (with a warning in the console)
when it is missing.

Licence: Sarasa Gothic is licensed under the SIL Open Font License 1.1, see
[LICENSE](LICENSE) in this directory (which ships with the repository).  The font
must keep its reserved font names; the OFL permits bundling it with this
(A)GPL-3.0 program.

---

## 简体中文

SDL 前端可以显示中日韩文字，但 Dear ImGui 内置字体没有 CJK 字形，所以需要提供字体文件
（放在可执行文件旁边，或者构建目录下的 `fonts/`）。

**`.ttf` 不随仓库分发。** 本项目是 `haveyouwantto/Imagina` 的 fork，GitHub 不允许向
fork 上传 LFS 对象，所以 25 MB 的字体改为以 release 资源发布：

<https://github.com/MarchBeta2087/Imagina/releases/tag/deps-fonts-v1>

可以用脚本下载（如果网络无法访问 GitHub 的下载重定向主机，脚本会自动改用 GitHub CLI）：

```
tools\fetch-fonts.ps1            # Windows
./tools/fetch-fonts.sh           # Linux、macOS、MSYS2
```

下载后会得到本目录下的 `fonts/SarasaMonoSC-Regular.ttf`。之后不需要额外配置：前端先找
可执行文件旁边的字体，再找可执行文件目录和当前工作目录下的 `fonts/`，都找不到时回退到
内置字体（并在控制台给出提示）。

许可：Sarasa Gothic 使用 SIL Open Font License 1.1，本目录的 [LICENSE](LICENSE) 随仓库
提供。字体需保留其保留字体名称；OFL 允许与采用 (A)GPL-3.0 的本程序一起分发。
