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
The package contains NO game data. Put your own raw NTSC-U disc image,
named ctr-u.bin, in a folder on each cabinet, for example C:\ctr-data (the
folder both templates name in data_dir). A linked cabinet needs ctr-u.bin:
with only the extracted BIGFILE.BIG files it stops at startup with "arcade
link requires a known build and content identity" (the extracted files
serve an unlinked run only). For a linked cabinet the folder holds ONLY
ctr-u.bin: delete any extracted game files (BIGFILE.BIG and the rest)
beside it. They would be read in place of the disc image without being
part of its hash, so the link checks pass and the cabinets can desync.
Both cabinets need the same ctr-u.bin.

Set up each cabinet
-------------------
Below, replace <cabinet 1 IP> or <cabinet 2 IP> as a whole, angle brackets
included, with that cabinet's fixed IP address.
1. Copy the contents of this folder to the cabinet, so that ctr_native.exe
   is directly in the target folder (not in a nested package folder). The
   folder must be writable: the log file (Crash Team Racing.log) and
   memcards\ are created next to the exe. arcade.cfg, memcards\ and the log
   belong to one cabinet: leave them out of any folder sync between the
   cabinets. If a sync copied them, delete the copied memcards\ and log
   and redo steps 2 and 3. Never copy a memcards\ save to a cabinet.
2. Cabinet 1: copy cab1.cfg to arcade.cfg (next to ctr_native.exe).
   Cabinet 2: copy cab2.cfg to arcade.cfg.
3. Edit arcade.cfg (save it as UTF-8 or ANSI text): set peer to the OTHER
   cabinet's fixed IP and keep its port (cabinet 1: <cabinet 2 IP>:7002,
   cabinet 2: <cabinet 1 IP>:7001). Change data_dir only if ctr-u.bin is
   not in C:\ctr-data (a full path, or one relative to this folder;
   C:ctr-data and \ctr-data are refused). Keep seat, port and fullscreen.
   A comment goes on its own line: after a value it becomes part of it.
4. Firewall. In an elevated PowerShell (Run as administrator), after
   Set-Location to this folder, allow the link port for this exe from the
   other cabinet only:
     cabinet 1:
       New-NetFirewallRule -DisplayName "CTR arcade link" -Direction Inbound -Action Allow -Protocol UDP -LocalPort 7001 -RemoteAddress <cabinet 2 IP> -Program "$PWD\ctr_native.exe"
     cabinet 2:
       New-NetFirewallRule -DisplayName "CTR arcade link" -Direction Inbound -Action Allow -Protocol UDP -LocalPort 7002 -RemoteAddress <cabinet 1 IP> -Program "$PWD\ctr_native.exe"
   A Block rule for the exe beats the Allow rule (Windows may add one, for
   example when its firewall prompt is cancelled). This must list nothing:
       Get-NetFirewallApplicationFilter -Program "$PWD\ctr_native.exe" | Get-NetFirewallRule | Where-Object Action -eq 'Block'
   Remove what it lists by adding | Remove-NetFirewallRule to it. Check
   again after the first start if Windows showed a firewall prompt. If
   this folder or the other cabinet's IP changes, remove the rule
   (Remove-NetFirewallRule -DisplayName "CTR arcade link") and add it again.
5. Same build and same disc. On both cabinets, in PowerShell in this folder:
       Get-FileHash ctr_native.exe -Algorithm SHA256
       Get-FileHash C:\ctr-data\ctr-u.bin -Algorithm SHA256
   (use your data_dir). The ctr_native.exe hash must equal its line in
   MANIFEST.txt, and each hash must be the same on both cabinets: the link
   refuses two different builds or disc images ("LINK REFUSED: SETTINGS DO
   NOT MATCH"). Neither check sees extracted files: on both cabinets
   Get-ChildItem C:\ctr-data -Force (your data_dir) must list only
   ctr-u.bin.

Start
-----
Double-click ctr_native.exe, or run it with no arguments. It reads
arcade.cfg next to it. Among its first console lines it must show
  [CTR Native] Config file: <this folder>\arcade.cfg
  [CTR Native] Config groups from the file: link fullscreen data_dir
These lines are on the console only, not in the log; the fullscreen window
may hide the console (Alt+Tab to it). "Config file: none" means there is no
arcade.cfg next to the exe (check for a hidden .txt extension), and the
cabinet would start unlinked. A config file with an error stops the game
with a message naming the file and, where there is one, the line. Startup
errors, such as that one or "arcade link requires a known build and
content identity.", are on the console only, not in the log. After a
double-click the console stays open on an error until Enter is pressed.
