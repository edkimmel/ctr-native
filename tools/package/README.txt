CTR Native arcade package
=========================

This folder is one self-contained build of CTR Native for a two-cabinet
arcade link. Nothing needs installing: ctr_native.exe is fully static.

Files
-----
  ctr_native.exe   the game (Release build)
  arcade.cfg       the config, the same file for every cabinet
  MANIFEST.txt     version, commit, and SHA-256 of every file
  README.txt       this file

Game data
---------
The package contains NO game data. Put your own raw NTSC-U disc image,
named ctr-u.bin, in a folder on each cabinet, for example C:\ctr-data (the
folder arcade.cfg names in data_dir). A linked cabinet needs ctr-u.bin:
with only the extracted BIGFILE.BIG files it stops at startup with "arcade
link requires a known build and content identity" (the extracted files
serve an unlinked run only). For a linked cabinet the folder holds ONLY
ctr-u.bin: delete any extracted game files (BIGFILE.BIG and the rest)
beside it. They would be read in place of the disc image without being
part of its hash, so the link checks pass and the cabinets can desync.
Both cabinets need the same ctr-u.bin.

Set up each cabinet
-------------------
Do these steps on both cabinets. Both use the same arcade.cfg, as shipped:
seat = auto. The cabinets find each other on the local network and elect
their seats (the cabinet with the lower IP address is cabinet 1).
1. Copy the contents of this folder to the cabinet, so that ctr_native.exe
   is directly in the target folder (not in a nested package folder). The
   folder must be writable: the log file (Crash Team Racing.log) and
   memcards\ are created next to the exe. memcards\ and the log belong to
   one cabinet, and so does arcade.cfg once you edit it: leave them out of
   any folder sync between the cabinets. If a sync copied them, delete the
   copied memcards\ and log and redo step 2. Never copy a memcards\ save to
   a cabinet. Extracting a new package over this folder overwrites
   arcade.cfg: keep a copy of an edited one and put it back afterwards.
2. arcade.cfg ships ready. Edit it (save it as UTF-8 or ANSI text) only
   to change data_dir, and only if ctr-u.bin is not in C:\ctr-data (a full
   path, or one relative to this folder; C:ctr-data and \ctr-data are
   refused). render_scale (1, 2, 3, 4, 6, or 8; shipped 8) and
   texture_filter (nearest or bilinear; shipped bilinear) set this
   cabinet's picture only; the cabinets may differ. Lower render_scale if
   the game stutters. Keep fullscreen = 1. A comment goes on its own line:
   after a value it becomes part of it.
   Optional overrides (arcade.cfg shows each as a comment):
   - seat = cab1 or seat = cab2 fixes this cabinet's seat; the other
     cabinet takes the other one. Two cabinets that fix the same seat never
     link.
   - group = <name> keeps two installations on one network apart: only
     cabinets with the same group link. Both cabinets need the same group.
   - lan = 192.168.1.0/24 (the arcade switch's subnet) is for a cabinet
     with two network cards. Windows sends the search (to
     255.255.255.255) through one card only, and without lan the search
     also goes onto the other network. With lan the cabinet searches only
     that subnet and hears only cabinets in it; a static peer must be in
     it too. Both cabinets need the same value. If no card of the cabinet
     is in the subnet, it logs
       [CTR Native] arcade discovery: no network interface in lan 192.168.1.0/24; not beaconing, retrying
     once, sends nothing, and looks again every 10 s; it never uses the
     other card instead. When the card is back it logs
       [CTR Native] arcade discovery: lan 192.168.1.0/24 on interface <its address>; beaconing
   - port = <port> sets this cabinet's link port (default 7001; never
     7000, the discovery port). Use the same port in the firewall rule.
   - A static peer turns the search off: seat = cab1 or cab2 (a different
     one on each cabinet), port, and peer = <other cabinet IP>:<its port>,
     with no group. Set it up on BOTH cabinets: a static cabinet does not
     look for the other one, so a static cabinet and a searching one never
     link.
3. Firewall. In an elevated PowerShell (Run as administrator), after
   Set-Location to this folder, allow the discovery port 7000 and the link
   port 7001 for this exe from the local network (the same rule on both
   cabinets):
     New-NetFirewallRule -DisplayName "CTR arcade link" -Direction Inbound -Protocol UDP -LocalPort 7000,7001 -RemoteAddress LocalSubnet -Program "$PWD\ctr_native.exe" -Profile Any -Action Allow
   (With a port override, put that port in place of 7001.) A Block rule
   for the exe beats the Allow rule (Windows may add one, for example when
   its firewall prompt is cancelled). This must list nothing:
       Get-NetFirewallApplicationFilter -Program "$PWD\ctr_native.exe" | Get-NetFirewallRule | Where-Object Action -eq 'Block'
   Remove what it lists by adding | Remove-NetFirewallRule to it. Check
   again after the first start if Windows showed a firewall prompt. If
   this folder changes, remove the rule
   (Remove-NetFirewallRule -DisplayName "CTR arcade link") and add it again.
4. Same build and same disc. On both cabinets, in PowerShell in this folder:
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
  [CTR Native] Config groups from the file: link fullscreen render_scale texture_filter data_dir
  [CTR Native] arcade link: auto port 7001, 0 peers
(with lan set, followed by "[CTR Native] arcade link: lan <its value>").
These lines are on the console only, not in the log; the fullscreen window
may hide the console (Alt+Tab to it). "Config file: none" means there is no
arcade.cfg next to the exe (check for a hidden .txt extension), and the
cabinet would start unlinked. A config file with an error stops the game
with a message naming the file and, where there is one, the line. Startup
errors, such as that one or "arcade link requires a known build and
content identity.", are on the console only, not in the log. After a
double-click the console stays open on an error until Enter is pressed.
When the two cabinets have found each other (shortly after both show the
title), each logs, on the console and in Crash Team Racing.log,
  [CTR Native] arcade discovery: paired with <other cabinet IP>:7001 as cab1
(as cab2 on the other cabinet).

Solo race
---------
A player can race when the other cabinet is off or sits on its attract
title. After START the cabinet shows CONNECTING; after about 3 s without
an answer it shows WAITING FOR OTHER CABINET and PRESS START TO RACE
SOLO. START or CROSS (on a G29 also the throttle pedal) then starts a
one-player race against 7 bots: pick a character, a track and the laps,
then race. RESULTS offers RACE AGAIN and LOBBY; left alone for 30 s it
returns to the attract title. OTHER CABINET IS READY on RESULTS means a
player started the other cabinet (pressed START) during this solo
session. Choose LOBBY: if that player is still in their LOBBY, the two
cabinets link as before. Solo needs no setting: arcade.cfg has no key for
it.
