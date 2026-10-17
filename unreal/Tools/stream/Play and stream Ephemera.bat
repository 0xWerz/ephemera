@echo off
rem Ephemera on this PC's own screen, and in a web browser at the same time: one game, yours, that anyone on the home
rem network (or your tailnet) can watch and join from the address this window prints. Close with any key here.
cd /d "%~dp0..\.."
powershell -NoProfile -ExecutionPolicy Bypass -File "Tools\stream\stream.ps1" -Serve -Window -Scale 100
pause
