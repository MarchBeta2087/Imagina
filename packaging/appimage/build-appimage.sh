#!/usr/bin/env bash
# Builds a portable AppImage from a build tree configured with
# -DIMAGINA_INSTALL_LAYOUT=system.
#
#   packaging/appimage/build-appimage.sh <build-dir> <output.AppImage> [version]
#
# The build directory can live anywhere; the AppDir is assembled in a temporary
# directory.  APPIMAGETOOL can point at an appimagetool binary, otherwise it is
# downloaded (once) into $XDG_CACHE_HOME/imagina.
#
# Building on the oldest distribution you want to support sets the glibc
# requirement of the result; the release workflow uses Ubuntu 22.04 (glibc
# 2.34).
set -euo pipefail

build_dir=${1:?usage: build-appimage.sh <build-dir> <output.AppImage> [version]}
output=${2:?usage: build-appimage.sh <build-dir> <output.AppImage> [version]}
source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)

version=${3:-}
if [ -z "$version" ]; then
	version=$(sed -n 's/^project *( *"Imagina" *VERSION *\([0-9][0-9.]*\).*/\1/p' "$source_dir/CMakeLists.txt" | head -n 1)
fi
if [ -z "$version" ]; then
	echo "cannot determine the version from CMakeLists.txt; pass it as the third argument" >&2
	exit 2
fi

appdir=$(mktemp -d)
chmod 0755 "$appdir"
trap 'rm -rf "$appdir"' EXIT

# Same layout as the .deb/Flatpak: bin/, share/imagina, share/doc, desktop
# entry, icons.
cmake --install "$build_dir" --prefix "$appdir/usr"

# The system layout installs the generic "imagina" desktop file and icons,
# which are there for the .deb.  An AppImage identifies itself with the
# application ID, so replace them.
rm -f "$appdir/usr/share/applications/imagina.desktop"
rm -rf "$appdir/usr/share/icons/hicolor"
rm -f "$appdir/usr/share/pixmaps/imagina.png"
install -Dm644 "$source_dir/packaging/linux/io.github.marchbeta2087.Imagina.desktop" \
	"$appdir/usr/share/applications/io.github.marchbeta2087.Imagina.desktop"
install -Dm644 "$source_dir/packaging/linux/io.github.marchbeta2087.Imagina.metainfo.xml" \
	"$appdir/usr/share/metainfo/io.github.marchbeta2087.Imagina.metainfo.xml"
# A Windows checkout has CRLF line endings; desktop-entry and AppStream files
# must be LF.
sed -i 's/\r$//' \
	"$appdir/usr/share/applications/io.github.marchbeta2087.Imagina.desktop" \
	"$appdir/usr/share/metainfo/io.github.marchbeta2087.Imagina.metainfo.xml"
for size in 128 256 512; do
	install -Dm644 "$source_dir/icon/imagina_$size.png" \
		"$appdir/usr/share/icons/hicolor/${size}x${size}/apps/io.github.marchbeta2087.Imagina.png"
done
install -Dm644 "$source_dir/LICENSE.txt" \
	"$appdir/usr/share/licenses/io.github.marchbeta2087.Imagina/LICENSE.txt"

# Bundle the libraries that are not part of a base desktop system.  The GL
# stack is deliberately left to the host: its drivers are hardware and driver
# specific.
mapfile -t required_libs < <(ldd "$appdir/usr/bin/Imagina" | awk '$2 == "=>" && $3 ~ /^\// { print $1, $3 }')
mkdir -p "$appdir/usr/lib"
for entry in "${required_libs[@]}"; do
	read -r name path <<< "$entry"
	case "$name" in
		libgmp.so.*|libgmpxx.so.*|libpng16.so.*|libz.so.*|libstdc++.so.*|libgcc_s.so.*)
			cp -L "$path" "$appdir/usr/lib/$name"
			;;
	esac
done

cat > "$appdir/AppRun" <<'EOF'
#!/bin/sh
HERE=$(dirname "$(readlink -f "$0")")
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export XDG_DATA_DIRS="$HERE/usr/share${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}"
exec "$HERE/usr/bin/Imagina" "$@"
EOF
chmod +x "$appdir/AppRun"

cp "$appdir/usr/share/applications/io.github.marchbeta2087.Imagina.desktop" \
	"$appdir/io.github.marchbeta2087.Imagina.desktop"
cp "$appdir/usr/share/icons/hicolor/512x512/apps/io.github.marchbeta2087.Imagina.png" \
	"$appdir/io.github.marchbeta2087.Imagina.png"
cp "$appdir/io.github.marchbeta2087.Imagina.png" "$appdir/.DirIcon"

appimagetool=${APPIMAGETOOL:-}
if [ -z "$appimagetool" ] || [ ! -x "$appimagetool" ]; then
	cache=${XDG_CACHE_HOME:-$HOME/.cache}/imagina
	appimagetool="$cache/appimagetool-x86_64.AppImage"
	if [ ! -x "$appimagetool" ]; then
		mkdir -p "$cache"
		echo "downloading appimagetool to $appimagetool" >&2
		curl -fsSL -o "$appimagetool" \
			"https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage"
		echo "e8c1169c4af66c3184d7ee110713d4be04befe671fa358e2c1591ee840553b03  $appimagetool" | sha256sum -c -
		chmod +x "$appimagetool"
	fi
fi

mkdir -p "$(dirname -- "$output")"
ARCH=x86_64 "$appimagetool" --appimage-extract-and-run "$appdir" "$output"
echo "built $output (Imagina $version)"
