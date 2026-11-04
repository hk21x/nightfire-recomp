@echo off
setlocal
rem The release's package from build.bat's output (run that first, with a disc in assets): the runtime and the compiler for
rem both programs, no game code. One package serves the USA and the PAL disc (the same code). Attach the zip and its
rem .json to the tag's draft release, then publish it.
cd /d "%~dp0"
call sdk\scripts\release-package.bat NightFire-package.zip Nightfire NightfireDriving
endlocal
