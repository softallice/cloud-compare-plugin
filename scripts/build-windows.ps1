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
    [string]$CcRef = "master"
)
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path "$PSScriptRoot\..").Path

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

Write-Host "== Configuring =="
cmake -S $CcSrc -B "$CcSrc\build" -DCMAKE_PREFIX_PATH=$QtDir -DPLUGIN_STANDARD_QBUILDINGDIMS=ON

Write-Host "== Building =="
cmake --build "$CcSrc\build" --config Release

Write-Host "== BUILD OK. See $CcSrc\build =="
