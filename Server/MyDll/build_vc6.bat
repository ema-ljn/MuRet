@echo off
setlocal
set "SRC=Server\MyDll"
set "OUT=build\mydll-vc6"
if not exist "%SRC%\build_vc6.bat" exit /b 2
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OUT%" exit /b 2
set "FLAGS_BASE=/nologo /MT /W4 /WX /Od /D_WIN32_WINNT=0x0502 /I%SRC%\src"
set "FLAGS=%FLAGS_BASE% /Fo%OUT%\"
set "PROTOCOL=%SRC%\src\Protocol\Selection.cpp %SRC%\src\Protocol\SelectionList.cpp"
set "BOOT=%SRC%\src\Init.cpp %SRC%\src\BootHooks.cpp %SRC%\src\HostBoot.cpp %SRC%\src\GameServer102i.cpp %SRC%\src\BootOutcome.cpp %SRC%\src\Observer.cpp"
if "%~1"=="hooks-red" goto hooks_red
if "%~1"=="hooks" goto hooks
if "%~1"=="host-red" goto host_red
if "%~1"=="host" goto host
if "%~1"=="load" goto load
if "%~1"=="log" goto log
if "%~1"=="observer-red" goto observer_red
if "%~1"=="observer" goto observer
if "%~1"=="dll" goto dll
if "%~1"=="selection" goto selection
if "%~1"=="selection-tests" goto selection_tests
echo Usage: Server\MyDll\build_vc6.bat dll^|hooks^|host^|load^|log^|observer^|selection^|selection-tests
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

:log
cl %FLAGS% "%SRC%\tests\test_boot_log.cpp" "%SRC%\src\BootOutcome.cpp" /Fe"%OUT%\test_boot_log.exe"
exit /b %ERRORLEVEL%

:observer_red
cl %FLAGS% /DMYDLL_RED_OBSERVER "%SRC%\tests\test_observer.cpp" /Fe"%OUT%\test_observer_red.exe"
exit /b %ERRORLEVEL%

:observer
cl %FLAGS% "%SRC%\tests\test_observer.cpp" "%SRC%\src\Observer.cpp" "%SRC%\src\BootHooks.cpp" /Fe"%OUT%\test_observer.exe"
exit /b %ERRORLEVEL%

:dll
cl %FLAGS% /LD %BOOT% /link /INCREMENTAL:NO /BASE:0x20000000 /DEF:"%SRC%\src\MyDll.def" /OUT:"%OUT%\MyDll.dll" /IMPLIB:"%OUT%\MyDll.lib"
exit /b %ERRORLEVEL%

:selection
if not exist "%OUT%\selection" mkdir "%OUT%\selection"
if not exist "%OUT%\selection" exit /b 2
cl %FLAGS_BASE% /Fo%OUT%\selection\ /DMYDLL_SELECTION /LD %BOOT% "%SRC%\src\SelectionGateway.cpp" "%SRC%\src\TerrainBoot.cpp" %PROTOCOL% /link /INCREMENTAL:NO /BASE:0x20000000 /DEF:"%SRC%\src\MyDll.def" /OUT:"%OUT%\selection\MyDll.dll" /IMPLIB:"%OUT%\selection\MyDll.lib" /MAP:"%OUT%\selection\MyDll.map"
exit /b %ERRORLEVEL%

:selection_tests
cl %FLAGS% "%SRC%\tests\test_selection.cpp" %PROTOCOL% /Fe"%OUT%\test_selection.exe"
if errorlevel 1 exit /b %ERRORLEVEL%
cl %FLAGS% "%SRC%\tests\test_selection_gateway.cpp" "%SRC%\src\SelectionGateway.cpp" "%SRC%\src\GameServer102i.cpp" "%SRC%\src\Observer.cpp" "%SRC%\src\BootHooks.cpp" %PROTOCOL% /Fe"%OUT%\test_selection_gateway.exe"
if errorlevel 1 exit /b %ERRORLEVEL%
cl %FLAGS% "%SRC%\tests\test_terrain_boot.cpp" "%SRC%\src\TerrainBoot.cpp" "%SRC%\src\BootOutcome.cpp" /Fe"%OUT%\test_terrain_boot.exe"
exit /b %ERRORLEVEL%
