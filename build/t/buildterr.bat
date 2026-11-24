@echo off
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSPATH="
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH set "VSPATH=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools"
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0..\.."
cl /nologo /std:c++17 /O2 /fp:fast /EHsc /W3 build\t\terr.cpp src\scene.cpp src\render.cpp src\terrain.cpp src\presets.cpp src\mesh.cpp src\image.cpp src\vmath.cpp /Fobuild\t\ /Febuild\t\terr.exe /link windowscodecs.lib ole32.lib || exit /b 1
