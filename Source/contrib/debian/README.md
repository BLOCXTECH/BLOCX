
Debian
====================
This directory contains files used to package blocxd/blocx-qt
for Debian-based Linux systems. If you compile blocxd/blocx-qt yourself, there are some useful files here.

## blocx: URI support ##


blocx-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install blocx-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your blocx-qt binary to `/usr/bin`
and the `../../share/pixmaps/blocx128.png` to `/usr/share/pixmaps`

blocx-qt.protocol (KDE)

