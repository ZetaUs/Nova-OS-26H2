@echo off
setlocal
call "%~dp0build.bat"
if errorlevel 1 exit /b 1

set "SHARE=%~dp0..\NovaShare"
if not exist "%SHARE%" mkdir "%SHARE%"

"%~dp0..\Program\qemu\qemu-system-x86_64.exe" -drive file="%~dp0build\nova-os-26h2.img",format=raw,if=ide,index=0 -drive file=fat:rw:%SHARE%,format=raw,if=ide,index=1 -drive file="%~dp0build\nova-data.img",format=raw,if=ide,index=2 -m 128M -boot c -vga std -device isa-debug-exit,iobase=0xf4,iosize=0x04
