@echo off
setlocal
set "ROOT=%~dp0.."
set "NASM=%ROOT%\Program\NASM\nasm.exe"
set "OUT=%~dp0build"
set "DEVCPP_BIN="

if defined DEVCPP_HOME if exist "%DEVCPP_HOME%\MinGW64\bin\g++.exe" set "DEVCPP_BIN=%DEVCPP_HOME%\MinGW64\bin"
if not defined DEVCPP_BIN if exist "D:\Program\Dev-Cpp\MinGW64\bin\g++.exe" set "DEVCPP_BIN=D:\Program\Dev-Cpp\MinGW64\bin"
if not defined DEVCPP_BIN if exist "%ProgramFiles(x86)%\Dev-Cpp\MinGW64\bin\g++.exe" set "DEVCPP_BIN=%ProgramFiles(x86)%\Dev-Cpp\MinGW64\bin"
if not defined DEVCPP_BIN if exist "%ProgramFiles%\Dev-Cpp\MinGW64\bin\g++.exe" set "DEVCPP_BIN=%ProgramFiles%\Dev-Cpp\MinGW64\bin"
if not defined DEVCPP_BIN (
  for /f "delims=" %%P in ('where g++.exe 2^>nul') do if not defined DEVCPP_BIN set "DEVCPP_BIN=%%~dpP"
)
if not defined DEVCPP_BIN (
  echo Dev-C++ MinGW compiler not found. Set DEVCPP_HOME to the Dev-C++ folder.
  exit /b 1
)
if not exist "%NASM%" (
  echo NASM was not found at "%NASM%".
  exit /b 1
)
where python.exe >nul 2>&1
if errorlevel 1 (
  echo Python 3 is required to generate the embedded Chinese font.
  exit /b 1
)

set "GXX=%DEVCPP_BIN%\g++.exe"
set "LD=%DEVCPP_BIN%\ld.exe"
set "OBJCOPY=%DEVCPP_BIN%\objcopy.exe"
if not exist "%OUT%" mkdir "%OUT%"

echo [1/10] Preparing the persistent Nova data disk...
python "%~dp0initialize_data_disk.py" "%OUT%\nova-data.img"
if errorlevel 1 exit /b 1

echo [1.5/9] Preparing the default Nova wallpaper...
python "%~dp0generate_wallpaper.py" "%ROOT%\image\Wallpaper\img0.png" "%OUT%\wallpaper.rgb565"
if errorlevel 1 exit /b 1

echo [1/9] Assembling BIOS boot sector...
"%NASM%" -f bin "%~dp0boot.asm" -o "%OUT%\boot.bin"
if errorlevel 1 exit /b 1

echo [2/9] Assembling second-stage long-mode loader...
"%NASM%" -f bin "%~dp0stage2.asm" -o "%OUT%\stage2.bin"
if errorlevel 1 exit /b 1

echo [3/9] Assembling x86-64 kernel entry...
"%NASM%" -f win64 "%~dp0kernel_entry.asm" -o "%OUT%\kernel_entry.o"
if errorlevel 1 exit /b 1

echo [4/9] Generating the embedded Chinese font...
python "%~dp0generate_chinese_font.py" "%~dp0chinese_glyphs.txt" "%ROOT%\HZKX\HZK48" "%OUT%\chinese_font.h"
if errorlevel 1 exit /b 1

echo [5/9] Embedding the ASC48 ASCII font...
python "%~dp0generate_ascii_font.py" "%ROOT%\HZKX\asc48.bin" "%OUT%\ascii_font.h"
if errorlevel 1 exit /b 1

echo [6/9] Generating the high-resolution logo...
python "%~dp0generate_logo.py" "%ROOT%\image\logo.png" "%OUT%\logo_data.h"
if errorlevel 1 exit /b 1

echo [6.5/9] Generating the mouse cursor sprite...
python "%~dp0generate_mouse_cursor.py" "%ROOT%\image\mouse.png" "%OUT%\mouse_cursor_data.h"
if errorlevel 1 exit /b 1

echo [6.8/10] Assembling the bundled ZEX applications...
for %%A in (filemgr notepad browser settings terminal) do (
  "%NASM%" -f bin "%ROOT%\RUN\ZEX\%%A.asm" -o "%OUT%\%%A.zex"
  if errorlevel 1 exit /b 1
)
python "%~dp0generate_zex_apps.py" "%OUT%" "%OUT%\zex_apps.h"
if errorlevel 1 exit /b 1

echo [7/9] Compiling the freestanding 64-bit kernel with Dev-C++ MinGW...
"%GXX%" -m64 -std=c++11 -Os -ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit -fno-stack-protector -fno-pic -fno-pie -fno-builtin -fno-asynchronous-unwind-tables -fno-unwind-tables -mno-red-zone -finput-charset=utf-8 -fexec-charset=utf-8 -I"%OUT%" -c "%~dp0kernel.cpp" -o "%OUT%\kernel.o"
if errorlevel 1 exit /b 1

echo [8/9] Linking and flattening the kernel image...
"%LD%" -mi386pep --image-base 0 --section-alignment 16 --file-alignment 16 --section-start .text=0x10000 -e kernel_entry -o "%OUT%\kernel.exe" "%OUT%\kernel_entry.o" "%OUT%\kernel.o"
if errorlevel 1 exit /b 1
"%OBJCOPY%" --only-section=.text --only-section=.rdata --only-section=.data -O binary "%OUT%\kernel.exe" "%OUT%\kernel.bin"
if errorlevel 1 exit /b 1

for %%F in ("%OUT%\boot.bin") do if not %%~zF==512 (
  echo Boot sector must be 512 bytes, got %%~zF bytes.
  exit /b 1
)
for %%F in ("%OUT%\stage2.bin") do if %%~zF GTR 4096 (
  echo Stage two exceeds its 8-sector limit: %%~zF bytes.
  exit /b 1
)
for %%F in ("%OUT%\kernel.bin") do if %%~zF GTR 65024 (
  echo Kernel exceeds its 127-sector BIOS loading limit: %%~zF bytes.
  exit /b 1
)

echo [9/9] Creating the 64 MiB BIOS disk image...
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "try { $boot=[System.IO.File]::ReadAllBytes('%OUT%\boot.bin'); $stage2=[System.IO.File]::ReadAllBytes('%OUT%\stage2.bin'); $kernel=[System.IO.File]::ReadAllBytes('%OUT%\kernel.bin'); $wallpaper=[System.IO.File]::ReadAllBytes('%OUT%\wallpaper.rgb565'); if ($wallpaper.Length -ne 508032) { throw 'Wallpaper image must contain exactly 508032 bytes.' }; $img=New-Object byte[] (64*1024*1024); $boot.CopyTo($img,0); $stage2.CopyTo($img,512); $kernel.CopyTo($img,9*512); $wallpaper.CopyTo($img,136*512); [System.IO.File]::WriteAllBytes('%OUT%\nova-os-26h2.img',$img) } catch { Write-Error $_; exit 1 }"
if errorlevel 1 exit /b 1

echo Built 64-bit Nova OS 26H2: %OUT%\nova-os-26h2.img
