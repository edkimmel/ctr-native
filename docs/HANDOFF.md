# Handoff

> **Living document.** Keep this file current and truthful. It describes the
> present state only. Do not record history, changelogs, migrations, "recently
> changed" notes, or superseded plans. When the state changes, edit this file
> so it reads correctly today. Remove anything that is no longer true.

This is the fastest way for a new contributor or session to understand where
the project is and what to do next.

## Repository

- Native PC port of Crash Team Racing (PS1), built on the CTR-ModSDK
  decompilation. Root: `C:\re-tools\ctr-native`.
- Integration branch: `arcade`, the line for a two-cabinet, wheel-driven CTR
  installation. Baseline for this line: `99dd3fad6f3f715848938c819673701febdea3bb`.
- MSVC x86 is the recommended Windows toolchain. No PSX toolchain is needed.
  The build is fully static; SDL3 is vendored and compiled from source.

## Mission

From `docs/ARCADE_FORK.md`: preserve and test deterministic native simulation,
run the original CTR bots identically on both peers, then add a two-peer
wired-LAN lobby with fixed-delay lockstep transport, exchange canonical state
hashes, and retain first-divergence diagnostics. OnlineCTR is not a dependency,
transport, compatibility target, or fallback.

Integration order:

1. Reproducible Win32 baseline — complete.
2. Canonical state, input replay, and deterministic hashes — substantially
   complete.
3. Stable two-human-plus-bot roster and RNG ownership — in progress.
4. Native lockstep protocol and virtual-network fault tests — not started.
5. Failure handling, results, and rematch.
6. CAB1 G29/kiosk gate.
7. Two-cabinet fleet acceptance.

## Deterministic simulation

- **Canonical state.** V4 is current (schema 5, replay format 4) and hashes six
  domains: control, RNG, input, drivers, world counters and mine registry, and
  topology. Domains are SHA-256 digested and folded into a combined digest;
  encode/decode are transactional and identity-checked. V1 and V3 remain for
  their existing consumers.
- **Deterministic RNG.** A dormant xoshiro256\*\* bank derived with
  `SHA-256/CTRNRNG1`, with eleven streams (match setup, items, hazards, eight
  bots) and global versus per-bot slot ownership. It does not read or replace
  the retail RNG.
- **Match config.** Portable match identity with arcade two-cabinet and
  one-cabinet profiles, a reserved `protocolVersion`, eight role-fixed slots
  (CAB1 human, CAB2 human, bot), and validated lifecycle transitions.
- **Input replay.** Replay schedulers with record and playback, per-domain
  canonical verification, and first-divergence masks for observation, VBlank
  parity, pad, canonical domain, and combined digest. Invalid submissions
  poison the session.
- **Input observation seam** and **arcade bot setup / roster / tick evidence**
  modules under `game/MAIN/`.
- **Render-scale independence.** A contract test freezes the sealed transport
  width and schema and forbids presentation terms from the simulation side, so
  integer render scale cannot alter replay or lockstep identity.

## Networking

No transport exists yet.

- `native_virtual_datagram` is a test-only, two-endpoint in-memory delivery
  harness with DROP, DELIVER, and DUPLICATE and explicit reordering. It has no
  wire encoding, OS transport, peer search, or version commitment.
- There are no sockets (`winsock`, `AF_INET`, SDL_net) and no lobby, peer
  discovery, or lockstep loop.
- `NativeMatchConfigV1.protocolVersion` is a reserved field, not a wire format.

## Topology lease

The topology lease authority is **retire-only**. A private, process-local owner
retires it at audited destructive boundaries (before `StateZero` clears `gGT`,
before the MEMPACK arena wipe, on `LOAD_LevelFile`, on the cold-boot ten-stage
load entry, on `LOAD_Hub_SwapNow`, and on a validated checkpoint restore before
overlay reset). The inactive `LOAD_Hub_ReadFile` preload is intentionally not
hooked. No game path acquires, observes, captures, activates, publishes,
serializes, or networks a lease, and the owner is excluded from checkpoint
regions.

## Build and test

Run from the repository root:

```sh
cmake --preset windows-msvc-x86
cmake --build build-msvc-x86 --config Debug
ctest --test-dir build-msvc-x86 -C Debug --output-on-failure
```

Use `build-msvc-x86`; other `build-msvc-x86-*` directories are from earlier
milestones. The full suite passes. LF-to-CRLF warnings are benign.

## Key files

- Canonical state and RNG: `platform/native_canonical_state{,_v3,_v4}.c`,
  `native_canonical_{drivers,topology,world_counters,world_mine_registry}.c`,
  `native_deterministic_rng.c`, `native_match_config.c`.
- Replay: `platform/native_replay_{v2,v3,v4}*.c`,
  `native_replay_scheduler*.c`.
- Topology lease: `game/MAIN/MainCanonicalTopologyLeaseAuthority.{c,h}`,
  `game/MAIN/MainCanonicalTopologyLeaseRuntime.{c,h}`,
  `game/MAIN/MainCanonicalTopologyLifecycleEvents.{c,h}`,
  `include/platform/native_topology_lease_runtime.h`.
- Virtual network harness: `platform/native_virtual_datagram.c`,
  `include/platform/native_virtual_datagram.h`.
- Unity build chain: `game/game_unity.h` (ordered includes; add new game `.c`
  files here). Standalone libraries are declared in `CMakeLists.txt`.
- Related docs: `docs/ARCADE_FORK.md`, `docs/TOPOLOGY_LEASE_AUTHORITY.md`,
  `docs/REPLAYS.md`, `docs/MEMORY_MODEL.md`, `docs/G29_INPUT.md`.

## Rules and constraints

- Do not wire acquire, activate, capture, or publish for the topology lease
  without a new live-cabinet gate, deterministic capture evidence, and review.
- Do not add the lease owner to checkpoints, replay, or canonical state.
  Isolation tests enforce this.
- Do not add a retire hook to `LOAD_Hub_ReadFile`.
- The repository must never contain retail game data, BIOS files, or extracted
  retail assets. The operator supplies `assets/ctr-u.bin` (gitignored, raw
  MODE2/2352 layout).
- Source and build output stay in `C:\re-tools\ctr-native`. Only a tested,
  versioned bundle is promoted to `C:\Arcade\games\ctr-native`, and CAB2
  receives it through the fleet rsync path. The canonical fleet plan lives on
  CAB1 in `C:\Arcade\docs\ctr-native`.
- Upstream updates land on a trial branch first and are promoted to `arcade`
  only after the deterministic replay and network suites pass.
- First-party native code targets portable C17 with compiler extensions
  disabled.

## Next work

Integration step 4. Design the lockstep protocol against the existing seams:
use `NativeMatchConfigV1` as durable match identity, `NativeCanonicalStateV4`
digests for per-frame verification, and `native_virtual_datagram` for fault
injection. Define the fixed-delay frame bundle and the first-divergence report,
add unit and isolation tests, and keep the protocol transport-agnostic until a
real wired-LAN socket layer is separately gated.