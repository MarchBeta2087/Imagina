# Flatpak

`io.github.marchbeta2087.Imagina.yml` builds the SDL front-end with the
freedesktop Platform SDK.  The manifest fetches SDL3, Dear ImGui, GLM and the
CJK font from their upstream sources, links GMP/libpng/zlib from the runtime,
and takes the application itself from the checkout (`type: dir`), so no release
tarball is needed for a local build.

## Build with flatpak-builder or org.flatpak.Builder

```
flatpak remote-add --if-not-exists --user flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install --user -y flathub org.flatpak.Builder
flatpak run --command=flatpak-builder org.flatpak.Builder \
    --user --disable-rofiles-fuse --install --force-clean build-flatpak \
    packaging/flatpak/io.github.marchbeta2087.Imagina.yml
flatpak run io.github.marchbeta2087.Imagina
```

or, with `flatpak-builder` installed on the host:

```
flatpak-builder --user --install --force-clean build-flatpak packaging/flatpak/io.github.marchbeta2087.Imagina.yml
```

`flatpak-builder` downloads the Platform/SDK the first time (about 1.5 GB).

`--disable-rofiles-fuse` is needed inside the `org.flatpak.Builder` sandbox:
the rofiles-fuse mount it creates is not visible to the builder itself, so the
finish step fails with `Command 'Imagina' not found` without it.

## Layout

- `/app/bin/Imagina` (system install layout)
- `/app/share/imagina/fonts/SarasaMonoSC-Regular.ttf`
- `/app/share/applications/io.github.marchbeta2087.Imagina.desktop`
- `/app/share/icons/hicolor/{128x128,256x256,512x512}/apps/io.github.marchbeta2087.Imagina.png`
- `/app/share/metainfo/io.github.marchbeta2087.Imagina.metainfo.xml`
- `/app/share/licenses/io.github.marchbeta2087.Imagina/{LICENSE.txt,imagina,imgui,sdl3,glm}`

## Flathub

The application ID is `io.github.marchbeta2087.Imagina`: the project is hosted
on GitHub, so Flathub's code-hosting ID rules (`io.github.<user>.<app>`, no
custom domain needed) apply.

Steps for a submission:

1. Copy `io.github.marchbeta2087.Imagina.yml` and `flathub.json` to the root of
   a pull request against the `new-pr` branch of
   [flathub/flathub](https://github.com/flathub/flathub).
2. Replace the `type: dir` source of the `imagina` module with the pinned
   release:

   ```yaml
   sources:
     - type: git
       url: https://github.com/MarchBeta2087/Imagina.git
       tag: v1.0.4
       commit: <commit of the v1.0.4 tag>
   ```

   Flatpak builds have no network access, so every source has to be a URL with
   a `sha256` (or a pinned git commit) that is present in the manifest.
3. `flathub.json` restricts the build to `x86_64`; the computation needs AVX2
   and the project does not build on aarch64.
4. Explain the permissions in the pull request: `--share=ipc` and
   `--socket={wayland,fallback-x11}` for the window, `--device=dri` for
   hardware OpenGL.  Files are opened and saved through the XDG desktop portal,
   so no filesystem permission is requested.
5. Run the linter from
   [Flathub's submission guide](https://docs.flathub.org/docs/for-app-authors/submission):

   ```
   flatpak run --command=flatpak-builder-lint org.flatpak.Builder manifest io.github.marchbeta2087.Imagina.yml
   ```

The screenshots referenced by the metainfo file live in `docs/flathub/` and are
served from the `v1.0.4` tag.  The metainfo file, desktop file, icons and
licences are already installed in the paths Flathub expects.
