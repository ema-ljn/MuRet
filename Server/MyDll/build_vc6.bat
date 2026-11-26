@echo off
setlocal
set "SRC=Server\MyDll"
set "OUT=build\mydll-vc6"
if not exist "%SRC%\build_vc6.bat" exit /b 2
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OUT%" exit /b 2
set "FLAGS=/nologo /MT /W3 /WX /Od /DWIN32 /DNDEBUG /D_WINDOWS /D_MBCS /D_USRDLL /D_WINDLL /GX /I%SRC%\src\Native"
if "%~1"=="native" goto native
if "%~1"=="dll" goto dll
if "%~1"=="test" goto test
echo Usage: %SRC%\build_vc6.bat native^|dll^|test
exit /b 2

:native
cl %FLAGS% /Fo%OUT%\ /c "%SRC%\src\Native\ByteOrder.cpp"
if errorlevel 1 exit /b %ERRORLEVEL%
cl %FLAGS% /Fo%OUT%\ /c "%SRC%\src\Native\Commands.cpp"
if errorlevel 1 exit /b %ERRORLEVEL%
cl %FLAGS% /Fo%OUT%\ /c "%SRC%\src\Native\Wire.cpp"
if errorlevel 1 exit /b %ERRORLEVEL%
exit /b 0

:dll
call "%SRC%\build_vc6.bat" native
if errorlevel 1 exit /b %ERRORLEVEL%
cl %FLAGS% "%SRC%\src\Init.cpp" "%OUT%\ByteOrder.obj" /LD /Fe"%OUT%\MyDll.dll" /link /INCREMENTAL:NO /OPT:NOREF /OPT:NOICF /BASE:0x20000000 /DEF:"%SRC%\src\MyDll.def" /MAP:"%OUT%\MyDll.map"
exit /b %ERRORLEVEL%

:test
cl %FLAGS% "%SRC%\tests\test_byte_order.cpp" "%SRC%\src\Native\ByteOrder.cpp" /Fe"%OUT%\test_byte_order.exe"
exit /b %ERRORLEVEL%
