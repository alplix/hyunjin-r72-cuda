@echo off

rem Exit with ERRORLEVEL containing the CUDA version level.
rem For example:  2010 = CUDA 2.1
rem               2000 = CUDA 2.0

rem The default CUDA install path contains spaces, so every use of it
rem below has to be quoted or findstr/exist split the path.

if "%CUDA_INC_PATH%"=="" goto notfound
if not exist "%CUDA_INC_PATH%\cuda.h" goto notfound

set cudaversion=
for /f "tokens=3 usebackq" %%i in (`findstr /c:"define CUDA_VERSION" "%CUDA_INC_PATH%\cuda.h"`) do (
  set cudaversion=%%i
)
if "%cudaversion%"=="" goto notfound
rem Guard against picking up a non-numeric match: exit codes are numeric.
echo %cudaversion%| findstr /r "^[0-9][0-9]*$" >nul || goto notfound
echo cudaversion=%cudaversion%
exit %cudaversion%

:notfound
exit 0
