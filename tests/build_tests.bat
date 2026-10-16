@echo off
setlocal
cd /D "%~dp0"
where cl >nul 2>nul && goto :have_cl
set vswhere="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`%vswhere% -latest -products * -property installationPath`) do call "%%i\VC\Auxiliary\Build\vcvars64.bat" >nul
:have_cl
if not exist ..\build mkdir ..\build
pushd ..\build
call cl /Od /Z7 /nologo /FC /W3 /wd4244 /wd4305 /wd4267 /wd4018 /wd4146 /Zc:preprocessor /I..\..\raddebugger\src /I.. ..\tests\parse_dump.c /link /INCREMENTAL:NO /out:parse_dump.exe || (popd & exit /b 1)
popd
