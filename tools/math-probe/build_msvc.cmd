@echo off
rem Builds the probe with MSVC (or clang-cl): the system math library and, when it compiles, LLVM-libc.
rem   build_msvc.cmd <name> <llvm libc dir> "<flags>" [compiler] [std switch]
setlocal EnableDelayedExpansion
set "NAME=%~1"
set "LLVM=%~2"
set "FLAGS=%~3"
set "COMPILER=%~4"
if "%COMPILER%"=="" set "COMPILER=cl"
set "STD=%~5"
if "%STD%"=="" set "STD=/std:c++20"
if not defined VCToolsInstallDir (
  for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
  call "!VSROOT!\VC\Auxiliary\Build\vcvars64.bat" >nul
)
set "OUT=build\%NAME%"
if not exist "%OUT%" mkdir "%OUT%"
echo === %NAME%: %COMPILER% %STD% %FLAGS%
%COMPILER% /nologo %STD% /EHsc %FLAGS% /Isrc /c src\impl_libm.cpp /Fo%OUT%\impl_libm.obj || exit /b 1
set HAS_LLVM=0
%COMPILER% /nologo %STD% /EHsc %FLAGS% /Isrc /I"%LLVM%" /DLIBC_NAMESPACE=probe_llvm_a /DPROBE_NAME=llvm /DPROBE_ENTRY=ImplLlvm /c src\impl_llvm.cpp /Fo%OUT%\impl_llvm.obj > %OUT%\llvm.log 2>&1
if errorlevel 1 (
  echo LLVM-libc did NOT compile with these switches. First lines of the log:
  powershell -NoProfile -Command "Get-Content '%OUT%\llvm.log' -TotalCount 40"
  del /q %OUT%\impl_llvm.obj 2>nul
) else (
  set HAS_LLVM=1
  powershell -NoProfile -Command "Get-Content '%OUT%\llvm.log' -TotalCount 12"
)
%COMPILER% /nologo %STD% /EHsc %FLAGS% /Isrc /DPROBE_HAS_LLVM=%HAS_LLVM% /c src\probe.cpp /Fo%OUT%\probe.obj || exit /b 1
link /nologo /OUT:%OUT%\probe.exe %OUT%\*.obj || exit /b 1
echo built %OUT%\probe.exe with LLVM=%HAS_LLVM%
endlocal
