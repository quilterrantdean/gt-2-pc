@echo off
rem Configure the isolated 32-bit Windows XP profile with the active MinGW toolchain.
setlocal

where gcc >nul 2>nul
if errorlevel 1 (
  echo ERROR: gcc was not found. Run this from the w64devkit shell.
  exit /b 1
)

for /f %%i in ('gcc -dumpmachine') do set "GCC_TARGET=%%i"
if not "%GCC_TARGET%"=="i686-w64-mingw32" (
  echo ERROR: expected i686-w64-mingw32, found "%GCC_TARGET%".
  echo Use the 32-bit w64devkit / MinGW toolchain.
  exit /b 1
)

set "BUILD_DIR=%~dp0build-xp"
cmake -S "%~dp0." -B "%BUILD_DIR%" -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DGT2_XP_CLASSIC=ON
if errorlevel 1 exit /b 1

cmake --build "%BUILD_DIR%" --target gt2xpglcheck
if errorlevel 1 exit /b 1

echo.
echo Built: %BUILD_DIR%\gt2xpglcheck.exe
