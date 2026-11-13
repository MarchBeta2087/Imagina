#!/usr/bin/env python3
"""Regenerates the icon artifacts from icon/imagina_icon.png.

    python3 tools/make-icon.py

Writes icon/imagina.ico (the Windows resource icon) and
src/sdl/WindowIcon.h (the 128x128 RGBA image the SDL front-end embeds, so its
window icon does not depend on where the program is installed).  Needs Pillow.
"""
import os
from PIL import Image

Root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
Source = os.path.join(Root, 'icon', 'imagina_icon.png')

src = Image.open(Source).convert('RGBA')

# One .ico with the sizes Explorer, the taskbar and the Alt-Tab list ask for.
src.save(os.path.join(Root, 'icon', 'imagina.ico'),
         sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])

icon = src.resize((128, 128), Image.LANCZOS)
data = icon.tobytes()
Header = os.path.join(Root, 'src', 'sdl', 'WindowIcon.h')
with open(Header, 'w', encoding='utf-8', newline='\n') as f:
    f.write('// Generated from icon/imagina_icon.png by tools/make-icon.py; do not edit.\n')
    f.write('// The window icon of the SDL front-end, 128x128 pixels, RGBA byte order.\n')
    f.write('#pragma once\n\n#include <cstdint>\n\n')
    f.write('static const int WindowIconWidth = 128;\n')
    f.write('static const int WindowIconHeight = 128;\n')
    f.write('static const uint8_t WindowIconPixels[] = {\n')
    for i in range(0, len(data), 16):
        f.write('\t' + ','.join('%d' % b for b in data[i:i + 16]) + ',\n')
    f.write('};\n')
print('wrote icon/imagina.ico and src/sdl/WindowIcon.h')
