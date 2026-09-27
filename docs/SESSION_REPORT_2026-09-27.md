# Session report: 2026-09-25 to 2026-09-27

This is a point-in-time report for the owner. The living state is kept in
`docs/HANDOFF.md`.

## Outcome

- CTR Native `ctr-arcade-c39867c69b7b` (dev commit `c39867c69`) runs on
  both cabinets. It is deployed through the fleet (`C:\Arcade` main
  `d0c3518`, pushed).
- The owner confirmed on the physical cabinets, launched through Pegasus:
  - the cabinets worked;
  - bot karts stayed full 3D models.
- The owner also found an open item: 3D terrain still has LOD applied
  (see Next).

## What shipped since v1 (`0e22781a4`)

There are 75 commits on `arcade`, all pushed. The main ones, with the
detailed docs:

- **Lockstep race (Task 8, `LOCKSTEP_RACE_MILESTONE.md`).**
  - Both cabinets drive the race from their pads, exchanged every tick
    with a 3-tick delay.
  - A frozen peer holds the other cabinet with a banner in the game font.
  - Desyncs, stalls and dropped peers end the race on RESULTS.
  - v1 already had this. Later work added the logging gaps LR-70 and
    drive end, and the LR-60 cap pin.
- **Packaging (`PACKAGING.md`).**
  - The Release package is a static exe plus `arcade.cfg`, README and
    MANIFEST, with no retail data.
  - Release links with `/OPT:NOICF`: identical-code folding broke the
    function-identity checks.
  - A fresh cabinet with no memcard save marks the options as loaded at
    Arm; before this, every linked race failed there.
- **arcade.cfg display keys.** `render_scale` and `texture_filter` join
  `fullscreen`, so a bare exe runs 8x bilinear fullscreen.
- **Single-player race (`SOLO_CAB_MILESTONE.md`).**
  - When the peer is silent, the lobby offers a solo race: 1 human and 7
    bots, driven locally.
  - The link keeps listening, and a peer that wakes is linked from the
    next LOBBY.
- **Auto-discovery (`DISCOVERY_MILESTONE.md`).**
  - `seat = auto` uses UDP beacons on port 7000, and the lower IPv4
    address becomes cab1.
  - `lan = 192.168.1.0/24` pins discovery to the arcade switch NIC;
    each cabinet has two NICs.
  - `group` plus an identity digest keep unrelated installs apart.
  - A static `peer` still works.
- **Cabinet polish.**
  - LINK mode skips the SCEA, copyright and crate intro, about 27 s,
    and does not play the crate song over black.
  - Every kart and model instance draws at its top LOD tier, with the
    2P-4P kart impostor off.
  - LINK primMem uses 256 KiB host buffers; the worst peak across 16
    tracks is 52%.
- **Retail bugs found by the real linked races (all fixed and
  live-proven):**
  - The N. Tropy clock wrote `clockFlash` through an empty driver slot.
    It crashed both cabinets on the same frame in a 6-driver race.
  - The missile target read `pushBuffer[driverID]` out of bounds for
    bots 4 and 5. On native the value followed ASLR, so two cabinets
    could pick different targets and desync.
  - The V4 drivers extractor rejected a retail spin re-hit, which failed
    Dingo Canyon and Polar Pass. The canonical rule was fixed with no
    schema change.
  - The BOTS plant camera and the shield crash flash wrote `pushBuffer`
    out of bounds. They are now guarded to driverID < 4.
- **Tests.**
  - The suite is 187 tests.
  - Live tests carry area labels, so a single area can be run.
  - Per change, the fast suite (about 35 s) runs plus the affected live
    area. The full suite (about 10 min at `-j 8`) runs once per
    milestone.
  - `arcade_roster_track_sweep` covers all 16 tracks in both profiles,
    with byte-identical same-seed pairs.
  - The new live gates are `arcade_solo_race`, `arcade_solo_wake_link`
    and `arcade_discovery_link`.
  - Every test process is silent (SDL dummy audio).
- **Fleet (`C:\Arcade`).** Changes are listed below in date order.
  - Per-cabinet setup runs by itself after every sync or boot
    (`setup-ctr-native.ps1 -Auto`). Each cabinet checks only itself, and
    the link mode follows the deployed MANIFEST.
  - The fleet deploys and rolls back with `deploy-ctr-native.ps1`; the
    v1 backup is in `games\backups\ctr-native\`.
  - The launcher runs 8x fullscreen bilinear.
  - The exit watcher was rebuilt: the committed exe predated its ini
    reader, so the G29 exit combo never closed CTR.
  - CTR has Pegasus art from libretro-thumbnails.

## Evidence

- Dev HEAD `c39867c69` passed the full Debug suite (186/186) and the full
  Release suite (186/186, 593 s). The package's exe SHA-256 is
  `181FE2A970CFB570E14A8FFE8B31A1B3D2567B5FB9C4D0D4B8B11B49C2A18C0D`.
- Deployment, 2026-09-27:
  - CAB1: `deploy-ctr-native: DONE`, `setup-ctr-native: PASS [SETUP,
    discovery]`.
  - CAB2: the sync pulled 15 items. Setup showed `PASS [SETUP,
    discovery]`, and the exe hash equals CAB1's.
  - The generated `arcade.cfg`: `seat = auto`, `lan = 192.168.1.0/24`,
    8x, bilinear, fullscreen.
- The owner's hardware check, with races through Pegasus: the cabinets
  worked, and bot karts stayed 3D.
- Later dev commits add logging and tests only. They passed the full
  Debug suite (187/187) and are packaged as `ctr-arcade-78c1376c5c32`.
  This package is not deployed.

## Next

1. **Terrain LOD** (owner-observed). The level BSP near/far slot and the
   level texture and subdivision depths are unchanged
   (`SOLO_CAB_MILESTONE.md` section 8.3 table). They were left at retail
   because primMem was sized for the retail tiers. LINK now has 256 KiB
   primMem buffers with a 52% worst peak, so forcing the near tier in
   LINK may fit. It must be measured on all 16 tracks and must stay
   presentation-only; collision and the per-player-count LEV file are
   simulation and stay unchanged.
2. **DISC-S6, the rest of the physical acceptance.**
   - Pairing at boot, and .11 as cab1: confirm from the logs.
   - With one cabinet off, the other offers solo.
   - They relink when it is powered back on.
3. The open items in HANDOFF "Next work":
   - audio during a hold;
   - RESULTS standings;
   - TOPOLOGY not compared;
   - the ONE_CAB pairs gate.
