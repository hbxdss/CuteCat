@echo off
rem Package a portable build into dist\CuteCat (exe + Qt runtime via windeployqt).
setlocal

set "ROOT=%~dp0.."
if not defined QT_DIR set "QT_DIR=C:\Qt\6.7.2\msvc2019_64"
set "EXE=%ROOT%\build\bin\Release\CuteCat.exe"
set "DIST=%ROOT%\dist\CuteCat"

if not exist "%EXE%" (
    echo [ERROR] %EXE% not found. Run scripts\build.bat first.
    exit /b 1
)

if exist "%DIST%" rmdir /s /q "%DIST%"
mkdir "%DIST%"
copy /y "%EXE%" "%DIST%\" >nul

"%QT_DIR%\bin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw "%DIST%\CuteCat.exe"
if errorlevel 1 (
    echo [ERROR] windeployqt failed.
    exit /b 1
)

echo.
echo Portable build: %DIST%
echo Double-click dist\CuteCat\CuteCat.exe - no Qt installation required.
exit /b 0