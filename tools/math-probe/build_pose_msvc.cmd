@echo off
rem Builds and runs the pose experiment with MSVC: the UCRT and LLVM-libc, in several configurations.
rem   build_pose_msvc.cmd <llvm libc dir> <glm dir>
setlocal EnableDelayedExpansion
set "LLVM=%~1"
set "GLM=%~2"
set "CLIENT=..\..\Client\src"
if not defined VCToolsInstallDir (
  for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
  call "!VSROOT!\VC\Auxiliary\Build\vcvars64.bat" >nul
)
set "SOURCES=pose\pose.cpp %CLIENT%\Render\Models\HalfLifeMath.cpp %CLIENT%\Render\Models\OriginalRotation.cpp"
call :one system-release system "/std:c++20 /O2 /MD"
call :one system-debug system "/std:c++20 /Od /MDd /RTC1"
call :one system-static system "/std:c++20 /O2 /MT"
call :one system-zc-cmath system "/std:c++latest /O2 /MD /Zc:cmath"
call :one cr-release cr "/std:c++20 /O2 /MD"
call :one cr-debug cr "/std:c++20 /Od /MDd /RTC1"
call :one cr-avx2 cr "/std:c++20 /O2 /MD /arch:AVX2"
exit /b 0

:one
set "OUT=build\pose-%~1"
if not exist "%OUT%" mkdir "%OUT%"
echo ##### pose %~1: cl %~3
if "%~2"=="cr" (
  cl /nologo /EHsc %~3 /I"%LLVM%" /DLIBC_NAMESPACE=pose_cr /c pose\cr_impl.cpp /Fo%OUT%\cr_impl.obj > %OUT%\build.log 2>&1
  if errorlevel 1 (powershell -NoProfile -Command "Get-Content '%OUT%\build.log' -TotalCount 30" & exit /b 0)
  cl /nologo /EHsc /D_CRT_SECURE_NO_WARNINGS %~3 /I%CLIENT% /I"%GLM%" /FIpose\cr_redirect.h %SOURCES% %OUT%\cr_impl.obj /Fo%OUT%\ /Fe%OUT%\pose.exe > %OUT%\build.log 2>&1
) else (
  cl /nologo /EHsc /D_CRT_SECURE_NO_WARNINGS %~3 /I%CLIENT% /I"%GLM%" %SOURCES% /Fo%OUT%\ /Fe%OUT%\pose.exe > %OUT%\build.log 2>&1
)
if errorlevel 1 (powershell -NoProfile -Command "Get-Content '%OUT%\build.log' -TotalCount 30" & exit /b 0)
findstr /c:"D9002" %OUT%\build.log
%OUT%\pose.exe
exit /b 0
