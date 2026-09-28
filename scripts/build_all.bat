@echo off
REM Master Windows build: C++20 engine + HLSL stage + citygen + full sandbox.
setlocal
cd /d "%~dp0\.."
if not exist build mkdir build
if not exist output\city mkdir output\city

echo ==^> Leonida Engine MICRO-PHASE 10 master build

where cmake >nul 2>nul
if %ERRORLEVEL%==0 (
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release
    set SANDBOX=build\Release\leonida_full_sandbox.exe
    if not exist %SANDBOX% set SANDBOX=build\leonida_full_sandbox.exe
) else (
    echo cmake not found
    exit /b 1
)

set PY=python
if exist tools\.venv\Scripts\python.exe set PY=tools\.venv\Scripts\python.exe
echo ==^> citygen 2 km
pushd tools\citygen
%PY% master_generate.py --size-km 2 --seed 42 --coast west --out ..\..\output\city
popd

echo ==^> full integration sandbox
%SANDBOX%
echo ==^> MASTER BUILD COMPLETE
endlocal
