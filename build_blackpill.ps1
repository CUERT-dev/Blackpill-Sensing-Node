# build_blackpill.ps1 - Automatic build script for Blackpill Sensing Node
$ErrorActionPreference = "Stop"

$armBin = "C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740\tools\bin"
$ninjaBin = "C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.ninja.win32_1.1.200.202606260906\tools\bin"

if (!(Test-Path $armBin)) {
    Write-Warning "Default ARM GCC path not found, searching in C:\ST..."
    $found = Get-ChildItem -Path "C:\ST" -Filter "arm-none-eabi-gcc.exe" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) { $armBin = $found.DirectoryName } else { throw "arm-none-eabi-gcc not found on system!" }
}

if (!(Test-Path $ninjaBin)) {
    $found = Get-ChildItem -Path "C:\ST" -Filter "ninja.exe" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) { $ninjaBin = $found.DirectoryName }
}

$env:PATH = "$ninjaBin;$armBin;$env:PATH"
$repoDir = $PSScriptRoot
$buildDir = Join-Path $repoDir "build"

Write-Host "Configuring CMake for Blackpill (STM32F401)..." -ForegroundColor Cyan
cmake -G Ninja -S $repoDir -B $buildDir -DCMAKE_TOOLCHAIN_FILE="cmake/gcc-arm-none-eabi.cmake"

Write-Host "Building firmware..." -ForegroundColor Cyan
cmake --build $buildDir

Write-Host "`nBuild Complete! Production binaries ready in build/:" -ForegroundColor Green
Get-ChildItem -Path $buildDir -Filter "Blackpill-Sensing-Node.*" | Where-Object { $_.Extension -in ".hex", ".bin", ".elf" } | Select-Object Name, Length, LastWriteTime | Format-Table -AutoSize
