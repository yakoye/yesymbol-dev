@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "ACTION=%~1"
if "%ACTION%"=="" set "ACTION=build"

if /i "%ACTION%"=="build" goto :build
if /i "%ACTION%"=="serve" goto :serve
if /i "%ACTION%"=="clean" goto :clean

echo [ERROR] Unknown command: %ACTION%
echo Usage: build-web.bat [build^|serve^|clean]
exit /b 2

:build
rem Fully qualified: with NoDefaultCurrentDirectoryInExePath=1 set, cmd.exe
rem will not find a sibling script by bare name.
call "%~dp0regenerate-data.cmd"
if errorlevel 1 exit /b 1
set "PYTHONUTF8=1"
python tools\build_web.py
if errorlevel 1 exit /b 1
echo [YeSymbol Web] Publish directory: %CD%\dist-web
exit /b 0

:serve
call "%~f0" build
if errorlevel 1 exit /b 1
echo [YeSymbol Web] Starting http://127.0.0.1:8080/
start "" "http://127.0.0.1:8080/"
python -m http.server 8080 --bind 127.0.0.1 --directory dist-web
exit /b %errorlevel%

:clean
if exist "dist-web" rmdir /s /q "dist-web"
echo [YeSymbol Web] Clean completed.
exit /b 0
