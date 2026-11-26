@echo off
setlocal
set "SRC=Server\MyDll"
set "OUT=build\mydll-vc6"
if not exist "%SRC%\build_vc6.bat" exit /b 2
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OUT%" exit /b 2
set "FLAGS=/nologo /MT /W4 /WX /Od /D_WIN32_WINNT=0x0502 /I%SRC%\src /Fo%OUT%\"
if "%~1"=="hooks-red" goto hooks_red
if "%~1"=="hooks" goto hooks
if "%~1"=="host-red" goto host_red
if "%~1"=="host" goto host
if "%~1"=="load" goto load
if "%~1"=="dll" goto dll
echo Usage: Server\MyDll\build_vc6.bat dll^|hooks^|host^|load
exit /b 2

:hooks_red
cl %FLAGS% /DMYDLL_RED_HOOKS "%SRC%\tests\test_boot_hooks.cpp" /Fe"%OUT%\test_boot_hooks_red.exe" /link /BASE:0x00400000 /FIXED /SECTION:.landing,ERW
exit /b %ERRORLEVEL%

:hooks
cl %FLAGS% "%SRC%\tests\test_boot_hooks.cpp" "%SRC%\src\BootHooks.cpp" /Fe"%OUT%\test_boot_hooks.exe" /link /BASE:0x00400000 /FIXED /SECTION:.landing,ERW
exit /b %ERRORLEVEL%

:host_red
cl %FLAGS% /DMYDLL_HOST_TEST /DMYDLL_RED_HOST "%SRC%\tests\test_host_boot.cpp" "%SRC%\src\BootHooks.cpp" /Fe"%OUT%\test_host_boot_red.exe"
exit /b %ERRORLEVEL%

:host
cl %FLAGS% /DMYDLL_HOST_TEST "%SRC%\tests\test_host_boot.cpp" "%SRC%\src\BootHooks.cpp" "%SRC%\src\HostBoot.cpp" "%SRC%\src\BootOutcome.cpp" /Fe"%OUT%\test_host_boot.exe"
exit /b %ERRORLEVEL%

:load
cl %FLAGS% "%SRC%\tests\test_library_load.cpp" /Fe"%OUT%\test_library_load.exe"
exit /b %ERRORLEVEL%

:dll
cl %FLAGS% /LD "%SRC%\src\Init.cpp" "%SRC%\src\BootHooks.cpp" "%SRC%\src\HostBoot.cpp" "%SRC%\src\GameServer102i.cpp" "%SRC%\src\BootOutcome.cpp" /link /INCREMENTAL:NO /BASE:0x20000000 /DEF:"%SRC%\src\MyDll.def" /OUT:"%OUT%\MyDll.dll" /IMPLIB:"%OUT%\MyDll.lib"
exit /b %ERRORLEVEL%
