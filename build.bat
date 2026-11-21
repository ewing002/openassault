@echo off
rem Builds OpenAssault on Windows: checks the tools, installs missing ones with winget (asks first),
rem then builds openassault.exe and OpenAssaultLauncher.exe into dist\OpenAssault\.
rem The first build downloads SDL2 and takes a while; the game data is not needed to build.
setlocal EnableExtensions
cd /d "%~dp0"

call :need git   Git.Git
call :need cmake Kitware.CMake
call :need py    Python.Python.3.12
set "VCVARS="
for %%E in (Enterprise Professional Community BuildTools) do (
  if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" (
    if not defined VCVARS set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat"
  )
)
if not defined VCVARS (
  echo Visual Studio 2022 with the C++ tools is missing.
  choice /C YN /M "Install Visual Studio 2022 Build Tools now (about 6 GB)"
  if errorlevel 2 exit /b 1
  winget install Microsoft.VisualStudio.2022.BuildTools --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive"
  echo Restart this script after the installation has finished.
  exit /b 1
)

call "%VCVARS%" >nul
cmake -S . -B build -A x64 || goto fail
cmake --build build --config Release || goto fail

if exist dist rmdir /s /q dist
mkdir dist\OpenAssault
copy build\Release\openassault.exe dist\OpenAssault\ >nul || goto fail
copy build\Release\OpenAssaultLauncher.exe dist\OpenAssault\ >nul || goto fail
xcopy /e /i /q lang dist\OpenAssault\lang >nul
echo.
echo Done: %CD%\dist\OpenAssault\
echo Copy this folder next to your Extreme Assault folder (the one with DATA\) and start OpenAssaultLauncher.exe.
exit /b 0

:need
where %1 >nul 2>nul && exit /b 0
echo %1 is missing.
choice /C YN /M "Install it with winget now"
if errorlevel 2 exit /b 1
winget install %2 --silent --accept-package-agreements --accept-source-agreements
rem winget updates PATH only for new shells
echo Installed %1. Open a new window and run build.bat again.
exit /b 1

:fail
echo Build failed.
exit /b 1
