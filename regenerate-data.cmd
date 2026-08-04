@echo off
setlocal EnableExtensions
cd /d "%~dp0"
set "PYTHONUTF8=1"

where python.exe >nul 2>nul
if errorlevel 1 (
  echo [ERROR] python.exe was not found.
  exit /b 1
)

echo [1/4] Ensuring pinned CLDR Chinese annotation cache...
python tools\update_cldr_zh.py ensure
if errorlevel 1 exit /b 1

echo [2/4] Building catalog.generated.json from catalog.txt...
python tools\catalog_text.py build
if errorlevel 1 exit /b 1

echo [3/4] Auditing catalog completeness and ordering...
python tools\audit_catalog.py
if errorlevel 1 exit /b 1

echo [4/4] Generating C symbol data...
attrib -R "src\symbol_data.c" >nul 2>nul
python tools\generate_bilingual_data.py
if errorlevel 1 exit /b 1

echo YeSymbol catalog and C data regenerated.
exit /b 0
