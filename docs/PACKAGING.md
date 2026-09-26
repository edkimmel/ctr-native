# Packaging and the per-cabinet config file

How a cabinet build is packaged and configured. Decisions PK-1..PK-8.

## Decisions

**PK-1 Build.** v1 ships the tested `CTR_INTERNAL` Release build of
`ctr_native`. The autopilot and fault options stay opt-in command-line flags.
There is no non-internal target in v1.

**PK-2 Config file.** A `key = value` text file. The default is `arcade.cfg`
in the exe directory (`SDL_GetBasePath`). `--config <path>` selects another
file; a relative path resolves against the launch directory.
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
- Whitespace (space, tab, CR) around the key and the value is trimmed.
- The value is the rest of the line after the first `=`. It may contain
  spaces and `=`, and it takes no quotes. A trailing `# comment` is part of
  the value, and so makes it invalid.
- Blank lines, and lines whose first non-blank character is `#` or `;`, are
  comments.
- CRLF or LF line ends are accepted. A UTF-8 BOM is accepted at the start
  of the file only.

Errors, each reported as `config file <path> line <n>: <reason>`:

| Error | Line reported |
| --- | --- |
| file over 16384 bytes | none |
| line over 1023 characters (line end excluded) | that line |
| NUL byte | that line |
| no `=`, or an empty key | that line |
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

## Running the package script

From the repository root, with a clean tree (commit first):

```sh
powershell -NoProfile -ExecutionPolicy Bypass -File tools/package-arcade.ps1
```

The script:
1. Refuses (exit 1) unless `git status --porcelain` is empty. Untracked files
   count; ignored files do not.
2. Runs `cmake --preset windows-msvc-x86` and
   `cmake --build build-msvc-x86 --config Release --target ctr_native`, then
   checks the tree is still clean.
3. Requires `ctr_native.exe --version` to print exactly
   `CTR Native <CTR_NATIVE_VERSION> (<git rev-parse --short=12 HEAD>)`, so a
   `-dirty` or `unknown` build is refused.
4. Recreates `build-msvc-x86\package\ctr-arcade-<short12>\`. There is no
   output-path parameter. The script refuses any destination outside
   `build-msvc-x86\package\`, and anything under `C:\arcade`.
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
- A file name has a retail or BIOS extension: `.bin .cue .iso .img .chd
  .ecm .pbp .big .hwl .str .xa .xnf .vag .mcd .mcr .sav .srm .bmp .png`.
- A file name contains `bios` or `scph` (case-insensitive).
- A non-exe file is larger than 64 KiB.
- The exe is 32 MiB or larger.

`package_arcade_content_guard` (ctest) builds fabricated folders of tiny
dummy files under `build-msvc-x86/package_guard_test/`. It checks that:
- the exact allowlist passes;
- each of these fails with its reason: `ctr-u.bin`, `SCPH1001.BIN`,
  `BIGFILE.BIG`, an unknown file, a subdirectory, an oversize `cab1.cfg`, a
  missing `MANIFEST.txt`, and a missing folder.

## Notes

- A malformed display flag (for example a bad `--render-scale`) still falls
  back to 1x windowed, as before. The fallback also drops the file's
  `fullscreen`.
- A cabinet whose `arcade.cfg` sets the link group is a link cabinet. Replay
  options and `--arcade-roster-proof` are then rejected, exactly as with
  `--arcade-link`. To run those on such a machine, pass `--config` with an
  empty file.
