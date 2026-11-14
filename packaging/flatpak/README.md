# Flatpak

`org.imagina.Imagina.yml` builds the SDL front-end with the freedesktop
Platform SDK.  The manifest fetches SDL3, Dear ImGui, GLM, GMP and the CJK font
from their upstream sources; the application itself is taken from the checkout
(`type: dir`), so no release tarball is needed for a local build.

## Build with flatpak-builder or org.flatpak.Builder

```
flatpak remote-add --if-not-exists --user flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install --user -y flathub org.flatpak.Builder
flatpak run --command=flatpak-builder org.flatpak.Builder \
    --user --disable-rofiles-fuse --install --force-clean build-flatpak \
    packaging/flatpak/org.imagina.Imagina.yml
flatpak run org.imagina.Imagina
```

or, with `flatpak-builder` installed on the host:

```
flatpak-builder --user --install --force-clean build-flatpak packaging/flatpak/org.imagina.Imagina.yml
```

`flatpak-builder` downloads the Platform/SDK the first time (about 1.5 GB).

`--disable-rofiles-fuse` is needed inside the `org.flatpak.Builder` sandbox:
the rofiles-fuse mount it creates is not visible to the builder itself, so the
finish step fails with `Command 'Imagina' not found` without it.

## Layout

- `/app/bin/Imagina` (system install layout)
- `/app/share/imagina/fonts/SarasaMonoSC-Regular.ttf`
- `/app/share/applications/org.imagina.Imagina.desktop`
- `/app/share/icons/hicolor/{128x128,256x256,512x512}/apps/org.imagina.Imagina.png`
- `/app/share/metainfo/org.imagina.Imagina.metainfo.xml`
- `/app/share/licenses/org.imagina.Imagina/LICENSE.txt`

## Flathub

For a Flathub submission the `type: dir` source of the `imagina` module has to
be replaced by a pinned source (a git commit or a release tarball with a
`sha256`).  The rest of the manifest, the desktop file, the metainfo file and
the icons are already in the layout Flathub expects.  Screenshots for the
AppStream listing are still missing.
