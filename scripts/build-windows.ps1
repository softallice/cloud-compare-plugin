# SPDX-License-Identifier: GPL-3.0-or-later
# Builds CloudCompare + qBuildingDims on Windows (Visual Studio + Qt6).
#
# Prereqs: Visual Studio 2019/2022 (C++), CMake, Git, Qt 6.
# Usage:
#   pwsh scripts/build-windows.ps1 -QtDir "C:\Qt\6.7.2\msvc2019_64" `
#        -CcSrc "C:\src\CloudCompare" [-CcRef master]
param(
    [Parameter(Mandatory=$true)][string]$QtDir,
    [string]$CcSrc = "$HOME\CloudCompare",
    [string]$CcRef = "master",
    [string]$Generator = "Visual Studio 18 2026",
    [string]$Arch = "x64"
)
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path "$PSScriptRoot\..").Path

# CloudCompare only supports MSVC on Windows. A MinGW Qt (…\mingw_XX) makes CMake
# fall back to the NMake/MinGW toolchain and fail ("CMAKE_C_COMPILER not set").
if ($QtDir -match "mingw") {
    throw "QtDir points to a MinGW build ($QtDir). CloudCompare needs an MSVC Qt, " +
          "e.g. C:\Qt\6.11.1\msvc2022_64. Install the 'MSVC 2022 64-bit' component " +
          "via the Qt Maintenance Tool and pass that path."
}

if (-not (Test-Path "$CcSrc\.git")) {
    Write-Host "== Cloning CloudCompare ($CcRef) =="
    git clone --recursive https://github.com/CloudCompare/CloudCompare.git $CcSrc
    git -C $CcSrc checkout $CcRef
    git -C $CcSrc submodule update --init --recursive
} else {
    git -C $CcSrc submodule update --init --recursive
}

# Integrate plugin (copy + register in CMake).
$dest = "$CcSrc\plugins\core\Standard\qBuildingDims"
if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
Copy-Item -Recurse "$Root\plugin" $dest
$cml = "$CcSrc\plugins\core\Standard\CMakeLists.txt"
if (-not (Select-String -Path $cml -Pattern "qBuildingDims" -Quiet)) {
    "add_subdirectory( qBuildingDims )`n" + (Get-Content $cml -Raw) | Set-Content $cml
}

Write-Host "== Configuring (generator: $Generator $Arch) =="
# Force the Visual Studio generator + architecture so CMake never falls back to
# NMake/MinGW when run outside a VS Developer prompt.
cmake -S $CcSrc -B "$CcSrc\build" -G $Generator -A $Arch `
    -DCMAKE_PREFIX_PATH=$QtDir -DPLUGIN_STANDARD_QBUILDINGDIMS=ON

Write-Host "== Building =="
cmake --build "$CcSrc\build" --config Release

Write-Host "== BUILD OK. See $CcSrc\build =="
