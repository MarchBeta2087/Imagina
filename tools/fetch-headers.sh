#!/usr/bin/env bash
# Downloads the Khronos OpenGL headers the Win32 front-end needs.
#
#   tools/fetch-headers.sh [--dest <dir>]
#
# The Windows SDK ships gl.h (OpenGL 1.1) but no glext.h/wglext.h, and this
# repository does not carry them.  A vcpkg build gets them from the
# opengl-registry port instead; a build with the include\lib\ layout next to this
# script (see build.ps1) needs this download once.
set -euo pipefail

# glext.h and wglext.h live in the OpenGL registry, khrplatform.h in the EGL one.
branch=main
opengl_base="https://raw.githubusercontent.com/KhronosGroup/OpenGL-Registry/$branch/api"
egl_base="https://raw.githubusercontent.com/KhronosGroup/EGL-Registry/$branch/api"

source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
destination="$source_dir/include"

while [ $# -gt 0 ]; do
	case "$1" in
		--dest) destination=${2:?--dest needs a value}; shift 2 ;;
		--dest=*) destination=${1#*=}; shift ;;
		-h|--help) sed -n '2,9p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "Unknown option: $1" >&2; exit 2 ;;
	esac
done

download() {
	local file=$1 base=$2
	local target="$destination/$file"
	mkdir -p "$(dirname -- "$target")"
	echo "downloading $file"
	curl -fsSL "$base/$file" -o "$target.tmp"
	mv "$target.tmp" "$target"
}

download GL/glext.h "$opengl_base"
download GL/wglext.h "$opengl_base"
download KHR/khrplatform.h "$egl_base"

echo "headers are in $destination"
