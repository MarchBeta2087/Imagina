# Downloads the fonts the SDL front-end uses for CJK text.
#
#   powershell -ExecutionPolicy Bypass -File tools/fetch-fonts.ps1
#
# The font is published as a release asset because this repository is a fork and
# GitHub does not accept Git LFS uploads for forks (see fonts/README.md).
param(
	[ValidateSet("deps-fonts-v1")]
	[string]$Release = "deps-fonts-v1",
	[string]$Destination = "",
	[switch]$Force
)

$ErrorActionPreference = "Stop"

$source = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (!$Destination) { $Destination = Join-Path $source "fonts" }
$repository = "MarchBeta2087/Imagina"
$font = "SarasaMonoSC-Regular.ttf"

New-Item -ItemType Directory -Force -Path $Destination | Out-Null
$target = Join-Path $Destination $font

if ((Test-Path $target) -and !$Force) {
	Write-Host "$target already exists (use -Force to download it again)."
	return
}

$url = "https://github.com/$repository/releases/download/$Release/$font"
Write-Host "Downloading $url"
try {
	Invoke-WebRequest -Uri $url -OutFile $target -UseBasicParsing -MaximumRedirection 10
} catch {
	Write-Host "The direct download failed: $($_.Exception.Message)"
}

$size = if (Test-Path $target) { (Get-Item $target).Length } else { 0 }
if ($size -lt 1MB -and (Get-Command gh -ErrorAction SilentlyContinue)) {
	# Some networks cannot reach the host GitHub redirects release assets to;
	# the CLI downloads through the API instead.
	Write-Host "Trying the GitHub CLI..."
	Remove-Item -Path $target -Force -ErrorAction SilentlyContinue
	& gh release download $Release --repo $repository --pattern $font --dir $Destination --clobber
	$size = if (Test-Path $target) { (Get-Item $target).Length } else { 0 }
}
if ($size -lt 1MB) {
	throw "$target is only $size bytes; the download did not work (a release asset needs to be fetched with the full URL, see fonts/README.md)."
}

Write-Host "Saved $target ($([math]::Round($size / 1MB, 1)) MB)"
Write-Host "Sarasa Gothic is licensed under the SIL Open Font License 1.1, see fonts/LICENSE."
