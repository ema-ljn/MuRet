@echo off
rem Tries to compile the CORE-MATH C sources of a tree with MSVC, one file at a time, and says which ones fail.
setlocal EnableDelayedExpansion
set "TREE=%~1"
if not exist "%TREE%\src" (echo no tree at %TREE% & exit /b 0)
if not defined VCToolsInstallDir (
  for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
  call "!VSROOT!\VC\Auxiliary\Build\vcvars64.bat" >nul
)
echo ##### CORE-MATH with MSVC: %TREE%
for %%p in (binary64 binary32) do (
  for %%f in (sin cos tan acos log10) do (
    set "FILE=%%f"
    if "%%p"=="binary32" set "FILE=%%ff"
    if exist "%TREE%\src\%%p\%%f\!FILE!.c" (
      cl /nologo /O2 /std:c17 /c "%TREE%\src\%%p\%%f\!FILE!.c" /I"%TREE%\src\%%p\%%f" /Fo%TEMP%\cm_probe.obj > cm.log 2>&1
      if errorlevel 1 (
        echo %%p !FILE!: DOES NOT COMPILE
        powershell -NoProfile -Command "(Select-String -Path cm.log -Pattern 'error').Count; Get-Content cm.log -TotalCount 5"
      ) else (
        echo %%p !FILE!: compiles
      )
    )
  )
)
exit /b 0
