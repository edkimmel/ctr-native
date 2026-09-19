# Replays

Use this for bug reports in internal builds.

## Quick State

- `F5`: save `debug/states/quick.ctrstates`
- `F8`: load `debug/states/quick.ctrstates`

## Record

```sh
build/ctr_native --record
```

Windows: use `build\ctr_native.exe` instead.

Normal saves live in `memcards/slot0`.

When recording starts, the CTR save files from `memcards/slot0` and `slot1` are copied to `memcard.seed`. The game records with a writable copy named `memcard.recording`, so saves and ghosts made while recording stay in the report.

To choose when recording starts:

```sh
build/ctr_native --record --toggle
```

- Press `F9` to start.
- Press `F10` to stop.

For more detailed reports:

```sh
build/ctr_native --record --detailed
```

You can combine both:

```sh
build/ctr_native --record --toggle --detailed
```

## Play Back

Use the command written in that folder's `metadata.txt`.

It looks like:

```sh
build/ctr_native --replay "debug/reports/20260605/ctr-123456/input.ctrreplay"
```

Playback creates a fresh writable `memcard.playback` from `memcard.seed` every run and does not touch your real saves.

If a developer asks you to bypass header identity checks:

```sh
build/ctr_native --replay "debug/reports/20260605/ctr-123456/input.ctrreplay" --replay-bypass-header
```

## Render-scale determinism sweep

First record and cleanly finalize a new canonical V2 replay with the current
build:

```sh
build/ctr_native --record-v2
```

Then run every local integer render scale against its `input.v2.ctrreplay`
from PowerShell:

```powershell
.\tools\run-render-scale-replay-sweep.ps1 `
  -ReplayPath .\debug\reports\YYYYMMDD\ctr-HHMMSS\input.v2.ctrreplay `
  -PerfOutputDirectory .\debug\perf\render-scale-YYYYMMDD
```

The runner rejects a non-finalized, truncated, empty, or non-CRV2 replay
before it starts. It launches scales `8, 6, 4, 3, 2, 1` sequentially with
`--replay-v2`, never bypassing build/content identity validation, and fails if
any replay exits nonzero. V2 replay verification is authoritative for
canonical state, input, frame timing, and VBlank parity; display options stay
cabinet-local. `-PerfOutputDirectory` creates one performance capture per
scale for the M4 race budget; use `-Fullscreen` only for a native-panel
presentation pass.
