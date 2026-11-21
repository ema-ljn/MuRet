@echo off
rem Runs one built variant: hashes, the check against the correctly rounded hashes, differences and timings.
set "EXE=build\%~1\probe.exe"
echo ##### %~1
%EXE% hash 2000000 libm
%EXE% check 2000000 libm expected\cr-2000000.txt
%EXE% list | findstr /x llvm >nul
if errorlevel 1 goto :nollvm
%EXE% check 2000000 llvm expected\cr-2000000.txt
%EXE% table 2000000 llvm libm
%EXE% time 300000 libm llvm
goto :eof
:nollvm
echo (no LLVM-libc in this variant)
%EXE% time 300000 libm
