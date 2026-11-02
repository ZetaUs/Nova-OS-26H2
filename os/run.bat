@echo off
setlocal
call "%~dp0build.bat"
if errorlevel 1 exit /b 1

set "SHARE=%~dp0..\NovaShare"
if not exist "%SHARE%" mkdir "%SHARE%"
set "BOOT_IMAGE=%~dp0build\nova-os-26h2.img"
set "DATA_IMAGE=%~dp0build\nova-data.img"

"%~dp0..\Program\qemu\qemu-system-x86_64.exe" -machine pc -drive file="%BOOT_IMAGE%",format=raw,if=none,id=boot -device ide-hd,drive=boot,bus=ide.0,unit=0 -drive "file=fat:rw:%SHARE%,format=raw,if=none,id=share" -device ide-hd,drive=share,bus=ide.0,unit=1 -drive file="%DATA_IMAGE%",format=raw,if=none,id=data -device ide-hd,drive=data,bus=ide.1,unit=0 -m 128M -boot c -vga std -device isa-debug-exit,iobase=0xf4,iosize=0x04
