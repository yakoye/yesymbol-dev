@echo off
setlocal EnableExtensions
cd /d "%~dp0"
set "PYTHONUTF8=1"

where python.exe >nul 2>nul
if errorlevel 1 (
  echo [ERROR] python.exe was not found.
  exit /b 1
)

echo [1/5] Ensuring pinned CLDR Chinese annotation cache...
python tools\update_cldr_zh.py ensure
if errorlevel 1 exit /b 1

echo [2/5] Building catalog.generated.json from catalog.txt...
python tools\catalog_text.py build
if errorlevel 1 exit /b 1

echo [3/5] Auditing catalog completeness and ordering...
python tools\audit_catalog.py
if errorlevel 1 exit /b 1

echo [4/5] Generating C symbol data...
attrib -R "src\symbol_data.c" >nul 2>nul
python tools\generate_bilingual_data.py
if errorlevel 1 exit /b 1

rem Uses data-source\emoji-images as a cache, so this only hits the network
rem the first time (or when new symbols are added to the scope).
echo [5/5] Embedding emoji artwork...
attrib -R "src\emoji_image_data.c" >nul 2>nul
python tools\build_emoji_images.py all
if errorlevel 1 exit /b 1

echo YeSymbol catalog and C data regenerated.
exit /b 0
