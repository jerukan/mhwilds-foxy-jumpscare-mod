@echo off
rem Builds jumpscare_audio.dll and copies it to ..\..\reframework\plugins\
rem Requires Visual Studio 2022 (or Build Tools) with "Desktop development with C++".
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo vswhere.exe not found - install Visual Studio 2022 Build Tools with the C++ workload.
    exit /b 1
)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo No Visual Studio install with the C++ x64 toolset was found.
    exit /b 1
)
call "%VSDIR%\Common7\Tools\VsDevCmd.bat" -arch=amd64 -no_logo || exit /b 1

cmake -S "%~dp0." -B "%~dp0build" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build "%~dp0build" || exit /b 1

if not exist "%~dp0..\..\reframework\plugins" mkdir "%~dp0..\..\reframework\plugins"
copy /y "%~dp0build\jumpscare_audio.dll" "%~dp0..\..\reframework\plugins\" >nul || exit /b 1
echo.
echo Built and copied to reframework\plugins\jumpscare_audio.dll
