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
4. Native lockstep protocol and virtual-network fault tests — protocol design
   and fault-tolerant session logic complete; real wired-LAN transport not
   started, separately gated (see Networking).
5. Failure handling, results, and rematch — stall-timeout policy, peer-drop
   roster, and rematch config builder complete and fault-tested against
   `native_virtual_datagram`; no game-loop integration or results/rematch UI
   yet.
6. CAB1 G29/kiosk gate.
7. Two-cabinet fleet acceptance.

## Deterministic simulation

- **Canonical state.** V4 is current (schema 5, replay format 4) and hashes six
  domains: control, RNG, input, drivers, world counters and mine registry, and
  topology. Domains are FNV-1a 64 digested (`NativeCodecDigest64`) and folded
  into a combined digest with the same function; encode/decode are
  transactional and identity-checked. SHA-256 in V4 is used only for
  `identity.build`, `identity.content`, and `configDigest`. V1 and V3 remain
  for their existing consumers.
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
- **Presentation independence.** Integer render scale (`--render-scale`), the
  local texture filter (`--texture-filter nearest|bilinear`, default
  `nearest`; bilinear is a post-palette blend inside the PSX fragment shaders
  and the VRAM sampler stays `GL_NEAREST`) and unattended frame capture
  (`--capture-frame <N>=<path.bmp>`, `--exit-after-frame <N>`) are host-local
  presentation options. A contract test freezes the sealed transport width and
  schema and forbids these presentation terms in the transport headers, and an
  isolation test keeps the texture filter out of vertex construction,
  savestate, checkpoint, replay, canonical-state and game code, so none of
  them can alter replay or lockstep identity.

## Networking

The native lockstep protocol is complete and transport-agnostic; no real
socket/OS transport exists yet.

- **Fixed-delay lockstep, no rollback, no prediction.** Each peer buffers its
  sampled input for a configured `inputDelay` (`D`) frames; simulation frame
  `N` consumes only inputs sampled at frame `N - D` by every peer, so the
  simulation stays a pure function of a fully known input set and is still
  recordable/replayable by the existing V4 scheduler unchanged (see
  `docs/REPLAYS.md`).
- **Frame-bundle codec** (`native_lockstep_protocol.c`/`.h`): a fixed 128-byte,
  little-endian, self-describing wire record per peer per frame, carrying
  match identity, frame index, input delay, per-slot pad input, and the
  lagged verified-frame digests (`verifiedFrameIndex = frameIndex - D - 1`)
  plus a trailing FNV-1a 64 `bundleDigest`.
- **Delay/reorder input window** (`native_lockstep_input_window.c`/`.h`): a
  fixed-capacity, no-allocation per-peer ring keyed by frame index that
  accepts fresh and out-of-order in-window arrivals, treats a byte-identical
  duplicate as a no-op, drops a stale arrival silently, and reports a protocol
  fault if a frame arrives past the window.
- **Session** (`native_lockstep_session.c`/`.h`): composes and accepts
  bundles, tracks recent local digests, and latches a first-divergence report
  (per-domain and combined digest mismatch, at the frame that actually
  diverged) and a separate first-fault report (malformed bundle, identity or
  delay mismatch, window overrun) with `const`-or-`NULL` accessors, mirroring
  `NativeReplaySchedulerV4`'s latch-once mismatch report.
- The whole protocol/window/session stack is fully covered by unit tests, a
  fault-injection integration test that drives loss, delay, reorder, and
  duplication over `native_virtual_datagram`, and a structural isolation test.
- `native_virtual_datagram` remains a test-only, two-endpoint in-memory
  delivery harness (DROP, DELIVER, DUPLICATE, explicit reordering); it has no
  wire encoding, OS transport, peer search, or version commitment, and the
  protocol library never links it.
- There are still no sockets (`winsock`, `AF_INET`, SDL_net) and no lobby or
  peer discovery. `NativeMatchConfigV1.protocolVersion` is a reserved field,
  not a wire format, though the lockstep bundle copies and compares it.
- **Failure handling, peer drop, and rematch** (`native_lockstep_match_outcome.c`/`.h`,
  `native_lockstep_match_roster.c`/`.h`, `native_lockstep_rematch.c`/`.h`)
  build a policy layer on top of the session. `NativeLockstepMatchOutcome`
  turns an indefinitely retried stall into a latch-once, bounded
  `STALL_TIMEOUT` outcome after a configurable number of consecutive stalled
  polls (default 180 frames / 3 s at 60 Hz, range 30-600), mirroring the
  session's own DIVERGED-outranks-FAULTED priority for the two other
  terminal causes. `NativeLockstepMatchRoster` tracks each slot's current
  lifecycle in a caller-owned array, separate from `NativeMatchConfigV1`
  (whose `initialLifecycle` is pinned and digested), and drops every human
  peer named by a latched outcome except the local slot; a bot slot is never
  dropped. `NativeLockstepRematch` builds a fresh `NativeMatchConfigV1` for
  the same fixture with a mandatory new `masterSeed`, and never resumes a
  session left DIVERGED/FAULTED or a replay scheduler left MISMATCH/POISON —
  a rematch always opens a brand-new session and a brand-new V4 replay
  recording. This layer is fault-tested against `native_virtual_datagram` the
  same way the protocol/window/session stack is. None of it is wired into
  the game loop yet, there is no results/rematch UI, and it makes no change
  to the topology lease, canonical state, or replay wire formats.

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
- Lockstep protocol: `platform/native_lockstep_protocol.c`,
  `include/platform/native_lockstep_protocol.h` (frame-bundle codec);
  `platform/native_lockstep_input_window.c`,
  `include/platform/native_lockstep_input_window.h` (delay/reorder window);
  `platform/native_lockstep_session.c`,
  `include/platform/native_lockstep_session.h` (session, first-divergence and
  first-fault reports).
- Failure handling: `platform/native_lockstep_match_outcome.c`,
  `include/platform/native_lockstep_match_outcome.h` (stall-timeout policy
  and outcome latch); `platform/native_lockstep_match_roster.c`,
  `include/platform/native_lockstep_match_roster.h` (peer-lifecycle roster
  and drop policy); `platform/native_lockstep_rematch.c`,
  `include/platform/native_lockstep_rematch.h` (rematch config builder).
- Presentation options (host-local): `platform/native_display_config.c`,
  `include/platform/native_display_config.h` (render scale, texture filter),
  `platform/native_frame_capture.c`, `include/platform/native_frame_capture.h`
  (unattended frame capture); renderer seam in `platform/native_renderer.c`.
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

Integration step 5's failure-handling policy (stall-timeout outcome,
peer-drop roster, rematch config builder — see Networking) is designed and
fault-tested against `native_virtual_datagram`, the same way the lockstep
protocol was, but two things remain undone and neither has been touched by
this milestone:

- **Game-loop integration.** `NativeLockstepMatchOutcome`,
  `NativeLockstepMatchRoster`, and `NativeLockstepRematch` are standalone
  `platform/native_*` libraries; nothing under `game/` calls them yet, and
  there is no results or rematch UI that reads their reports.
- **The real socket/transport layer** (winsock or SDL_net, peer discovery, a
  lobby) remains a separately gated piece of work with its own live-cabinet
  evidence requirement.

Both need their own live-cabinet evidence before step 6 (CAB1 G29/kiosk gate)
and step 7 (two-cabinet fleet acceptance) can run on real hardware.