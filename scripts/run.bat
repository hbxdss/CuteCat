@echo off
rem Run the built CuteCat desktop pet.
setlocal
set "ROOT=%~dp0.."
set "EXE=%ROOT%\build\bin\Release\CuteCat.exe"
if not exist "%EXE%" (
    echo [ERROR] Not built yet. Run scripts\build.bat first.
    exit /b 1
)
start "" "%EXE%"