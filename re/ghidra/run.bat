@echo off
rem run.bat out.c <hexaddr | c<hexaddr> ...>  -  headless Ghidra decompile of re\xa3dfx.img (Windows)
rem set GHIDRA=C:\tools\ghidra_11.1.2_PUBLIC   (needs a JDK 21)
setlocal
set HERE=%~dp0
if "%GHIDRA%"=="" set GHIDRA=C:\tools\ghidra_11.1.2_PUBLIC
if not exist "%HERE%proj.rep" call "%GHIDRA%\support\analyzeHeadless.bat" "%HERE%." proj -import "%HERE%..\xa3dfx.img" -processor x86:LE:32:default -noanalysis
set OUT=%1
shift
set ARGS=
:loop
if "%1"=="" goto run
set ARGS=%ARGS% %1
shift
goto loop
:run
call "%GHIDRA%\support\analyzeHeadless.bat" "%HERE%." proj -process xa3dfx.img -noanalysis -readOnly -scriptPath "%HERE%." -postScript Decomp.java %OUT% %ARGS%
