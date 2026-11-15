@echo off
rem Build HWFStrike.dll with the MSVC x64 toolchain.
setlocal
cd /d "%~dp0"

set "VCVARS="
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

if not defined VCVARS (
    echo Could not find vcvars64.bat.
    echo Install Visual Studio or Build Tools with the Desktop development with C++ workload.
    exit /b 1
)

call "%VCVARS%" >nul || exit /b 1
if not exist build mkdir build
if not exist build\obj mkdir build\obj

cl /nologo /c /O2 /TC /I third_party\minhook\include /I third_party\minhook\src /I third_party\minhook\src\hde ^
   third_party\minhook\src\buffer.c third_party\minhook\src\hook.c third_party\minhook\src\trampoline.c third_party\minhook\src\hde\hde64.c ^
   /Fo:build\obj\
if errorlevel 1 exit /b 1

cl /nologo /LD /O2 /std:c++17 /EHsc /W4 /D_CRT_SECURE_NO_WARNINGS /DHWFSTRIKE_VERSION=\"0.5.0\" ^
   /I third_party\minhook\include ^
   src\main.cpp src\config.cpp src\types.cpp ^
   build\obj\buffer.obj build\obj\hook.obj build\obj\trampoline.obj build\obj\hde64.obj ^
   /Fo:build\obj\ /Fe:build\HWFStrike.dll ^
   /link /OUT:build\HWFStrike.dll user32.lib
if errorlevel 1 exit /b 1

echo.
echo Built build\HWFStrike.dll
