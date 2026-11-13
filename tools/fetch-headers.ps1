# Downloads the Khronos OpenGL headers the Win32 front-end needs.
#
#   powershell -ExecutionPolicy Bypass -File tools/fetch-headers.ps1 [-Dest <dir>]
#
# The Windows SDK ships gl.h (OpenGL 1.1) but no glext.h/wglext.h, and this
# repository does not carry them.  A vcpkg build gets them from the
# opengl-registry port instead; a build with the include\lib\ layout next to this
# script (see build.ps1) needs this download once.
[CmdletBinding()]
param(
        # Default: include\ next to this script, which is what build.ps1 uses.
        [string]$Dest = ""
)

$ErrorActionPreference = "Stop"

# glext.h and wglext.h live in the OpenGL registry, khrplatform.h in the EGL one.
$branch = "main"
$openglBase = "https://raw.githubusercontent.com/KhronosGroup/OpenGL-Registry/$branch/api"
$eglBase = "https://raw.githubusercontent.com/KhronosGroup/EGL-Registry/$branch/api"

$source = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (!$Dest) { $Dest = Join-Path $source "include" }

function Get-Header([string]$File, [string]$Base) {
        $target = Join-Path $Dest $File
        $directory = Split-Path -Parent $target
        New-Item -ItemType Directory -Force -Path $directory | Out-Null
        Write-Host "downloading $File"
        $temporary = "$target.tmp"
        Invoke-WebRequest -Uri "$Base/$File" -OutFile $temporary -UseBasicParsing
        Move-Item -Force -Path $temporary -Destination $target
}

Get-Header "GL/glext.h" $openglBase
Get-Header "GL/wglext.h" $openglBase
Get-Header "KHR/khrplatform.h" $eglBase

Write-Host "headers are in $Dest"
