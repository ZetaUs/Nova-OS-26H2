# Nova 26H2

Nova 26H2 is a small BIOS-bootable x86-64 OS experiment. It reuses the Nova identity and high-resolution desktop direction of the `Try 1\os` project, while replacing its 32-bit protected-mode kernel with a native 64-bit freestanding C++ kernel.

## Build

From the workspace root, run:

```powershell
.\os\build.bat
```

The build uses the Dev-C++ MinGW-w64 compiler (`-m64`), the bundled NASM assembler, and Python 3 to embed the Chinese glyphs and ASC48 English glyphs used by the desktop and menu. It produces `os\build\nova-os-26h2.img` so that an older running QEMU instance can keep using the previous disk image. If Dev-C++ is installed elsewhere, set `DEVCPP_HOME` to its installation directory before building.

The BIOS loader requests a VBE linear framebuffer, enables the QEMU/Bochs VBE 1920x1080x32 mode, builds identity-mapped 2 MiB page tables for the first 4 GiB, and enters x86-64 long mode. The kernel occupies up to 127 BIOS sectors; the desktop wallpaper is generated from `image\Wallpaper\img0.png` as a 672x378 RGB565 image and loaded from LBA 136 before entering long mode. The build assembles the five ZEX applications in `RUN\ZEX`, validates their version-2 headers, and embeds them in the kernel image, so runtime does not depend on a host `Z:` drive. Clicking a desktop icon loads that ZEX at its declared address and handles its `int 0x80` system calls for window creation, text output, key wait, and exit. All bundled windows can be closed by clicking the title-bar × button, pressing Alt+F4, or pressing another key; the desktop and mouse cursor are redrawn when the program exits. The file-manager ZEX browses FAT12/16/32 volumes using ATA PIO, including a persistent 32 MiB `os\build\nova-data.img` volume and the host folder `NovaShare` exposed by QEMU. The data image is created only when absent and is preserved by subsequent builds. It lists only mounted FAT volumes: A: is the persistent data disk and B: is `NovaShare`. The raw boot image is not browsable as a FAT system volume, and other host drive letters such as `X:\` are not automatically enumerated. File-manager navigation supports directory selection, Enter, Backspace, Tab volume switching, arrow-key and PageUp/PageDown scrolling (up to 128 entries), and Alt+F4; listing is currently read-only. The other bundled ZEX files remain window prototypes and do not yet implement their advertised full functionality. The full-HD mode reduces software-rendering and framebuffer-copy work. Desktop and loading frames are drawn to a back buffer before being presented, preventing partially rendered screen transitions. The build downsamples the source PNG into a 192x192 antialiased logo mask and embeds it with run-length encoding. Replace that PNG to change the default wallpaper; other source wallpapers can remain in the same folder. Desktop app icons follow the Explorer, Notepad, Browser, Settings, and Terminal layout from the Nova reference site. The software mouse pointer uses the antialiased image in `image\mouse.png` at 14x22 pixels (about 60% of its previous dimensions) and retains one-pixel movement precision. The startup screen contains only the Nova logo and a progress bar; shutdown displays the logo, “正在关机”, and an animated progress bar before briefly going black and requesting QEMU debug exit or ACPI S5. ASCII text uses coverage-based antialiasing to retain detail when scaled, and desktop icon labels are centered beneath their icons. Back-buffer drawing uses normal memory writes, and each row is copied to the framebuffer with `REP MOVSD` for faster full-screen presentation. The kernel is deliberately freestanding and does not depend on a hosted C++ runtime.

## Run

```powershell
.\os\run.bat
```

This builds the image and launches it in the bundled QEMU standard VGA device with a persistent data disk and the host folder `NovaShare` attached. Put files in `NovaShare` before launching to browse them in Nova; no sample files are added. A Nova loading screen appears before the 1920x1080 desktop. The desktop displays a software mouse pointer in the center of the screen. The desktop, taskbar, and start menu use Chinese labels. Press the **Windows key** or click the Nova button at the bottom left to open or close the start menu. In the open menu, use **Up/Down** to select **重启** or **关机**, then press **Enter**; these actions are also available by mouse. Press **Esc** to close an open menu. Selecting **关机** powers off the guest and exits QEMU.

## Image layout

| Disk sectors | Contents |
| --- | --- |
| 0 | BIOS boot sector |
| 1-8 | Real-mode VBE and x86-64 loader |
| 9-135 | 64-bit kernel image (up to 127 sectors) |
| 136-1128 | 672x378 RGB565 Nova wallpaper |
