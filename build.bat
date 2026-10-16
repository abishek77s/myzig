@echo off
setlocal
cd /D "%~dp0"

:: Builds mdview against the RAD Debugger sources in ..\raddebugger\src.
:: Usage: build [release]
:: Uses cl from PATH, or finds the newest Visual Studio install via vswhere.

where cl >nul 2>nul && goto :have_cl
set vswhere="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`%vswhere% -latest -products * -property installationPath`) do call "%%i\VC\Auxiliary\Build\vcvars64.bat" >nul
where cl >nul 2>nul || (echo [error: MSVC cl.exe not found] & exit /b 1)
:have_cl

set rad=..\..\raddebugger\src
set common=/I%rad% /I.. /nologo /FC /Z7 /Zc:preprocessor /W3 /wd4244 /wd4305 /wd4267 /wd4018 /wd4146
if "%1"=="release" (set opts=/O2 /DBUILD_DEBUG=0) else (set opts=/Od /Ob1 /DBUILD_DEBUG=1)

if not exist build mkdir build
pushd build
call cl %opts% %common% ..\mdview_main.c /link /INCREMENTAL:NO /SUBSYSTEM:WINDOWS /out:mdview.exe || (popd & exit /b 1)
popd
echo [built build\mdview.exe]
