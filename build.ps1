# Builds Imagina.  Works with the Visual Studio toolchain (MSVC), GCC
# (MinGW-w64 on Windows) and Clang; build.sh does the same on macOS and Linux.
#
#   powershell -ExecutionPolicy Bypass -File build.ps1                  # build, compiler detected
#   powershell -ExecutionPolicy Bypass -File build.ps1 -Compiler gcc    # force GCC
#   powershell -ExecutionPolicy Bypass -File build.ps1 -Compiler clang  # force Clang
#   powershell -ExecutionPolicy Bypass -File build.ps1 -Clean -Test     # rebuild all + codec test
#   powershell -ExecutionPolicy Bypass -File build.ps1 -NoGui -Test     # portable parts only
#
# -Compiler auto (the default) picks MSVC when Visual Studio is installed,
# else Clang, else GCC.  -Compiler clang-cl uses Clang's cl.exe-compatible
# driver inside the Visual Studio environment.  Additional arguments after the
# named ones are passed to CMake, e.g. -DENABLE_CUSTOM_FORMULA=ON.
#
# Dependencies (not part of the repository) are expected next to this script,
# where the CMake variables INCLUDE_PATH and LIBRARY_PATH point by default.
# MSVC needs import libraries (*.lib), GCC and Clang need libraries in their
# own format, e.g. libmpir.a libmpirxx.a libpng16.a libz.a in lib\ (or a
# -DZLIB_LIBRARY=... override on the command line):
#
#   include\  mpir.h mpirxx.h gmp.h gmpxx.h gmp-impl.h config.h gmp-mparam.h
#             longlong.h png.h pngconf.h pnglibconf.h zlib.h zconf.h
#             glext.h wglext.h KHR\khrplatform.h glm\
#   lib\      mpir mpirxx libpng16 zlibwapi   (MSVC: .lib, GCC/Clang: lib*.a)
#
# With MSVC everything is built with the static C runtime (/MT) and MPIR is
# linked statically (MPIR_USE_DLL=OFF), so the executable needs no extra DLLs.
#
# MPIR has to be built from source, its releases do not install gmp-impl.h which
# this project includes.  In the MPIR source tree run
# "build.vc\gen_config_h.bat cdata\" and "build.vc\gen_mpir_h.bat x64", then build
# build.vc14\lib_mpir_gc\lib_mpir_gc.vcxproj and
# build.vc14\lib_mpir_cxx\lib_mpir_cxx.vcxproj for Release|x64.  zlib and libpng
# only need their own sources compiled into static libraries.  The sources this
# build used are kept in deps\src\ (git-ignored).  For MinGW-w64 the same sources
# can be configured with --host=x86_64-w64-mingw32 and built with `make install`
# into include\ and lib\.
#
# Note for non English Visual Studio installations: cl.exe prints its include
# markers in the system language, and ninja only recognises the markers of the
# languages it knows.  With a Chinese Visual Studio no header dependencies are
# recorded, so *changing a header needs a clean build* (use -Clean).
[CmdletBinding()]
param(
	[ValidateSet("auto", "msvc", "clang", "clang-cl", "gcc")]
	[string]$Compiler = "auto",
	[ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
	[string]$Configuration = "Release",
	[switch]$Clean,
	[switch]$Test,
	# Build only the parts that do not need the Windows API (the codec test).
	[switch]$NoGui,
	# Empty: use ninja when it is on PATH, else the default make generator.
	[string]$Generator = "",
	# Empty: locate Visual Studio with vswhere.
	[string]$VisualStudio = "",
	[string]$BuildDir = "",
	# Anything after the named parameters is passed to CMake verbatim, e.g.
	# -DENABLE_CUSTOM_FORMULA=ON -DZLIB_LIBRARY=z
	[Parameter(ValueFromRemainingArguments = $true)]
	[string[]]$CMakeArgs = @()
)

$ErrorActionPreference = "Stop"

$source = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = if ($BuildDir) { $BuildDir } else { Join-Path $source "build" }
if (![System.IO.Path]::IsPathRooted($build)) { $build = Join-Path (Get-Location).Path $build }
function Find-Tool([string]$Name) {
	$command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
	if ($command) { return $command.Source }
	return $null
}

function Find-VisualStudio {
	if ($VisualStudio) {
		$vcvars = Join-Path $VisualStudio "VC\Auxiliary\Build\vcvars64.bat"
		if (!(Test-Path $vcvars)) {
			throw "vcvars64.bat not found below '$VisualStudio' (use -VisualStudio)."
		}
		return $VisualStudio
	}

	$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
	if (Test-Path $vswhere) {
		$path = & $vswhere -latest -products * `
			-requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
			-property installationPath 2>$null
		if ($LASTEXITCODE -eq 0 -and $path) { return ($path | Select-Object -First 1).Trim() }
	}

	# Fall back to the usual installation locations.
	$roots = @("$env:ProgramFiles\Microsoft Visual Studio", "${env:ProgramFiles(x86)}\Microsoft Visual Studio")
	foreach ($root in $roots) {
		if (!(Test-Path $root)) { continue }
		$found = Get-ChildItem -Path $root -Directory -ErrorAction SilentlyContinue |
			ForEach-Object { Get-ChildItem -Path $_.FullName -Directory -ErrorAction SilentlyContinue } |
			Where-Object { Test-Path (Join-Path $_.FullName "VC\Auxiliary\Build\vcvars64.bat") } |
			Sort-Object Name -Descending | Select-Object -First 1
		if ($found) { return $found.FullName }
	}
	return $null
}

# --- pick the compiler ------------------------------------------------------
$visualStudioPath = $null
if ($Compiler -eq "auto") {
	$visualStudioPath = Find-VisualStudio
	if ($visualStudioPath) {
		$Compiler = "msvc"
	} elseif (Find-Tool "clang++") {
		$Compiler = "clang"
	} elseif (Find-Tool "g++") {
		$Compiler = "gcc"
	} else {
		throw "No compiler found: install Visual Studio (MSVC), LLVM (Clang) or MinGW-w64 (GCC), or pass -Compiler."
	}
	Write-Host "Detected compiler: $Compiler"
}

$cCompiler = $null
$cxxCompiler = $null
switch ($Compiler) {
	"msvc" {
		if (!$visualStudioPath) { $visualStudioPath = Find-VisualStudio }
		if (!$visualStudioPath) { throw "Visual Studio with the C++ workload was not found (use -VisualStudio)." }
		$vcvars = Join-Path $visualStudioPath "VC\Auxiliary\Build\vcvars64.bat"
	}
	"gcc" {
		$cCompiler = Find-Tool "gcc"
		$cxxCompiler = Find-Tool "g++"
		if (!$cxxCompiler) { throw "g++ was not found on PATH (install MinGW-w64 and put its bin directory on PATH)." }
	}
	"clang" {
		$cCompiler = Find-Tool "clang"
		$cxxCompiler = Find-Tool "clang++"
		if (!$cxxCompiler) { throw "clang++ was not found on PATH (install LLVM and put its bin directory on PATH)." }
	}
	"clang-cl" {
		# clang-cl speaks the cl.exe command line, so it needs the MSVC
		# environment for the Windows SDK headers and libraries.
		if (!$visualStudioPath) { $visualStudioPath = Find-VisualStudio }
		if (!$visualStudioPath) { throw "clang-cl needs the Visual Studio environment (Windows SDK); Visual Studio was not found." }
		$vcvars = Join-Path $visualStudioPath "VC\Auxiliary\Build\vcvars64.bat"
		$cCompiler = Find-Tool "clang-cl"
		$cxxCompiler = $cCompiler
		if (!$cxxCompiler) { throw "clang-cl was not found on PATH (install LLVM)." }
	}
}

# --- generator --------------------------------------------------------------
$ninja = Find-Tool "ninja"
if ($Generator) {
	# keep the caller's choice
} elseif ($ninja) {
	$Generator = "Ninja"
} elseif ($Compiler -eq "msvc" -or $Compiler -eq "clang-cl") {
	throw "Ninja was not found on PATH (it ships with Visual Studio: run this script from a VS developer prompt, or install Ninja)."
} elseif (Find-Tool "mingw32-make") {
	$Generator = "MinGW Makefiles"
} elseif (Find-Tool "make") {
	$Generator = "Unix Makefiles"
} else {
	throw "Neither Ninja nor make was found on PATH."
}

if (Get-Process -Name Imagina -ErrorAction SilentlyContinue) {
	throw "Imagina.exe is running; close it before building (the linker cannot replace it)."
}

# --- configure and build ----------------------------------------------------
$configureArgs = @(
	"-S", $source,
	"-B", $build,
	"-G", $Generator,
	"-DCMAKE_BUILD_TYPE=$Configuration",
	"-DMPIR_USE_DLL=OFF",
	"-DBUILD_CODEC_TEST=$(if ($Test) { 'ON' } else { 'OFF' })",
	"-DBUILD_GUI=$(if ($NoGui) { 'OFF' } else { 'ON' })"
)

if ($cCompiler) { $configureArgs += "-DCMAKE_C_COMPILER=$cCompiler" }
if ($cxxCompiler) { $configureArgs += "-DCMAKE_CXX_COMPILER=$cxxCompiler" }
if ($Compiler -eq "msvc" -or $Compiler -eq "clang-cl") {
	# mpir is built as a static library with the static C runtime, so the
	# application has to use it as well (see MPIR_USE_DLL=OFF above).
	$configureArgs += "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"
}
if ($CMakeArgs.Count -gt 0) { $configureArgs += $CMakeArgs }

$buildArgs = @("--build", $build, "--config", $Configuration, "--parallel")
if ($Clean) { $buildArgs += "--clean-first" }

function Format-CmdArgument([string]$Value) {
	if ($Value -match '[\s&|<>^"]') {
		return '"' + ($Value -replace '"', '""' -replace '%', '%%') + '"'
	}
	return $Value -replace '%', '%%'
}

if ($vcvars) {
	# The MSVC environment only exists inside a cmd shell, so configure and
	# build run from a temporary batch file that loads it first.  A batch file
	# keeps the quoting intact, which a single cmd.exe command line would not.
	$batch = Join-Path ([System.IO.Path]::GetTempPath()) ("imagina-build-" + [System.Guid]::NewGuid().ToString("N") + ".cmd")
	$configureLine = "cmake " + (($configureArgs | ForEach-Object { Format-CmdArgument $_ }) -join " ")
	$buildLine = "cmake " + (($buildArgs | ForEach-Object { Format-CmdArgument $_ }) -join " ")
	$lines = @(
		"@echo off",
		"call `"$vcvars`" >nul 2>&1",
		"if errorlevel 1 exit /b 1",
		$configureLine,
		"if errorlevel 1 exit /b 1",
		$buildLine
	)
	Set-Content -Path $batch -Value $lines -Encoding ASCII
	try {
		cmd /c "`"$batch`""
		$exitCode = $LASTEXITCODE
	} finally {
		Remove-Item -Path $batch -Force -ErrorAction SilentlyContinue
	}
} else {
	& cmake @configureArgs
	$exitCode = $LASTEXITCODE
	if ($exitCode -eq 0) {
		& cmake @buildArgs
		$exitCode = $LASTEXITCODE
	}
}
if ($exitCode -ne 0) { throw "Build failed with exit code $exitCode." }

Write-Host ""
$executables = @()
if (!$NoGui) { $executables += "Imagina.exe" }
if ($Test) { $executables += "RawMapCodecTest.exe" }
foreach ($executable in $executables) {
	$path = Join-Path $build $executable
	if (Test-Path $path) { Write-Host "Built $path" }
}
