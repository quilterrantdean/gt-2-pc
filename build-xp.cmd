@echo off
rem Build the native 32-bit Windows XP game with the active MinGW toolchain.
setlocal

gcc -dumpmachine >nul 2>nul
if errorlevel 1 (
  echo ERROR: gcc was not found. Run this from the 32-bit w64devkit shell.
  exit /b 1
)

for /f %%i in ('gcc -dumpmachine') do set "GCC_TARGET=%%i"
if not "%GCC_TARGET%"=="i686-w64-mingw32" (
  echo ERROR: expected i686-w64-mingw32, found "%GCC_TARGET%".
  echo Use the 32-bit w64devkit / MinGW toolchain.
  exit /b 1
)

if not defined GT2_JOBS set "GT2_JOBS=1"
set "BUILD_DIR=%~dp0build-xp"

cmake -S "%~dp0." -B "%BUILD_DIR%" -G "MinGW Makefiles" ^
  -DCMAKE_BUILD_TYPE=Release -DGT2_XP_CLASSIC=ON
if errorlevel 1 exit /b 1

cmake --build "%BUILD_DIR%" --target gt2game gt2xpglcheck gt2install --parallel %GT2_JOBS%
if errorlevel 1 exit /b 1

echo.
echo Built:
echo   %BUILD_DIR%\gt2game.exe
echo   %BUILD_DIR%\gt2xpglcheck.exe
echo   %BUILD_DIR%\gt2install.exe
