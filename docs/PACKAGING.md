# Packaging and the per-cabinet config file

How a cabinet build is packaged and configured. Decisions PK-1..PK-8.

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
  needs a known build identity.
- The config never reaches the match config, simulation or canonical state,
  replay, checkpoints, identity, or the topology lease. It reaches only the
  window mode, the link options, and the assets directory.
  `tests/native_arcade_config_isolation_test.cmake` pins this.

**PK-6 Data directory.** `data_dir` and `--data-dir` name the folder that
holds the user's own `ctr-u.bin`, or the extracted `BIGFILE.BIG` tree. It
plays the role `assets/` plays without a config.
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

- The ports are the ones the loopback gate uses.
- The IPs are placeholders. A comment says to edit each to the other
  cabinet's fixed IP.
- Both templates set `data_dir = C:\ctr-data` and `fullscreen = 1`.
- `tools/package/README.txt` is the operator guide: data, per-cabinet setup,
  the firewall rule, starting, and the same-build hash check.
- `native_arcade_config_unit` parses both templates with the real parser and
  checks the resulting link options.

## Config grammar

- One `key = value` per line. Keys are exactly `data_dir`, `seat`, `port`,
  `peer`, `fullscreen` (lowercase).
- Whitespace (space, tab) around the key and the value is trimmed.
- The value is the rest of the line after the first `=`. It may contain
  spaces and `=`, and it takes no quotes. A trailing `# comment` is part of
  the value, and so makes it invalid.
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
