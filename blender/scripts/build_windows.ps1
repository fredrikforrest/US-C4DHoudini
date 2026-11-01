<#
.SYNOPSIS
  Build the Blender fork on Windows with upstream's prebuilt libraries.

.DESCRIPTION
  Clones Blender (projects.blender.org), creates fork/main from the pinned tag,
  applies the patches from ../patches, runs `make.bat update` and
  `make.bat <Target>`, and optionally zips the portable result.
  Requirements: Git + Git LFS, CMake on PATH, Visual Studio 2022 with the
  "Desktop development with C++" workload. See docs/04-windows-build.md.

.EXAMPLE
  .\build_windows.ps1 -Workspace C:\dev -Target full -Package
#>
[CmdletBinding()]
param(
  [string]$Workspace = "C:\dev",
  [ValidateSet("full", "release", "lite", "headless", "developer")]
  [string]$Target = "full",
  [string]$Tag = "v5.2.3",
  [string]$ForkBranch = "fork/main",
  [switch]$Ninja,
  [switch]$Package,
  [switch]$SkipUpdate
)
$ErrorActionPreference = "Stop"
$kit = Split-Path -Parent $PSScriptRoot          # .../blender (this kit)
$patches = Join-Path $kit "patches"
$src = Join-Path $Workspace "blender"

function Require-Tool($name, $hint) {
  if (-not (Get-Command $name -ErrorAction SilentlyContinue)) { throw "$name not found on PATH. $hint" }
}
Require-Tool git "Install Git for Windows."
Require-Tool cmake "Install CMake and tick 'Add to PATH'."
if ((git lfs version 2>$null) -eq $null) { throw "Git LFS not installed. Run: git lfs install" }
if ($Workspace -match " ") { throw "Workspace path must not contain spaces (make.bat refuses them)." }

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "Visual Studio installer not found; install Visual Studio 2022 with the C++ workload." }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationName
if (-not $vs) { throw "No Visual Studio with 'Desktop development with C++' found." }
Write-Host "Using $vs"

New-Item -ItemType Directory -Force -Path $Workspace | Out-Null
if (-not (Test-Path (Join-Path $src ".git"))) {
  Write-Host "Cloning Blender (this includes Git LFS files)..."
  git clone https://projects.blender.org/blender/blender.git $src
}
Push-Location $src
try {
  git fetch --tags origin
  $branches = git branch --list $ForkBranch
  if (-not $branches) {
    Write-Host "Creating $ForkBranch from $Tag"
    git checkout -b $ForkBranch $Tag
  } else {
    git checkout $ForkBranch
  }

  # Apply kit patches that are not in the branch yet (matched by subject line).
  $log = git log --format=%s "$Tag..HEAD"
  Get-ChildItem $patches -Filter *.patch | Sort-Object Name | ForEach-Object {
    $subject = (Select-String -Path $_.FullName -Pattern '^Subject: \[PATCH[^\]]*\] (.*)$' | Select-Object -First 1).Matches[0].Groups[1].Value
    if ($log -contains $subject) { Write-Host "already applied: $subject" }
    else { Write-Host "applying: $subject"; git am $_.FullName }
  }

  if (-not $SkipUpdate) {
    Write-Host "Fetching prebuilt libraries (lib\windows_x64, several GB)..."
    cmd /c "make.bat update"
    if ($LASTEXITCODE) { throw "make.bat update failed" }
  }

  $args = @()
  if ($Ninja) { $args += "ninja" }
  $args += $Target
  Write-Host "Building: make.bat $($args -join ' ')"
  cmd /c "make.bat $($args -join ' ')"
  if ($LASTEXITCODE) { throw "make.bat $Target failed" }
}
finally { Pop-Location }

$build = Get-ChildItem $Workspace -Directory -Filter "build_windows_*" | Sort-Object LastWriteTime | Select-Object -Last 1
$bin = Join-Path $build.FullName "bin\Release"
$exe = Join-Path $bin "blender.exe"
if (-not (Test-Path $exe)) { throw "Build finished but $exe is missing" }
Write-Host "`nBuilt: $exe"
& $exe --version | Select-Object -First 1

if ($Package) {
  $hash = (git -C $src rev-parse --short HEAD)
  $zip = Join-Path $Workspace "blender-fork-$Tag-$hash-windows-x64.zip"
  if (Test-Path $zip) { Remove-Item $zip }
  Write-Host "Packaging portable zip: $zip"
  Compress-Archive -Path (Join-Path $bin "*") -DestinationPath $zip -CompressionLevel Optimal
  Write-Host "Done: $zip"
}
