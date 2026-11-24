@echo off
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSPATH="
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH set "VSPATH=%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools"
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /std:c++17 /O2 /fp:fast /EHsc /W3 /MP /DNDEBUG src\*.cpp /Fobuild\ /Febuild\Brycescape.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comdlg32.lib windowscodecs.lib ole32.lib || exit /b 1
echo Built build\Brycescape.exe
