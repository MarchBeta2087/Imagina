# Program icon / 程序图标

`imagina_icon.png` is the source of the program icon.  Two artefacts are generated
from it by `tools/make-icon.py`:

- `imagina.ico` (16–256 px) — the resource icon of the Win32 front-end, loaded as
  `IDI_ICON1` by the window class;
- `../src/sdl/WindowIcon.h` — the 128×128 RGBA image compiled into the SDL
  front-end and passed to `SDL_SetWindowIcon`, so the window icon does not depend
  on a data file next to the executable.

`imagina_icon.png` 是程序图标的源图。`tools/make-icon.py` 由它生成两个产物：

- `imagina.ico`（16–256 px）—— Win32 前端的资源图标，由窗口类以 `IDI_ICON1` 加载；
- `../src/sdl/WindowIcon.h` —— 编译进 SDL 前端的 128×128 RGBA 图像，交给
  `SDL_SetWindowIcon`，因此窗口图标不依赖可执行文件旁的数据文件。

Regenerate after changing the source image (needs Pillow):

```
python tools/make-icon.py
```

修改源图后重新生成（需要 Pillow）：

```
python tools/make-icon.py
```

## Licence / 许可

CC0 1.0 (public domain), see [LICENSE](LICENSE).  The source code of Imagina is
AGPL-3.0; the icon is the one part of the repository that is not.

CC0 1.0（公有领域），见 [LICENSE](LICENSE)。Imagina 的源代码使用 AGPL-3.0；图标是
仓库中唯一不适用该许可的部分。
