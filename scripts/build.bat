@echo off
rem CuteCat build script (Windows / MSVC 2022 + Qt 6)
rem usage: scripts\build.bat [Debug|Release]   (default: Release)
rem NOTE: keep this file pure ASCII - cmd.exe mis-parses UTF-8 batch files.
setlocal

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

set "ROOT=%~dp0.."
set "BUILD_DIR=%ROOT%\build"

if not defined QT_DIR set "QT_DIR=C:\Qt\6.7.2\msvc2019_64"
if not exist "%QT_DIR%\bin\Qt6Core.dll" goto :no_qt

set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if exist "%VCVARS%" goto :have_vc

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :no_vs
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" goto :no_vs

:have_vc
call "%VCVARS%" >nul
if errorlevel 1 goto :no_vs

echo [1/2] cmake configure  (Qt: %QT_DIR%)
cmake -S "%ROOT%" -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="%QT_DIR%"
if errorlevel 1 exit /b 1

echo [2/2] cmake build (%CONFIG%)
cmake --build "%BUILD_DIR%" --config %CONFIG% --parallel
if errorlevel 1 exit /b 1

echo.
echo Build OK: %BUILD_DIR%\bin\%CONFIG%\CuteCat.exe
exit /b 0

:no_qt
echo [ERROR] Qt not found: %QT_DIR%
echo Set QT_DIR to your Qt msvc directory, e.g.
echo   set QT_DIR=C:\Qt\6.7.2\msvc2019_64
exit /b 1

:no_vs
echo [ERROR] Visual Studio 2022 C++ toolchain not found.
echo Install "Desktop development with C++" workload first.
exit /b 1