CTR Native arcade package
=========================

This folder is one self-contained build of CTR Native for a two-cabinet
arcade link. Nothing needs installing: ctr_native.exe is fully static.

Files
-----
  ctr_native.exe   the game (Release build)
  cab1.cfg         config template for cabinet 1
  cab2.cfg         config template for cabinet 2
  MANIFEST.txt     version, commit, and SHA-256 of every file
  README.txt       this file

Game data
---------
The package contains NO game data. Put your own ctr-u.bin (or the extracted
BIGFILE.BIG files) in a folder on each cabinet, for example C:\ctr-data, and
set data_dir to that folder in arcade.cfg: a full path such as C:\ctr-data,
or a path relative to this folder (C:ctr-data and \ctr-data are refused).

Set up each cabinet
-------------------
1. Copy this whole folder to the cabinet. The folder must be writable: the
   log file (Crash Team Racing.log) and memcards\ are created next to the exe.
2. Cabinet 1: copy cab1.cfg to arcade.cfg (next to ctr_native.exe).
   Cabinet 2: copy cab2.cfg to arcade.cfg.
3. Edit arcade.cfg: set peer to the OTHER cabinet's fixed IP address (keep
   its port), and set data_dir to your data folder.
4. Allow the link port through the firewall. In an elevated PowerShell:
     cabinet 1:
       New-NetFirewallRule -DisplayName "CTR arcade link" -Direction Inbound -Protocol UDP -LocalPort 7001 -Action Allow
     cabinet 2:
       New-NetFirewallRule -DisplayName "CTR arcade link" -Direction Inbound -Protocol UDP -LocalPort 7002 -Action Allow

Start
-----
Double-click ctr_native.exe (it reads arcade.cfg next to it), or, from a
command prompt in this folder, run
  ctr_native.exe --config cab1.cfg
(a relative --config path is read from the current folder). A config file
with an error stops the game with a message naming the file and, where there
is one, the line.

Both cabinets must run the same build
-------------------------------------
The link handshake rejects two different builds. On both cabinets run
  Get-FileHash ctr_native.exe -Algorithm SHA256
and check that the hash equals the ctr_native.exe line in MANIFEST.txt.
