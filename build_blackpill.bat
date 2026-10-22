@echo off
setlocal

set "ARM_BIN=C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740\tools\bin"
set "NINJA_BIN=C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.ninja.win32_1.1.200.202606260906\tools\bin"

set "PATH=%NINJA_BIN%;%ARM_BIN%;%PATH%"
set "REPO_DIR=%~dp0"
if "%REPO_DIR:~-1%"=="\" set "REPO_DIR=%REPO_DIR:~0,-1%"
set "BUILD_DIR=%REPO_DIR%\build"

echo ============================================
echo   Building Blackpill Sensing Node Firmware
echo ============================================

cmake -G Ninja -S "%REPO_DIR%" -B "%BUILD_DIR%" -DCMAKE_TOOLCHAIN_FILE="cmake/gcc-arm-none-eabi.cmake"
if %ERRORLEVEL% neq 0 (
    echo [ERROR] CMake configuration failed!
    exit /b %ERRORLEVEL%
)

cmake --build "%BUILD_DIR%"
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Build failed!
    exit /b %ERRORLEVEL%
)

echo.
echo [SUCCESS] Production files ready in build folder:
dir "%BUILD_DIR%\Blackpill-Sensing-Node.*" /b
echo ============================================
