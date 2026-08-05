@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "ACTION=%~1"
if "%ACTION%"=="" set "ACTION=build"

if /i "%ACTION%"=="build" goto :build
if /i "%ACTION%"=="clean" goto :clean
if /i "%ACTION%"=="data" goto :data
if /i "%ACTION%"=="cldr" goto :cldr
if /i "%ACTION%"=="all" goto :all
if /i "%ACTION%"=="run" goto :run
if /i "%ACTION%"=="web" goto :web
if /i "%ACTION%"=="web-serve" goto :web_serve
if /i "%ACTION%"=="web-clean" goto :web_clean
if /i "%ACTION%"=="help" goto :help
if /i "%ACTION%"=="-h" goto :help
if /i "%ACTION%"=="--help" goto :help

echo [ERROR] Unknown command: %ACTION%
echo.
goto :help_error

:check_cmake
where cmake.exe >nul 2>nul
if errorlevel 1 (
  echo [ERROR] cmake.exe was not found.
  echo Install CMake or enable the CMake component in Visual Studio.
  exit /b 1
)
exit /b 0

:configure
call :check_cmake
if errorlevel 1 exit /b 1

if exist "build\CMakeCache.txt" (
  echo [YeSymbol] Reusing the existing CMake generator and platform...
  cmake -S . -B build
  if not errorlevel 1 exit /b 0

  echo.
  echo [YeSymbol] Existing CMake cache is incompatible.
  echo [YeSymbol] Recreating build as Visual Studio x64...
  rmdir /s /q "build" 2>nul
)

cmake -S . -B build -A x64
if errorlevel 1 exit /b 1
exit /b 0

:build
call :configure
if errorlevel 1 goto :failed

echo [YeSymbol] Building Release...
cmake --build build --config Release --parallel
if errorlevel 1 goto :failed

if not exist "dist\yesymbol.exe" (
  echo [ERROR] Build completed, but dist\yesymbol.exe was not generated.
  exit /b 1
)

echo.
echo [YeSymbol] Build completed successfully.
echo [YeSymbol] Output: %CD%\dist\yesymbol.exe
exit /b 0

:clean
echo [YeSymbol] Cleaning build and dist...
if exist "build" rmdir /s /q "build"
if exist "build" (
  echo [ERROR] Could not remove build. Close programs using files in that directory.
  exit /b 1
)
if exist "dist" rmdir /s /q "dist"
if exist "dist" (
  echo [ERROR] Could not remove dist. Close yesymbol.exe or use: build.bat run
  exit /b 1
)
echo [YeSymbol] Clean completed.
exit /b 0

:data
echo [YeSymbol] Regenerating catalog data...
rem Fully qualified on purpose: when NoDefaultCurrentDirectoryInExePath=1 is
rem set in the environment (a common security/policy setting), cmd.exe does
rem not search the current directory for commands, so a bare
rem "call regenerate-data.cmd" fails with "is not recognized as an internal
rem or external command" even though the file sits right next to this script.
call "%~dp0regenerate-data.cmd"
if errorlevel 1 (
  echo [ERROR] Catalog regeneration failed. Build/run has been stopped.
  exit /b 1
)
echo [YeSymbol] Catalog data regenerated.
exit /b 0

:cldr
echo [YeSymbol] Refreshing pinned CLDR Chinese names...
set "PYTHONUTF8=1"
python tools\update_cldr_zh.py refresh
if errorlevel 1 (
  echo [ERROR] CLDR refresh failed. Existing local data was not replaced.
  exit /b 1
)
python tools\catalog_text.py apply-cldr
if errorlevel 1 exit /b 1
call "%~f0" data
exit /b %errorlevel%

:all
call "%~f0" data
if errorlevel 1 exit /b 1
call "%~f0" clean
if errorlevel 1 exit /b 1
call "%~f0" build
exit /b %errorlevel%

:run
echo [YeSymbol] Stopping existing yesymbol.exe...
taskkill /F /IM yesymbol.exe >nul 2>nul

call "%~f0" build
if errorlevel 1 exit /b 1

if not exist "dist\yesymbol.exe" (
  echo [ERROR] dist\yesymbol.exe was not found.
  exit /b 1
)

echo [YeSymbol] Starting the new build...
start "" /D "%CD%\dist" "%CD%\dist\yesymbol.exe"
if errorlevel 1 (
  echo [ERROR] Failed to start dist\yesymbol.exe.
  exit /b 1
)
exit /b 0

rem Fully qualified for the same reason as regenerate-data.cmd above.
:web
call "%~dp0build-web.bat" build
exit /b %errorlevel%

:web_serve
call "%~dp0build-web.bat" serve
exit /b %errorlevel%

:web_clean
call "%~dp0build-web.bat" clean
exit /b %errorlevel%

:help
echo.
echo Usage:
echo   build.bat         Incremental Release build
echo   build.bat build   Incremental Release build
echo   build.bat clean   Delete build and dist only
echo   build.bat data    Regenerate JSON and C symbol data
echo   build.bat cldr    Refresh CLDR Chinese names and regenerate data
echo   build.bat all     Regenerate data, clean and build
echo   build.bat run     Kill, incrementally build and run
echo   build.bat web     Regenerate shared data and build dist-web
echo   build.bat web-serve Build dist-web and serve on http://127.0.0.1:8080/
echo   build.bat web-clean Delete dist-web only
echo   build.bat help    Show this help
echo.
exit /b 0

:help_error
call :help
exit /b 2

:failed
echo.
echo [ERROR] Build failed.
exit /b 1
