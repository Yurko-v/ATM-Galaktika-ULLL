@echo off
setlocal
cd /d "%~dp0"

rem vswhere is missing on some machines with Build Tools only, so try the known
rem Build Tools location first and fall back to vswhere.
set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars32.bat"
if not exist "%VCVARS%" (
  set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
  for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -prerelease -products * -find VC\Auxiliary\Build\vcvars32.bat`) do set "VCVARS=%%i"
)
if not exist "%VCVARS%" (
  echo vcvars32.bat not found - install the C++ Build Tools
  exit /b 1
)

call "%VCVARS%" >nul 2>nul
cl /nologo /O2 /EHsc /utf-8 /DUNICODE /D_UNICODE afvsim.cpp /Fe:afvsim.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED || exit /b 1
del afvsim.obj 2>nul
echo Done: %~dp0afvsim.exe
