#!/usr/bin/env bash
# Builds Imagina with GCC or Clang on Linux, macOS, the BSDs and (MSYS2 /
# MinGW-w64) Windows.  build.ps1 is the equivalent for the Visual Studio
# toolchain.
#
#   ./build.sh                       # build, compiler detected
#   ./build.sh --compiler clang      # force Clang
#   ./build.sh --compiler gcc        # force GCC
#   ./build.sh --clean --test        # rebuild all + codec test
#   ./build.sh -- -DENABLE_CUSTOM_FORMULA=ON
#
# The window application is built on every platform: Windows uses the Win32
# front-end by default, everything else the SDL3 + Dear ImGui one.  --no-gui
# builds only the portable parts (the raw map codec test).
#
# Dependencies are expected next to this script, in include/ (mpir.h, gmp-impl.h,
# png.h, zlib.h, glm/, ...) and lib/ (libmpir.a libmpirxx.a libpng16.a libz.a).
# The names can be overridden, see the CMake variables ZLIB_LIBRARY,
# PNG_LIBRARY, MPIR_LIBRARY and MPIRXX_LIBRARY.
set -euo pipefail

usage() {
	cat <<'EOF'
Usage: ./build.sh [options] [-- <extra cmake arguments>]

Options:
  --compiler <auto|gcc|clang>   Compiler to use (default: auto)
  --config <Debug|Release|RelWithDebInfo|MinSizeRel>
                                Build type (default: Release)
  --build-dir <dir>             Build directory (default: build, or build/<compiler>
                                when --compiler is given)
  --generator <name>            CMake generator (default: Ninja if available)
  --frontend <win32|sdl>        Window toolkit (default: win32 on Windows, sdl elsewhere)
  --gui                         Require the window application (the default)
  --no-gui                      Skip the window application
  --test                        Also build the raw map codec test
  --clean                       Remove the build directory first
  -j, --jobs <n>                Parallel jobs (default: number of CPUs)
  -h, --help                    Show this help
EOF
}

source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

compiler=auto
compiler_explicit=0
frontend=
config=Release
build_dir=
build_dir_explicit=0
generator=
want_gui=auto
want_test=OFF
clean=0
jobs=
extra_args=()

while [ $# -gt 0 ]; do
	case "$1" in
		--compiler)      compiler=${2:?--compiler needs a value}; compiler_explicit=1; shift 2 ;;
		--compiler=*)    compiler=${1#*=}; compiler_explicit=1; shift ;;
		--config)        config=${2:?--config needs a value}; shift 2 ;;
		--config=*)      config=${1#*=}; shift ;;
		--build-dir)     build_dir=${2:?--build-dir needs a value}; build_dir_explicit=1; shift 2 ;;
		--build-dir=*)   build_dir=${1#*=}; build_dir_explicit=1; shift ;;
		--generator)     generator=${2:?--generator needs a value}; shift 2 ;;
		--generator=*)   generator=${1#*=}; shift ;;
		--frontend)      frontend=${2:?--frontend needs a value}; shift 2 ;;
		--frontend=*)    frontend=${1#*=}; shift ;;
		--gui)           want_gui=ON; shift ;;
		--no-gui)        want_gui=OFF; shift ;;
		--test)          want_test=ON; shift ;;
		--clean)         clean=1; shift ;;
		-j|--jobs)       jobs=${2:?$1 needs a value}; shift 2 ;;
		-j*)             jobs=${1#-j}; shift ;;
		-h|--help)       usage; exit 0 ;;
		--)              shift; extra_args=("$@"); break ;;
		*)               echo "Unknown option: $1 (see --help)" >&2; exit 2 ;;
	esac
done

case "$compiler" in
	auto|gcc|clang) ;;
	*) echo "Unsupported compiler '$compiler' (use auto, gcc or clang; for MSVC use build.ps1)." >&2; exit 2 ;;
esac

find_tool() { command -v "$1" 2>/dev/null || true; }

# MSYS2/Cygwin shells hand out POSIX paths (/c/...), which a native Windows
# CMake does not understand; cygpath turns them into C:/... .
to_native() {
	case "$1" in
		/*)
			if command -v cygpath >/dev/null 2>&1; then
				cygpath -m -- "$1"
			else
				printf '%s\n' "$1"
			fi
			;;
		*) printf '%s\n' "$1" ;;
	esac
}

# --- platform ---------------------------------------------------------------
case "$(uname -s)" in
	MINGW*|MSYS*|CYGWIN*)
		on_windows=1
		# Executables get a .exe suffix, dependencies are import libraries.
		;;
	Darwin|Linux|FreeBSD|OpenBSD|NetBSD|DragonFly)
		on_windows=0
		;;
	*)
		echo "Warning: untested platform '$(uname -s)', trying anyway." >&2
		on_windows=0
		;;
esac

if [ -z "$frontend" ]; then
	if [ "$on_windows" = 1 ]; then frontend=win32; else frontend=sdl; fi
fi
case "$frontend" in
	win32|sdl) ;;
	*) echo "Unsupported front-end '$frontend' (use win32 or sdl)." >&2; exit 2 ;;
esac

if [ "$frontend" = sdl ]; then
	# The SDL front-end builds everywhere.
	:
elif [ "$on_windows" = 0 ]; then
	echo "The Win32 front-end needs Windows; use --frontend sdl." >&2
	exit 2
fi

if [ "$want_gui" = auto ]; then
	want_gui=ON
fi
if [ "$want_gui" = ON ] && [ "$frontend" = win32 ] && [ "$on_windows" = 0 ]; then
	echo "The Win32 front-end uses the Win32 API, wgl and a .rc resource, so it cannot" >&2
	echo "be built on $(uname -s).  Use --frontend sdl (the default) instead." >&2
	exit 2
fi
if [ "$want_gui" = OFF ] && [ "$want_test" = OFF ]; then
	want_test=ON
fi

# --- compiler ---------------------------------------------------------------
if [ "$compiler" = auto ]; then
	# Respect a configured toolchain, then prefer whatever is installed.
	if [ -n "${CXX:-}" ]; then
		compiler=$(basename -- "$CXX")
		case "$compiler" in
			clang*) compiler=clang ;;
			g++*|c++*) compiler=gcc ;;
			*) echo "Cannot derive the compiler family from CXX='$CXX'; pass --compiler." >&2; exit 2 ;;
		esac
	elif [ -n "$(find_tool clang++)" ]; then
		compiler=clang
	elif [ -n "$(find_tool g++)" ]; then
		compiler=gcc
	else
		echo "No C++ compiler found: install clang or gcc, or set CC/CXX." >&2
		exit 2
	fi
	echo "Detected compiler: $compiler"
fi

# An explicit --compiler gets its own directory so switching compilers does
# not reuse a cache that was configured for a different one.  The plain
# default stays "build" for the documented commands and existing setups.
if [ "$build_dir_explicit" = 0 ]; then
	if [ "$compiler_explicit" = 1 ]; then
		build_dir="$source_dir/build/$compiler"
	else
		build_dir="$source_dir/build"
	fi
fi

cc=${CC:-}
cxx=${CXX:-}
if [ "$compiler" = clang ]; then
	cc=${cc:-$(find_tool clang)}
	cxx=${cxx:-$(find_tool clang++)}
	[ -n "$cxx" ] || { echo "clang++ was not found on PATH." >&2; exit 2; }
else
	cc=${cc:-$(find_tool gcc)}
	cxx=${cxx:-$(find_tool g++)}
	[ -n "$cxx" ] || { echo "g++ was not found on PATH." >&2; exit 2; }
fi
cc=$(to_native "$cc")
cxx=$(to_native "$cxx")

# --- generator and jobs -----------------------------------------------------
if [ -z "$generator" ]; then
	if [ -n "$(find_tool ninja)" ]; then
		generator=Ninja
	elif [ -n "$(find_tool make)" ]; then
		generator="Unix Makefiles"
	else
		echo "Neither Ninja nor make was found on PATH." >&2
		exit 2
	fi
fi

if [ -z "$jobs" ]; then
	jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
fi

build_dir=$(to_native "$build_dir")
source_dir=$(to_native "$source_dir")

cache_matches() {
	local cached="$1"
	[ -z "$cached" ] && return 0
	local name
	name=$(basename -- "$cached" | tr '[:upper:]' '[:lower:]')
	case "$compiler" in
		clang) case "$name" in clang++*|clang-*|clang) return 0 ;; esac ;;
		gcc)   case "$name" in g++*|gcc*|c++*) return 0 ;; esac ;;
	esac
	return 1
}

cached_compiler=""
if [ "$clean" = 0 ] && [ -f "$build_dir/CMakeCache.txt" ]; then
	cached_compiler=$(sed -n 's/^CMAKE_CXX_COMPILER:[^=]*=//p' "$build_dir/CMakeCache.txt" | head -n 1)
fi
if [ -n "$cached_compiler" ] && ! cache_matches "$cached_compiler"; then
	if [ "$build_dir_explicit" = 0 ] && [ "$compiler_explicit" = 0 ]; then
		# The old default directory belongs to another compiler.  Keep it and
		# fall back to the per-compiler directory instead of failing.
		fallback="$source_dir/build/$compiler"
		cached_fallback=""
		if [ -f "$fallback/CMakeCache.txt" ]; then
			cached_fallback=$(sed -n 's/^CMAKE_CXX_COMPILER:[^=]*=//p' "$fallback/CMakeCache.txt" | head -n 1)
		fi
		if [ -n "$cached_fallback" ] && ! cache_matches "$cached_fallback"; then
			echo "The CMake cache in '$fallback' was configured with '$cached_fallback'." >&2
			echo "Use --build-dir <dir> or remove that directory before building with $compiler." >&2
			exit 1
		fi
		echo "The CMake cache in '$build_dir' was configured with '$cached_compiler'; using '$fallback'."
		build_dir="$fallback"
	else
		echo "The CMake cache in '$build_dir' was configured with '$cached_compiler'." >&2
		echo "Use --build-dir <dir> or remove that directory before building with $compiler." >&2
		exit 1
	fi
fi

if [ "$clean" = 1 ]; then
	rm -rf -- "$build_dir"
fi

# --- configure and build ----------------------------------------------------
cmake_args=(
	-S "$source_dir"
	-B "$build_dir"
	-G "$generator"
	"-DCMAKE_BUILD_TYPE=$config"
	-DMPIR_USE_DLL=OFF
	"-DBUILD_CODEC_TEST=$want_test"
	"-DBUILD_GUI=$want_gui"
	"-DIMAGINA_FRONTEND=$frontend"
	"-DCMAKE_C_COMPILER=$cc"
	"-DCMAKE_CXX_COMPILER=$cxx"
)
if [ ${#extra_args[@]} -gt 0 ]; then
	cmake_args+=("${extra_args[@]}")
fi

cmake "${cmake_args[@]}"
cmake --build "$build_dir" --config "$config" --parallel "$jobs"

echo
for exe in Imagina RawMapCodecTest; do
	path="$build_dir/$exe"
	if [ "$on_windows" = 1 ]; then path="$path.exe"; fi
	if [ -x "$path" ]; then echo "Built $path"; fi
done
exit 0
