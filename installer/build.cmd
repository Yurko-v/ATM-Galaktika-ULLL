@echo off
setlocal
cd /d "%~dp0.."

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -prerelease -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set "MSBUILD=%%i"
if not defined MSBUILD (
  echo MSBuild not found
  exit /b 1
)

set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (
  echo Inno Setup 6 not found: winget install JRSoftware.InnoSetup
  exit /b 1
)

"%MSBUILD%" GalaxyATMSystem.sln -p:Configuration=Release -p:Platform=Win32 -v:minimal -nologo || exit /b 1
"%ISCC%" /Q installer\GalaxyATMSystem.iss %* || exit /b 1
echo Done: dist\
