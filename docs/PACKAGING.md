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

**PK-3 Keys.** `data_dir`, `seat` (`cab1`|`cab2`|`auto`), `port`
(1-65535, optional), `peer` (`a.b.c.d:port`, repeatable up to 8, optional),
`group` (1-32 letters, digits, `.`, `_`, or `-`, not starting with `-`;
optional), `lan` (`a.b.c.d/n`, an IPv4 subnet with a prefix of 8-30 and
the host bits zero, as `--arcade-link-lan`; optional, docs/DISCOVERY_MILESTONE.md
DISC-19), `fullscreen` (`0`|`1`|`yes`|`no`|`true`|`false`), `render_scale`
(`1`|`2`|`3`|`4`|`6`|`8`, as `--render-scale`), `texture_filter`
(`nearest`|`bilinear`, as `--texture-filter`). Keys and values are
case-sensitive, as on the command line. The grammar and the error classes
are listed below. The link group (`seat`, `port`, `peer`, `group`, `lan`)
follows exactly the command line's rules (PK-5, docs/DISCOVERY_MILESTONE.md
DISC-11): `seat` turns the link on; with a `peer` it is static mode, without
one discovery mode. `lan` pins the link to one subnet, in either mode: the
same value on every cabinet (a subnet, unlike an interface address, is the
same on all of them). It exists because Windows sends the limited broadcast
255.255.255.255 through one network card only on a machine with several,
and because without it the beacons also go to every other network the
cabinet is on (the fleet cabinets have the wired arcade switch at
192.168.1.x and a second network). `render_scale` and `texture_filter`
are host-local presentation of one cabinet: the two cabinets may differ in them, and they
never reach the match config, simulation identity, replay, checkpoints, the
link, or canonical state.

**PK-4 Command-line overrides, per group.**
- Link: if argv names `--arcade-link`, `--arcade-link-port`,
  `--arcade-link-peer`, `--arcade-link-group`, `--arcade-link-lan`, or
  `--arcade-link-preview`, the file's whole link group is ignored (the
  file's `lan` included). The discovery test flags
  `--arcade-discovery-port` and `--arcade-discovery-target` are not link
  group options (DISC-18): argv naming them keeps the file's group, so a
  test can run a shipped `arcade.cfg` with them.
- Window mode: `--fullscreen` or `--windowed` overrides `fullscreen`.
- Display, per key: `--render-scale` overrides `render_scale`, and
  `--texture-filter` overrides `texture_filter`. A bad value in the file is
  fatal with its line, like any bad value; a bad display flag still falls
  back to 1x windowed (see "Notes").
- Data: `--data-dir <dir>` overrides `data_dir`.

Startup prints the loaded file (or `none (<default path> not found)`), the
groups taken from it, and the groups the command line overrode.

**PK-5 One link grammar.** The config module has no seat, port, peer,
group, or lan grammar of its own.
- Each `seat`, `port`, `peer`, `group`, and `lan` value is checked, at its
  line, by `NativeArcadeLinkOptions_ApplyArgs` itself, over a synthetic argv
  that is a valid group for every good value: `seat`, `group`, and `lan` in
  a discovery-mode sample (a sample seat, no peer), `port` and `peer` in a
  static-mode sample (a sample seat, port, and peer). The `lan` grammar is
  the discovery core's `NativeArcadeDiscovery_ParseLan` (DISC-19), which the
  options parser calls: `a.b.c.d/n`, octets 0-255, prefix 8-30, host bits
  zero, nothing else (no spaces inside, no comment after it).
- The whole group is then checked the same way, as
  `--arcade-link <seat> --arcade-link-port <port> --arcade-link-group <group> --arcade-link-lan <lan> --arcade-link-peer <peer>...`
  with only the keys the file sets. `ApplyArgs` applies the DISC-11 rules to
  the file and to argv alike:
  - `seat` is the one key that turns the link on. `port`, `peer`, `group`,
    or `lan` without `seat` is an incomplete link group.
  - `lan` is optional in both modes below.
  - Static mode: at least one `peer`. It needs `seat` `cab1` or `cab2` and
    a `port`, and takes no `group`. Discovery is off: no discovery socket,
    no beacon.
  - Discovery mode: no `peer`. `seat` is `auto`, `cab1`, or `cab2` (`cab1`
    and `cab2` become the seat preference of the election; two cabinets
    that prefer the same seat never pair). `port` is optional (default
    7001), and so is `group` (default `ctr-native`).
  - So a `peer` with `seat = auto`, a `peer` without `port`, and a `peer`
    with `group` are incomplete link groups too.
- After the file's group is applied (or argv's, PK-4), `main.c` checks the
  merged options with `NativeArcadeLinkOptions_ValidateMerged`:
  `--arcade-discovery-port` or `--arcade-discovery-target` without
  discovery mode is an error, and in discovery mode the link port may not
  equal the discovery port (7000 unless `--arcade-discovery-port` sets
  another: the discovery socket holds it for the whole run). With a `lan`
  (DISC-19) every `peer` and every `--arcade-discovery-target` must lie
  inside it. Any of these stops the game (exit 1) with
  `invalid arcade-link option; ...`, with no config line: `port = 7000`, or
  a `peer` outside the `lan`, passes the file check and is refused here.
- With a `lan`, in discovery mode, the cabinet beacons only to the lan's
  directed broadcast (192.168.1.0/24 gives 192.168.1.255), and only while
  one of its network cards (up, not loopback) is on exactly the lan's
  subnet: the `lan` must equal the arcade card's subnet, the same network
  and the same prefix (a card 192.168.1.11 with mask 255.255.255.0 is on
  192.168.1.0/24). It never sends 255.255.255.255 or another card's
  broadcast, and it drops every beacon from a source outside the lan.
  Without a card on the lan's subnet it sends nothing, logs one line on
  entering that state, and looks again at every interface refresh (every
  10 s, held while a race runs); it never falls back to the other cards.
  The line says why: `arcade discovery: interface <address>/<prefix> is in
  lan <lan> but its subnet differs; not beaconing, retrying` for a card
  whose address is inside the lan on another prefix (fix the `lan` or the
  card's mask so they match; a lan broadcast on such a card would not
  reach the switch or would leave through the gateway), `arcade discovery:
  interface list unavailable; not beaconing, retrying` when Windows does
  not return its card list, and otherwise `arcade discovery: no network
  interface in lan <lan>; not beaconing, retrying`. When a card on the
  lan's subnet appears it logs `arcade discovery: lan <lan> on interface
  <address>; beaconing`. `main.c` prints `arcade link: lan <lan>`
  at startup, after the `arcade link: <seat> port <p>, <n> peers` line.
- Without a `lan` nothing of the above changes.
- A static cabinet and a discovery cabinet never link (the static one does
  not beacon): both cabinets of a pair use the same mode.
- The group reaches the link only through `NativeArcadeConfig_ApplyLink`. It
  fills `struct NativeArcadeLinkOptions` through the same parser, exactly as
  the flags do. Every later check in `main.c` then applies unchanged: it is
  rejected with replay options and with `--arcade-roster-proof`, and it
  needs a known build and content identity (PK-6).
- `render_scale` and `texture_filter` have no grammar of their own either.
  Each value is checked, at its line, by `NativeDisplayConfig_ApplyArgs`
  over a synthetic `--render-scale=<value>` or `--texture-filter=<value>`,
  so the file accepts what the flag accepts (a value over 7 or 15
  characters excepted: a bad value). They reach the display config only
  through `NativeArcadeConfig_ApplyDisplay`, the same parser, right after
  `NativeDisplayConfig_SetDefaults` and before the display flags.
- The config never reaches the match config, simulation or canonical state,
  replay, checkpoints, identity, or the topology lease. It reaches only the
  display config (window mode, render scale, texture filter), the link
  options, and the assets directory.
  `tests/native_arcade_config_isolation_test.cmake` pins this, and that no
  match config, replay, canonical, checkpoint, lockstep, netplay,
  arcade-link, or arcade race source names the display keys.

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

**PK-8 Template.** One file, `tools/package/arcade.cfg`, for every cabinet
(DISC-11, DISC-S5):
- `seat = auto` (discovery mode: the cabinets find each other and elect
  their seats) and no active `port`, `peer`, `group`, or `lan` line.
  Comments show the optional overrides: `# port = 7001` (the default link
  port; it must differ from the discovery port 7000), `# group = <name>`
  (keeps two installations on one LAN apart; both cabinets need the same
  one), `# lan = 192.168.1.0/24` (DISC-19: pins discovery to the arcade
  switch on a cabinet with two network cards; both cabinets need the same
  value), `seat = cab1` or `cab2` as a fixed-seat override, and a static override
  (`seat = cab1|cab2`, `port`, and `peer = a.b.c.d:port`, on BOTH cabinets:
  a pair of one static and one discovery cabinet never links).
- `data_dir = C:\ctr-data`, `fullscreen = 1`, `render_scale = 8`, and
  `texture_filter = bilinear`.
- It ships under the name the exe reads by default (PK-2), so a fresh
  install needs no copy step and no per-cabinet network edit.
- `tools/package/README.txt` is the operator card: data, per-cabinet setup
  (with the optional overrides), the PK-9 firewall rule, starting, the
  same-build and same-disc hash checks, and the solo race. It follows
  "Per-cabinet setup" and "Solo race" below.
- `native_arcade_config_unit` parses it with the real parser and checks the
  resulting link options (on, discovery mode, seat auto, link port 7001, no
  peer, no group, no lan, and the post-merge check passes) and display config. The
  package smoke gate checks the packaged copy's keys and runs the packaged
  exe with it.

**PK-9 Firewall rule.** A default the owner may change (DISC-17). Every
cabinet has the same one inbound rule: UDP on the discovery port 7000 and
the link port 7001, only for the packaged `ctr_native.exe` (`-Program`),
only from the local subnet (`-RemoteAddress LocalSubnet`), on every network
profile (`-Profile Any`):

```powershell
New-NetFirewallRule -DisplayName 'CTR arcade link' -Direction Inbound -Protocol UDP -LocalPort 7000,7001 -RemoteAddress LocalSubnet -Program "$pkg\ctr_native.exe" -Profile Any -Action Allow
```

- The peer's address is found at run time, so the rule cannot name it;
  `LocalSubnet` keeps every other network out.
- `-Profile Any`: a cabinet LAN on a dumb switch may be classified Public,
  and a rule scoped to Private would drop every beacon.
- Outbound broadcast needs no rule.
- A cabinet with a `port` override puts that port in place of 7001. A
  static pair (PK-5) needs only its link port; the same rule covers it.
- The exact steps are in "Per-cabinet setup", step 4.

**PK-10 Per-cabinet files.** A default the owner may change. `arcade.cfg`,
`memcards\`, and `Crash Team Racing.log` in the package folder belong to
one cabinet. The shipped `arcade.cfg` is the same on every cabinet, but an
edited one (`data_dir`, a `seat`, `group`, `lan`, or `port` override, a static
peer, or the display keys) is that cabinet's alone. Any sync of the folder
between cabinets (docs/HANDOFF.md: CAB2 receives the bundle through the
fleet rsync path) excludes them. If a sync copied them anyway, the receiving
cabinet deletes the copied `memcards\` and log and redoes "Per-cabinet
setup" step 3, since it would otherwise run the other cabinet's settings
(with a fixed seat or a static peer, the other cabinet's seat). A
`memcards\` save is never copied to a cabinet. Extracting a new package
over an installed folder overwrites `arcade.cfg` with the shipped one: keep
a copy of an edited one and put it back.

## Config grammar

- One `key = value` per line. Keys are exactly `data_dir`, `seat`, `port`,
  `peer`, `group`, `lan`, `fullscreen`, `render_scale`, `texture_filter`
  (lowercase). Only `peer` may repeat.
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
| incomplete link group (PK-5: `port`, `peer`, `group`, or `lan` without `seat`; a `peer` with `seat = auto`, without `port`, or with `group`) | the first link line |

A discovery-mode link port equal to the discovery port (`port = 7000`)
passes the file check; `main.c` refuses it after the merge (PK-5), with no
line.

Code: `platform/native_arcade_config.c` and
`include/platform/native_arcade_config.h`. This is the pure library
`ctr_native_arcade_config`, which links only
`ctr_native_arcade_link_options` and `ctr_native_display_config`. `main.c`
reads the file and is the only caller.

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
5. Copies the Release `ctr_native.exe`, the template `arcade.cfg`, and
   `README.txt` verbatim.
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
- Its file set is not exactly `ctr_native.exe`, `arcade.cfg`,
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
  `BIGFILE.BIG`, an unknown file, a subdirectory, an oversize `arcade.cfg`,
  `CTR_NATIVE.EXE` in place of `ctr_native.exe`, a hidden extra file, a
  hidden `README.txt`, an exe of 32 MiB + 1 byte (made with
  `fsutil file createnew`, which needs no elevation; the case is skipped
  with a status line only if fsutil fails), a missing `MANIFEST.txt`, and a
  missing folder.

## Package smoke gate

`tools/package-arcade-smoke.ps1` proves that a packaged `ctr_native.exe`
runs a two-process loopback lockstep race driven by the package's own
`arcade.cfg`, in discovery mode (DISC-S5). It runs the
`arcade_discovery_link` gate (`tools/arcade-discovery-link-check.ps1`) on
the packaged exe in its config mode, with `-ConfigA` and `-ConfigB`: each
run gets `--config <file>` in place of `--arcade-link auto` and the link
port, and its stdout must show that file loaded, the link group taken from
it, no group overridden, and `arcade link: auto port <its port>, 0 peers`.

Why this gate: the package ships one `arcade.cfg` for every cabinet, with
`seat = auto`, so the smoke proves the packaged exe plus that file to one
linked race: the two runs find each other, elect their seats, select, race,
and exit. The static-seat LR-16 scenario (three races: the finish, the
desync, the peer drop) stays covered by `arcade_link_launch`
(`tools/arcade-link-launch-check.ps1`) on the build-tree exe.

Steps:
1. Runs the retail-data guard (`-CheckFolder`) on the package folder, and
   checks every `MANIFEST.txt` size and SHA-256 against the files.
2. Copies the package to a fresh `<output>\run\` and re-checks the copies
   against the MANIFEST. The game writes its log and `memcards\` next to the
   exe, so the package folder itself is never run. The copy must hold no
   `memcards\` before the gate starts: it runs as a fresh cabinet with no
   memcard save, so no game options were ever loaded from one.
3. Checks the package's `arcade.cfg`: `data_dir`, `seat`, `fullscreen`,
   `render_scale`, and `texture_filter` set exactly once each, `seat = auto`,
   `texture_filter = bilinear`, and no other key set (`port`, `peer`,
   `group`, and `lan` are commented-out examples only).
4. Derives `<output>\a.loopback.cfg` and `b.loopback.cfg` from it. Only
   three values change: `data_dir` (to the folder holding the disc image),
   `fullscreen` (to `0`), and `render_scale` (to `1`: two cabinets at 8x on
   one test PC is needless load). One line is added after the seat line,
   `port = 7001` (a) or `port = 7002` (b): two processes on one host need
   distinct link ports. Every other line must be unchanged (`seat` stays
   `auto`, `texture_filter` stays `bilinear`, no peer).
5. Runs the gate in `<output>\gate\` with `-LinkPortA 7001 -LinkPortB 7002
   -DiscoveryPortA 7003 -DiscoveryPortB 7004`. The discovery flags are
   test flags, not `arcade.cfg` keys (DISC-18), so they go on the command
   line: a `--arcade-discovery-port 7003 --arcade-discovery-target
   127.0.0.1:7004`, b `--arcade-discovery-port 7004
   --arcade-discovery-target 127.0.0.1:7003`. The election makes a (the
   lower link port, 7001) cab1 and b cab2; each run must log exactly one
   `paired with 127.0.0.1:<the other's link port> as cab<n>` line. After a
   pass the smoke also requires both stdouts to show the groups
   `link fullscreen render_scale texture_filter data_dir` from the file, a
   1x render scale, a windowed window mode, and a bilinear texture filter,
   and re-checks the run copy and the package against the MANIFEST.
6. Prints the exe SHA-256 and size and `package smoke: PASS`.

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
  hold only package files, or files its own `MANIFEST.txt` lists when that
  MANIFEST marks it `ctr-arcade-staged` (a stage written with an older
  package file set, such as the per-cabinet templates before DISC-S5), and
  no subdirectory.

`-OutputDirectory` must resolve under `build-msvc-x86\`. `-AssetsFile`
defaults to `assets\ctr-u.bin`. The smoke skips (exit 77) like the other
live gates: without the disc image, without a display, with a non-internal
build, or with an unknown build identity (a build from a dirty tree). A skip
is not a pass.

The ctest `package_arcade_smoke` (labels `live` and `live-package`; not
`RUN_SERIAL`, so it may run in parallel with the other live tests) runs the
stage mode on each configuration's own `ctr_native.exe`, writing under
`build-msvc-x86\package_smoke\<config>`. It runs on link ports 7001 and 7002
and discovery ports 7003 and 7004; `arcade_link_launch` uses 7101 and 7102
and `arcade_discovery_link` 7301-7304, so they never share a port. Run it
alone with
`ctest --test-dir build-msvc-x86 -C Debug -L live-package --output-on-failure`.
`package_arcade_stage` (not live) checks the stage mode, the argument checks
of the smoke and of both gate scripts with a dummy exe, and the smoke's
refusal of a package `arcade.cfg` that sets a port, a peer, a group, a lan, or a
seat other than `auto`.

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
linked cabinet. Do them on both cabinets: they are the same on both. The
package's `arcade.cfg` works on every cabinet as shipped (PK-8): the
cabinets find each other on the local network and elect their seats.

| | Every cabinet (as shipped) | Optional override (edit `arcade.cfg`) |
| --- | --- | --- |
| `seat` | `auto`: elected; the lower IP address is cab1 | `cab1` or `cab2`: a fixed seat, the other cabinet takes the other one (never the same seat on both) |
| `port` (this cabinet's link port) | none: 7001 | another port, never 7000 (change the firewall rule to match) |
| `group` | none: `ctr-native` | a name, the same on both cabinets, to keep two installations on one LAN apart |
| `lan` | none: every network | `lan = 192.168.1.0/24` (exactly the arcade card's subnet, same network and prefix), the same on both cabinets: on a cabinet with two network cards, discovery then uses only that subnet (see below) |
| `peer` | none: discovery | static mode on BOTH cabinets: `seat` `cab1`/`cab2`, `port`, `peer = <other cabinet IP>:<its port>`, no `group` |

A static cabinet does not beacon, so a pair of one static and one discovery
cabinet never links (PK-5). `<other cabinet IP>` stands for that cabinet's
fixed IP address; replace the whole placeholder, angle brackets included.

`lan` is for a cabinet with two network cards, such as the fleet's (the
wired arcade switch at 192.168.1.x and another network). Windows sends the
search broadcast 255.255.255.255 through one card only, and without `lan`
the cabinet's beacons also go onto the other network and beacons from it
are heard. With `lan = 192.168.1.0/24` on both cabinets, discovery uses only
that subnet (PK-5): beacons to 192.168.1.255 only, nothing heard from
outside it, and a `peer` (static mode) must be inside it. The `lan` must
equal the arcade card's subnet exactly, the same network and the same
prefix: `192.168.1.0/24` for a card 192.168.1.11 with mask 255.255.255.0
(check with `ipconfig`). If no card of the cabinet is on the subnet (the
switch cable out, the card down), the cabinet sends nothing, logs
`[CTR Native] arcade discovery: no network interface in lan
192.168.1.0/24; not beaconing, retrying` once, waits (solo is offered),
and looks again every 10 s; it never falls back to the other card. A card
whose address is in the lan but whose mask differs (192.168.1.12 with mask
255.255.0.0) does not count either: the cabinet logs `[CTR Native] arcade
discovery: interface 192.168.1.12/16 is in lan 192.168.1.0/24 but its
subnet differs; not beaconing, retrying` once and waits the same way; fix
the card's mask or the `lan` so they match. If Windows does not return its
card list, it logs `[CTR Native] arcade discovery: interface list
unavailable; not beaconing, retrying` and waits the same way. When the
card is back it logs `[CTR Native] arcade discovery: lan 192.168.1.0/24 on
interface <its address>; beaconing`. Two cards in the same subnet are still
not supported (docs/DISCOVERY_MILESTONE.md risk 2). The fleet setup script
(C:\Arcade\scripts\setup-ctr-native.ps1) will write `lan = 192.168.1.0/24`
in a later change; this repository does not change it.

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
   cabinet: game options"). Copying a new package over an installed
   folder overwrites `arcade.cfg` with the shipped one: keep a copy of an
   edited one and put it back (PK-10).
2. **Game data.** Put your own raw NTSC-U disc image (MODE2/2352), named
   `ctr-u.bin`, in `C:\ctr-data`, the folder `arcade.cfg` names in
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
3. **Config file.** `$pkg\arcade.cfg` ships ready; there is no copy step.
   Edit it (save it as UTF-8 or ANSI text, not UTF-16) only for these:
   - `data_dir`: only if the data is not in `C:\ctr-data`. Use a full path,
     or a path relative to `$pkg`; `C:ctr-data` and `\ctr-data` are
     refused (PK-6).
   - `seat`: keep `auto`. `cab1` or `cab2` is an optional fixed-seat
     override (see the table above).
   - `port`, `group`, `lan`, `peer`: none by default. The file shows each as a
     comment; uncomment one only for an override from the table above.
   - `fullscreen`: keep `1` on a cabinet (`0` is windowed, for testing).
   - `render_scale` (shipped `8`) and `texture_filter` (shipped
     `bilinear`): this cabinet's picture only; the two cabinets may differ.
     Lower `render_scale` (`1`, `2`, `3`, `4`, or `6`) if the cabinet's GPU
     cannot keep up; `nearest` is the blocky PS1 look.

   There are no other keys (PK-3). A comment goes on its own line: a
   `# comment` after a value becomes part of the value. After `seat`,
   `port`, `peer`, `group`, `lan`, `fullscreen`, `render_scale`, or
   `texture_filter` that is a bad value, and the game stops
   with the file and line. After `data_dir` it becomes part of the path, and
   the game stops with `data directory ... does not hold ctr-u.bin or
   BIGFILE.BIG.` Either way the cabinet does not start.
4. **Firewall (PK-9).** In an elevated PowerShell (Run as administrator),
   after setting `$pkg`, allow inbound UDP on the discovery port 7000 and
   the link port 7001 for the packaged exe, from the local subnet. The rule
   is the same on both cabinets:

   ```powershell
   New-NetFirewallRule -DisplayName 'CTR arcade link' -Direction Inbound -Protocol UDP -LocalPort 7000,7001 -RemoteAddress LocalSubnet -Program "$pkg\ctr_native.exe" -Profile Any -Action Allow
   ```

   With a `port` override, put that port in place of 7001.

   A Block rule beats every Allow rule. Windows may have added one for
   this exe (for example when its firewall prompt was cancelled). List the
   Block rules that name the exe; the list must be empty:

   ```powershell
   Get-NetFirewallApplicationFilter -Program "$pkg\ctr_native.exe" | Get-NetFirewallRule | Where-Object Action -eq 'Block'
   ```

   Remove any it lists by adding `| Remove-NetFirewallRule` to that
   command. Check again after the first start if Windows showed a firewall
   prompt.

   If the package folder or the port changes, remove the rule
   (`Remove-NetFirewallRule -DisplayName 'CTR arcade link'`) and add it
   again.
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
   [CTR Native] Config groups from the file: link fullscreen render_scale texture_filter data_dir
   [CTR Native] Local render scale: 8x
   [CTR Native] Local window mode: fullscreen
   [CTR Native] Local texture filter: bilinear
   ```

   and, a little later, `[CTR Native] arcade link: auto port 7001, 0 peers`
   (with a fixed seat `cab1` or `cab2` in place of `auto`; a static cabinet
   shows its seat, port, and peer count), followed, with a `lan` set, by
   `[CTR Native] arcade link: lan 192.168.1.0/24`. These lines are on the console
   only, not in `Crash Team Racing.log` (the config lines are printed
   before the log file opens). The fullscreen window may hide the console;
   switch to it (Alt+Tab) to read them. When the two cabinets have found
   each other (shortly after both show the title), each logs, on the
   console and in the log,
   `[CTR Native] arcade discovery: paired with <other cabinet IP>:7001 as cab1`
   (`as cab2` on the other cabinet).
   `Config file: none (... not found)` means there is no `arcade.cfg` next
   to the exe (check for a hidden `.txt` extension), and the cabinet would
   start unlinked. An error in the file stops the game with a message
   naming the file and, where there is one, the line. Startup errors, such
   as that one or `arcade link requires a known build and content
   identity.`, are printed on the console only, not in the log. After a
   double-click the console stays open on an error until Enter is pressed.

## Solo race

A linked cabinet does not need the other cabinet to be on
(docs/SOLO_CAB_MILESTONE.md). When the other cabinet is off, or sits on
its attract title, the player's START shows `CONNECTING`, and after about
3 s without an answer `WAITING FOR OTHER CABINET` and
`PRESS START TO RACE SOLO`. START or CROSS (on a G29
CROSS is also the throttle pedal) then starts a one-player race against 7
bots, after the player picks a character, a track, and the laps. RESULTS
offers `RACE AGAIN` and `LOBBY`; left alone for 30 s it returns to the
attract title. `OTHER CABINET IS READY` on RESULTS means a player
started the other cabinet (pressed START) during this solo session.
Choose `LOBBY`: if that player is still in their LOBBY, the two cabinets
link as before. During solo the link only listens on its link port and
sends nothing; in discovery mode the cabinet keeps sending its discovery
beacons, so a cabinet that starts later still finds it. The PK-9 firewall
rule covers both. No `arcade.cfg` key is needed for solo, and none exists
(PK-3).

## Template line ends

`.gitattributes` checks out `tools/package/*.cfg` and `*.txt` with CRLF
line ends on every machine, whatever `core.autocrlf` says, so every checkout
packages the same template bytes and the MANIFEST hashes do not depend on
the packager's git settings.

## Notes

- A malformed display flag (for example a bad `--render-scale`) still falls
  back to 1x windowed, as before. The fallback also drops the file's
  `fullscreen`, `render_scale`, and `texture_filter`. A bad `render_scale`
  or `texture_filter` in the file is not a flag: it stops the game with the
  file and line, like any bad value.
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
  and from the file), `--config` twice, and a bad `render_scale` and a bad
  `texture_filter` (with their lines). One more case, a file with
  `render_scale = 2`, `texture_filter = bilinear`, and an empty `data_dir`
  folder run with `--render-scale 4`, checks the startup lines: 4x,
  bilinear, `render_scale` overridden, and `texture_filter data_dir` from
  the file. Every case stops before the assets and `Platform_Init`, so it
  needs no game data and no display.
