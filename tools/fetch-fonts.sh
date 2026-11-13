#!/usr/bin/env bash
# Downloads the fonts the SDL front-end uses for CJK text.
#
#   ./tools/fetch-fonts.sh [--force] [--dest <dir>]
#
# The font is published as a release asset because this repository is a fork and
# GitHub does not accept Git LFS uploads for forks (see fonts/README.md).
set -euo pipefail

release=deps-fonts-v1
font=SarasaMonoSC-Regular.ttf
repository=MarchBeta2087/Imagina

source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
destination="$source_dir/fonts"
force=0

while [ $# -gt 0 ]; do
	case "$1" in
		--force) force=1; shift ;;
		--dest) destination=${2:?--dest needs a value}; shift 2 ;;
		--dest=*) destination=${1#*=}; shift ;;
		-h|--help) sed -n '2,10p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "Unknown option: $1" >&2; exit 2 ;;
	esac
done

target="$destination/$font"
if [ -e "$target" ] && [ "$force" != 1 ]; then
	echo "$target already exists (use --force to download it again)."
	exit 0
fi

mkdir -p -- "$destination"
url="https://github.com/$repository/releases/download/$release/$font"
echo "Downloading $url"

download() {
	if command -v curl >/dev/null 2>&1; then
		curl -fL --retry 2 --connect-timeout 30 -o "$target" "$url"
	elif command -v wget >/dev/null 2>&1; then
		wget -O "$target" "$url"
	fi
}

download || true
size=$([ -f "$target" ] && wc -c < "$target" || echo 0)
if [ "$size" -lt 1048576 ] && command -v gh >/dev/null 2>&1; then
	# Some networks cannot reach the host GitHub redirects release assets to;
	# the CLI downloads through the API instead.
	echo "The direct download did not work, trying the GitHub CLI..."
	rm -f -- "$target"
	gh release download "$release" --repo "$repository" --pattern "$font" --dir "$destination" --clobber
fi

size=$([ -f "$target" ] && wc -c < "$target" || echo 0)
if [ "$size" -lt 1048576 ]; then
	echo "$target is only $size bytes; the download did not work (see fonts/README.md)." >&2
	exit 1
fi

echo "Saved $target ($((size / 1048576)) MB)"
echo "Sarasa Gothic is licensed under the SIL Open Font License 1.1, see fonts/LICENSE."
