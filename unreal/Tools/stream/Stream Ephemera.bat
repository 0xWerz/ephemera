@echo off
rem Starts Ephemera as a stream a web browser can play, shows where to open it, restarts the game if
rem it ever stops, and ends the stream when a key is pressed in this window. (A copy of this file may
rem live on the desktop: it finds the project by the path below.)
set PROJECT=F:\Ephemera
title Ephemera stream
powershell -NoProfile -ExecutionPolicy Bypass -File "%PROJECT%\Tools\stream\stream.ps1" -Serve
echo.
pause
