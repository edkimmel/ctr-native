# Packaging and the per-cabinet config file

How a cabinet build is packaged and configured. Decisions PK-1..PK-10. The
owner's step-by-step cabinet setup is "Per-cabinet setup" below.

## Decisions

**PK-1 Build.** v1 ships the tested `CTR_INTERNAL` Release build of
`ctr_native`. The autopilot and fault options stay opt-in command-line flags.
There is no non-internal target in v1.

**PK-2 Config file.** A `key = value` text file. The default is `arcade.cfg`
in the exe directory (`SDL_GetBasePath`). `--config <path>` selects another
file; a relative path resolves against the launch directory.
- If `SDL_GetBasePath` fails (returns NULL; startup then prints
  `SDL base path: (null)`), the default `arcade.cfg` is looked up in the
  launch directory instead, and a relative `data_dir` resolves against the
  launch directory too. This is the existing no-base-path fallback; it is
  not an error.
- No `--config` and no default file: exactly the behaviour without a config.
- A missing `--config` file, a file that cannot be opened or read (including
  a default `arcade.cfg` that exists but cannot be opened), or a malformed
  file is fatal. The message names the file and the line, and the process
  exits 1 through `NativeConsole_Return`. A cabinet must not come up silently
  unlinked.

**PK-3 Keys.** `data_dir`, `seat` (`cab1`|`cab2`), `port` (1-65535), `peer`
(`a.b.c.d:port`, repeatable up to 8), `fullscreen`
(`0`|`1`|`yes`|`no`|`true`|`false`). Keys and values are case-sensitive, as
on the command line. The grammar and the error classes are listed below.
The link group (`seat`, `port`, `peer`) is all-or-none, with exactly the
command line's rules.

**PK-4 Command-line overrides, per group.**
- Link: if argv names `--arcade-link`, `--arcade-link-port`,
  `--arcade-link-peer`, or `--arcade-link-preview`, the file's whole link
  group is ignored.
- Window mode: `--fullscreen` or `--windowed` overrides `fullscreen`.
- Data: `--data-dir <dir>` overrides `data_dir`.

Startup prints the loaded file (or `none (<default path> not found)`), the
groups taken from it, and the groups the command line overrode.

**PK-5 One link grammar.** The config module has no port or peer grammar of
its own.
- Each `seat`, `port`, and `peer` value is checked, at its line, by
  `NativeArcadeLinkOptions_ApplyArgs` itself. The parser runs over a
  synthetic argv in which the other two members are fixed good samples.
- The whole group is then checked the same way:
  `--arcade-link <seat> --arcade-link-port <port> --arcade-link-peer <peer>...`.
- The group reaches the link only through `NativeArcadeConfig_ApplyLink`. It
  fills `struct NativeArcadeLinkOptions` through the same parser, exactly as
  the flags do. Every later check in `main.c` then applies unchanged: it is
  rejected with replay options and with `--arcade-roster-proof`, and it
  needs a known build and content identity (PK-6).
- The config never reaches the match config, simulation or canonical state,
  replay, checkpoints, identity, or the topology lease. It reaches only the
  window mode, the link options, and the assets directory.
  `tests/native_arcade_config_isolation_test.cmake` pins this.

**PK-6 Data directory.** `data_dir` and `--data-dir` name the folder that
holds the user's own `ctr-u.bin`, or the extracted `BIGFILE.BIG` tree. It
plays the role `assets/` plays without a config.
- An unlinked run works from either. A linked run needs the disc image
  `ctr-u.bin`: the link fixture carries the content identity, the SHA-256
  of the open disc image (`NativeIdentity_Get`,
  `NativeDiscImage_GetContentIdentity`). With only the extracted files a
  linked cabinet stops at startup (exit 1) with
  `[CTR Native] arcade link requires a known build and content identity.`
- For a linked cabinet the folder holds only `ctr-u.bin`, with no extracted
  game files beside it. An extracted file on disk is read before the disc
  image (`NativeAssets_ReadBytes`; `platform/native_cd.c` keeps them as
  dev and modding overrides), but the content identity hashes only
  `ctr-u.bin`. So a leftover `BIGFILE.BIG` passes the handshake and the
  same-disc hash check and is still what the game runs, and different
  leftovers on the two cabinets desync the race instead of refusing the
  link.
- A relative path resolves against the exe directory. This holds for
  `--data-dir` too, so one relative value means the same thing in the file
  and on the command line.
- When a data directory is set, the base directory (cwd, the log file,
  `memcards/`) stays the exe directory. The folder is used as the assets
  directory, and the parent and grandparent search is skipped.
- A drive-relative value (`C:` or `C:dir`) or, on Windows, a root-relative
  one (`\dir` or `/dir`; a UNC `\\server\share` is fine) is fatal: it would
  resolve against the launch directory before startup enters the base
  directory and against the base directory after. The message names the
  value and where it came from, and asks for a full path or one relative to
  the exe folder (`NativeAssets_IsDriveOrRootRelativePath`).
- A folder with neither file (either case) is fatal. The message names the
  value, the resolved path, and where it came from. The full asset
  validation still runs after it.
- When no data directory is set, the search is unchanged.

The entry point is `NativeAssets_InitWithAssetDir(exeBase, assetDir, resolved, size)`.

**PK-7 Package script.** `tools/package-arcade.ps1` (Windows PowerShell 5.1).
See "Running the package script" below.

**PK-8 Templates.** `tools/package/cab1.cfg` and `cab2.cfg`:

| Template | seat | port | peer |
| --- | --- | --- | --- |
| `cab1.cfg` | `cab1` | 7001 | `192.168.1.102:7002` |
| `cab2.cfg` | `cab2` | 7002 | `192.168.1.101:7001` |

- The ports are the default ports of the loopback gate
  `tools/arcade-link-launch-check.ps1` and the ports the package smoke gate
  runs on. The ctest `arcade_link_launch` passes 7101 and 7102 instead, so
  the two live gates never share a port.
- The IPs are placeholders. A comment says to edit each to the other
  cabinet's fixed IP.
- Both templates set `data_dir = C:\ctr-data` and `fullscreen = 1`.
- `tools/package/README.txt` is the operator card: data, per-cabinet setup,
  the PK-9 firewall rule, starting, and the same-build and same-disc hash
  checks. It follows "Per-cabinet setup" below.
- `native_arcade_config_unit` parses both templates with the real parser and
  checks the resulting link options.

**PK-9 Firewall rule.** A default the owner may change. Each cabinet
allows inbound UDP on its own link port only, only for the packaged
`ctr_native.exe` (`-Program`), and only from the other cabinet's fixed IP
(`-RemoteAddress`). The link's only partner is the configured `peer`, so
this is the narrowest rule the link needs. A port-only rule also works, but
it admits any program and any sender on any network profile. The rule leaves
`-Profile` at its default (all profiles), since a cabinet LAN on a dumb
switch may not be classified as a private network. The exact commands are
in "Per-cabinet setup", step 4.

**PK-10 Per-cabinet files.** A default the owner may change. `arcade.cfg`,
`memcards\`, and `Crash Team Racing.log` in the package folder belong to
one cabinet. Any sync of the folder between cabinets (docs/HANDOFF.md: CAB2
receives the bundle through the fleet rsync path) excludes them. If a sync
copied them anyway, the receiving cabinet deletes the copied `memcards\`
and log and redoes "Per-cabinet setup" step 3, since it would otherwise run
the other cabinet's seat and peer. A `memcards\` save is never copied to a
cabinet.

## Config grammar

- One `key = value` per line. Keys are exactly `data_dir`, `seat`, `port`,
  `peer`, `fullscreen` (lowercase).
- Whitespace (space, tab) around the key and the value is trimmed.
- The value is the rest of the line after the first `=`. It may contain
  spaces and `=`, and it takes no quotes. A trailing `# comment` is part of
  the value, and so makes it invalid (for `data_dir`, a wrong path:
  "Per-cabinet setup", step 3).
- Blank lines, and lines whose first non-blank character is `#` or `;`, are
  comments.
- CRLF or LF line ends are accepted (and a CR that ends the file). Any
  other CR, such as old Mac CR-only line ends or a stray CR inside a line,
  is a syntax error at its line. A UTF-8 BOM is accepted at the start of the
  file only. A UTF-16 file fails on its NUL bytes, and the message says to
  save the file as UTF-8 or ANSI text.

Errors are reported as `config file <path> line <n>: <reason>`, except the
file-size error, which has no line: `config file <path>: <reason>`.

| Error | Line reported |
| --- | --- |
| file over 16384 bytes | none (no `line <n>` in the message) |
| line over 1023 characters (line end excluded) | that line |
| NUL byte (for example a UTF-16 file) | that line |
| no `=`, an empty key, or a bare CR | that line |
| unknown key | that line |
| non-repeatable key given twice | the second line |
| empty value | that line |
| bad value | that line |
| a ninth `peer` | that line |
| incomplete link group | the first link line |

Code: `platform/native_arcade_config.c` and
`include/platform/native_arcade_config.h`. This is the pure library
`ctr_native_arcade_config`, which links only
`ctr_native_arcade_link_options`. `main.c` reads the file and is the only
caller.

## Before packaging

The package script builds and packages; it runs no tests. At the commit
being packaged, with a clean tree, the full Debug and Release suites must
pass first, including the `live` gates. Those run against each
configuration's own `ctr_native.exe`, so the Release run gates the exe that
ships:

```sh
cmake --preset windows-msvc-x86
cmake --build build-msvc-x86 --config Debug
ctest --test-dir build-msvc-x86 -C Debug --output-on-failure
cmake --build build-msvc-x86 --config Release
ctest --test-dir build-msvc-x86 -C Release --output-on-failure
```

Do not add `-LE live` here: the fast suite is for iteration, not release.
Package only a commit whose two runs both pass in full.

## Running the package script

From the repository root, with a clean tree (commit first), after the
suites above pass:

```sh
powershell -NoProfile -ExecutionPolicy Bypass -File tools/package-arcade.ps1
```

The script:
1. Refuses (exit 1) unless
   `git status --porcelain --untracked-files=all --ignore-submodules=none`
   is empty. The flags are explicit so no user git config can hide a file.
   Untracked files count; ignored files do not.
2. Runs `cmake --preset windows-msvc-x86` and
   `cmake --build build-msvc-x86 --config Release --target ctr_native`, then
   checks the tree is still clean.
3. Requires `ctr_native.exe --version` to print exactly
   `CTR Native <CTR_NATIVE_VERSION> (<git rev-parse --short=12 HEAD>)`, so a
   `-dirty` or `unknown` build is refused.
4. Recreates `build-msvc-x86\package\ctr-arcade-<short12>\`. There is no
   output-path parameter. The script refuses any destination outside
   `build-msvc-x86\package\`, and anything under `C:\arcade`. Before any
   delete it refuses if `build-msvc-x86`, `build-msvc-x86\package`, or the
   destination is a junction or symbolic link (a reparse point).
5. Copies the Release `ctr_native.exe` and the templates `cab1.cfg`,
   `cab2.cfg`, and `README.txt` verbatim.
6. Writes `MANIFEST.txt` (ASCII, CRLF). It holds the package name, the
   version, the full and short commit hash, the configuration `Release`, and
   the size in bytes and SHA-256 (uppercase hex, as `Get-FileHash` prints)
   of every other file. It adds a note that both cabinets must show the same
   `ctr_native.exe` SHA-256.
7. Runs the guard on the finished folder. On failure it deletes the folder
   and exits 1.
8. Prints the package path, the exe SHA-256 and size, and the file list.

The output is under the ignored build tree. It is never committed and never
written to `C:\arcade`: the owner deploys packages there.

## Retail-data guard

`tools/package-arcade.ps1 -CheckFolder <dir>` runs only the guard on an
existing folder (exit 0 pass, 1 fail) and never changes the folder.

A folder fails when any of these holds:
- Its file set is not exactly `ctr_native.exe`, `cab1.cfg`, `cab2.cfg`,
  `README.txt`, `MANIFEST.txt`. Names are case-sensitive, and a missing
  file fails too.
- It holds any subdirectory.
- A file has the hidden or system attribute (hidden files are listed too).
- A file name has a retail or BIOS extension: `.bin .cue .iso .img .chd
  .ecm .pbp .big .hwl .str .xa .xnf .vag .mcd .mcr .sav .srm .bmp .png`.
- A file name contains `bios` or `scph` (case-insensitive).
- A non-exe file is larger than 64 KiB.
- The exe is 32 MiB or larger.

`package_arcade_content_guard` (ctest) builds fabricated folders of tiny
dummy files under `build-msvc-x86/package_guard_test/`. It checks that:
- the exact allowlist passes;
- each of these fails with its reason: `ctr-u.bin`, `SCPH1001.BIN`,
  `BIGFILE.BIG`, an unknown file, a subdirectory, an oversize `cab1.cfg`,
  `CTR_NATIVE.EXE` in place of `ctr_native.exe`, a hidden extra file, a
  hidden `README.txt`, an exe of 32 MiB + 1 byte (made with
  `fsutil file createnew`, which needs no elevation; the case is skipped
  with a status line only if fsutil fails), a missing `MANIFEST.txt`, and a
  missing folder.

## Package smoke gate

`tools/package-arcade-smoke.ps1` proves that a packaged `ctr_native.exe`
runs a two-process loopback lockstep race driven by the package's own config
files. It runs the `arcade_link_launch` gate
(`tools/arcade-link-launch-check.ps1`) on the packaged exe, with
`-Cab1Config` and `-Cab2Config`: each run gets `--config <file>` in place of
the three link options, and its stdout must show that file loaded, the link
group taken from it, and no group overridden.

Steps:
1. Runs the retail-data guard (`-CheckFolder`) on the package folder, and
   checks every `MANIFEST.txt` size and SHA-256 against the files.
2. Copies the package to a fresh `<output>\run\` and re-checks the copies
   against the MANIFEST. The game writes its log and `memcards\` next to the
   exe, so the package folder itself is never run. The copy must hold no
   `memcards\` before the gate starts: it runs as a fresh cabinet with no
   memcard save, so no game options were ever loaded from one.
3. Derives `<output>\cab1.loopback.cfg` and `cab2.loopback.cfg` from the
   package's `cab1.cfg` and `cab2.cfg`. Only three values change: the peer
   IP (to `127.0.0.1`, ports kept), `data_dir` (to the folder holding the
   disc image), and `fullscreen` (to `0`). Every other line must be
   unchanged, and seat, port, and peer port must equal the gate's
   (cab1 7001 peer :7002, cab2 7002 peer :7001).
4. Runs the gate in `<output>\gate\`. After a pass it also requires both
   stdouts to show the groups `link fullscreen data_dir` from the file and a
   windowed window mode, and re-checks the run copy and the package against
   the MANIFEST.
5. Prints the exe SHA-256 and size and `package smoke: PASS`.

Two modes:
- `-PackageDirectory <dir>` tests an existing package folder, which is only
  read.
- `-StageExecutable <exe>` first stages a test package into
  `<output>\package\` with `tools/package-arcade.ps1 -StageExecutable <exe>
  -StageDirectory <dir>`. The stage uses the same staging function and guard
  as the real package, without the clean-tree, build, and `--version`
  checks. Its MANIFEST names the package `ctr-arcade-staged` and says it is
  a staged test package that must never be deployed. The stage destination
  must resolve under `build-msvc-x86\`, and an existing destination must
  hold only package files.

`-OutputDirectory` must resolve under `build-msvc-x86\`. `-AssetsFile`
defaults to `assets\ctr-u.bin`. The smoke skips (exit 77) like the other
live gates: without the disc image, without a display, with a non-internal
build, or with an unknown build identity (a build from a dirty tree). A skip
is not a pass.

The ctest `package_arcade_smoke` (labels `live` and `live-package`; not
`RUN_SERIAL`, so it may run in parallel with the other live tests) runs the
stage mode on each configuration's own `ctr_native.exe`, writing under
`build-msvc-x86\package_smoke\<config>`. It runs on the package's ports,
7001 and 7002; `arcade_link_launch` uses 7101 and 7102, so the two never
share a port. Run it alone with
`ctest --test-dir build-msvc-x86 -C Debug -L live-package --output-on-failure`.
`package_arcade_stage` (not live) checks the stage mode and the argument
checks of both scripts with a dummy exe.

To smoke-test a real package folder, with a clean tree:

```sh
powershell -NoProfile -ExecutionPolicy Bypass -File tools/package-arcade.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/package-arcade-smoke.ps1 -PackageDirectory build-msvc-x86\package\ctr-arcade-<short12> -OutputDirectory build-msvc-x86\package_smoke\real
```

## Fresh cabinet: game options

A cabinet needs no `memcards\` folder and no memcard save. A package is
deployed without one, and the smoke gate runs its copy that way.

Retail marks the game options loaded (`sdata->boolHasLoadedOptions`) only
when it loads them from a memcard save, and the race setup's Launch
refuses to start a race until they are. So on a cabinet with no save,
`MainArcadeRaceSetup_Arm` marks them loaded itself on the first linked
race: it sets that one flag and nothing else
(`game/MAIN/MainArcadeRaceSetupCore.c`, `MainArcadeRaceSetupCore_Arm`).
It does not run the retail options load, which without a save would load
all-zero options and mute the audio. The cabinet keeps its live volumes,
stereo mode, `data.rwd`, and vibration settings. It logs, to the console
and to `Crash Team Racing.log` next to the exe:

```text
[CTR Native] arcade race setup: no memcard options loaded; marked the options loaded (live settings kept)
```

The flag then stays set for the rest of the process (Disarm does not
restore it), so later races and rematches log nothing more. On a cabinet
that loaded a save the flag is already set: nothing is written or logged.
See docs/ROSTER_MILESTONE.md (the MainArcadeRaceSetup_Arm seam).

## Per-cabinet setup

The complete steps for one cabinet, from a package folder to a running
linked cabinet. Do them on both cabinets. Only the template, the port,
and the peer differ:

| | Cabinet 1 | Cabinet 2 |
| --- | --- | --- |
| template | `cab1.cfg` | `cab2.cfg` |
| `seat` | `cab1` | `cab2` |
| `port` (this cabinet's inbound UDP port) | `7001` | `7002` |
| `peer` | `<cabinet 2 IP>:7002` | `<cabinet 1 IP>:7001` |

`<cabinet 1 IP>` and `<cabinet 2 IP>` stand for that cabinet's fixed IP
address. Replace the whole placeholder, angle brackets included: for
example `192.168.1.102:7002`, and `-RemoteAddress 192.168.1.102` in step 4.

The commands are PowerShell. They use `$pkg` for the folder the package
was copied to; `C:\Arcade\games\ctr-native` is the example (the fleet
location named in docs/HANDOFF.md). Set it first in each PowerShell
window:

```powershell
$pkg = 'C:\Arcade\games\ctr-native'
```

`arcade.cfg`, `memcards\`, and `Crash Team Racing.log` in `$pkg` belong to
this cabinet (PK-10). Exclude them from any sync of the folder to the
other cabinet (such as the fleet rsync path). If a sync copied them
anyway, delete the copied `memcards\` and `Crash Team Racing.log` on the
receiving cabinet and redo step 3. Never copy a `memcards\` save to a
cabinet.

1. **Copy the package.** Copy the contents of the package folder,
   `build-msvc-x86\package\ctr-arcade-<short12>\` (made by
   `tools/package-arcade.ps1`), to `$pkg` on the cabinet, so that
   `$pkg\ctr_native.exe` exists (not a nested
   `$pkg\ctr-arcade-<short12>\` folder). The folder must be writable: the
   game writes `Crash Team Racing.log` (recreated at each start) and
   `memcards\` next to `ctr_native.exe`. No `memcards\` is needed ("Fresh
   cabinet: game options").
2. **Game data.** Put your own raw NTSC-U disc image (MODE2/2352), named
   `ctr-u.bin`, in `C:\ctr-data`, the folder both templates name in
   `data_dir`. A linked cabinet requires `ctr-u.bin`: the link identifies
   the game content by the disc image's SHA-256, so with only the extracted
   files (`BIGFILE.BIG` and the rest) it stops at startup with
   `[CTR Native] arcade link requires a known build and content identity.`
   The extracted files are enough only for an unlinked run. For a linked
   cabinet the folder holds only `ctr-u.bin`: delete any extracted game
   files beside it. They would be read in place of the disc image without
   being part of its hash, so the link checks pass and the cabinets can
   desync (PK-6). Both cabinets need the same disc image (step 5). The
   package holds no game data (PK-6).
3. **Config file.** Copy the cabinet's template to `arcade.cfg` next to
   the exe. Cabinet 1:

   ```powershell
   Copy-Item "$pkg\cab1.cfg" "$pkg\arcade.cfg"
   ```

   Cabinet 2:

   ```powershell
   Copy-Item "$pkg\cab2.cfg" "$pkg\arcade.cfg"
   ```

   Then edit `$pkg\arcade.cfg` (save it as UTF-8 or ANSI text, not
   UTF-16):
   - `peer`: always. Replace the placeholder IP with the other cabinet's
     fixed IP and keep the port: cabinet 1
     `peer = <cabinet 2 IP>:7002`, cabinet 2 `peer = <cabinet 1 IP>:7001`.
   - `data_dir`: only if the data is not in `C:\ctr-data`. Use a full path,
     or a path relative to `$pkg`; `C:ctr-data` and `\ctr-data` are
     refused (PK-6).
   - `seat` and `port`: keep the template's values. The two cabinets need
     different seats, and each `peer` port must be the other cabinet's
     `port`.
   - `fullscreen`: keep `1` on a cabinet (`0` is windowed, for testing).

   There are no other keys (PK-3). A comment goes on its own line: a
   `# comment` after a value becomes part of the value. After `seat`,
   `port`, `peer`, or `fullscreen` that is a bad value, and the game stops
   with the file and line. After `data_dir` it becomes part of the path, and
   the game stops with `data directory ... does not hold ctr-u.bin or
   BIGFILE.BIG.` Either way the cabinet does not start.
4. **Firewall (PK-9).** In an elevated PowerShell (Run as administrator),
   after setting `$pkg`, allow inbound UDP on this cabinet's port for the
   packaged exe, from the other cabinet only. Cabinet 1:

   ```powershell
   New-NetFirewallRule -DisplayName 'CTR arcade link' -Direction Inbound -Action Allow -Protocol UDP -LocalPort 7001 -RemoteAddress <cabinet 2 IP> -Program "$pkg\ctr_native.exe"
   ```

   Cabinet 2:

   ```powershell
   New-NetFirewallRule -DisplayName 'CTR arcade link' -Direction Inbound -Action Allow -Protocol UDP -LocalPort 7002 -RemoteAddress <cabinet 1 IP> -Program "$pkg\ctr_native.exe"
   ```

   A Block rule beats every Allow rule. Windows may have added one for
   this exe (for example when its firewall prompt was cancelled). List the
   Block rules that name the exe; the list must be empty:

   ```powershell
   Get-NetFirewallApplicationFilter -Program "$pkg\ctr_native.exe" | Get-NetFirewallRule | Where-Object Action -eq 'Block'
   ```

   Remove any it lists by adding `| Remove-NetFirewallRule` to that
   command. Check again after the first start if Windows showed a firewall
   prompt.

   If the package folder or the other cabinet's IP changes, remove the
   rule (`Remove-NetFirewallRule -DisplayName 'CTR arcade link'`) and add
   it again.
5. **Same build and same disc.** On both cabinets:

   ```powershell
   Get-FileHash "$pkg\ctr_native.exe" -Algorithm SHA256
   Get-FileHash 'C:\ctr-data\ctr-u.bin' -Algorithm SHA256
   ```

   (Use the cabinet's own `data_dir` in place of `C:\ctr-data`.) The exe
   hash must equal the `ctr_native.exe` line in `$pkg\MANIFEST.txt`, and so
   be the same on both cabinets. The `ctr-u.bin` hash must be the same on
   both cabinets. The link handshake refuses two different builds or two
   different disc images: both show `LINK REFUSED: SETTINGS DO NOT MATCH`.
   Neither check sees extracted files, so also confirm that the
   `data_dir` folder holds only `ctr-u.bin` on both cabinets (step 2):

   ```powershell
   Get-ChildItem 'C:\ctr-data' -Force
   ```
6. **Start.** Double-click `ctr_native.exe` in `$pkg`, or run it with no
   arguments with `$pkg` as the working directory:

   ```powershell
   Set-Location $pkg
   .\ctr_native.exe
   ```

   It reads `arcade.cfg` from its own folder (PK-2) and then works in that
   folder, whatever the launch directory. Among its first console lines
   it must show (here with the example `$pkg`):

   ```text
   [CTR Native] Config file: C:\Arcade\games\ctr-native\arcade.cfg
   [CTR Native] Config groups from the file: link fullscreen data_dir
   [CTR Native] Local window mode: fullscreen
   ```

   These lines are printed before the log file opens, so they are on the
   console only, not in `Crash Team Racing.log`. The fullscreen window may
   hide the console; switch to it (Alt+Tab) to read them.
   `Config file: none (... not found)` means there is no `arcade.cfg` next
   to the exe (check for a hidden `.txt` extension), and the cabinet would
   start unlinked. An error in the file stops the game with a message
   naming the file and, where there is one, the line. Startup errors, such
   as that one or `arcade link requires a known build and content
   identity.`, are printed on the console only, not in the log. After a
   double-click the console stays open on an error until Enter is pressed.

## Template line ends

`.gitattributes` checks out `tools/package/*.cfg` and `*.txt` with CRLF
line ends on every machine, whatever `core.autocrlf` says, so every checkout
packages the same template bytes and the MANIFEST hashes do not depend on
the packager's git settings.

## Notes

- A malformed display flag (for example a bad `--render-scale`) still falls
  back to 1x windowed, as before. The fallback also drops the file's
  `fullscreen`.
- A cabinet whose `arcade.cfg` sets the link group is a link cabinet. Replay
  options and `--arcade-roster-proof` are then rejected, exactly as with
  `--arcade-link`, and the rejection message adds
  `(link group from config file <path>)`. To run those on such a machine,
  pass `--config` with an empty file.
- `ctr_native_config_startup` (ctest, not live) runs the real exe with
  fabricated config files under `build-msvc-x86/ctr_native_config_startup/`
  and checks exit code 1 and the message for: a missing `--config` file, an
  unknown key (with its line), a file over 16 KiB, a UTF-16 file, a config
  link group with `--replay` and with `--arcade-roster-proof`, `--data-dir`
  naming an empty folder, a drive-relative data directory (from the flag
  and from the file), and `--config` twice.
  Every case stops before the assets and `Platform_Init`, so it needs no
  game data and no display.
