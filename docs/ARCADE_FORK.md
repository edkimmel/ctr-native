# Arcade fork

The `arcade` branch is the integration line for a two-cabinet, wheel-driven
CTR installation maintained in `edkimmel/racing-cabinet-fleet`.

Baseline: `99dd3fad6f3f715848938c819673701febdea3bb`

## Scope

- Preserve and test deterministic native simulation.
- Run the original CTR bots identically on both peers.
- Add a native two-peer wired-LAN lobby and fixed-delay lockstep transport.
- Exchange canonical state hashes and retain first-divergence diagnostics.
- Add wheel-first lobby, waiting, results, rematch, and exit flows.

OnlineCTR is not a dependency, transport, compatibility target, or fallback.

## Repository boundary

This repository contains source, tests, protocol definitions, and build
metadata. It must never contain retail game data, BIOS files, extracted retail
assets, or release-local logs and state.

The operator supplies a legitimately owned NTSC-U raw MODE2/2352 disc image as
`assets/ctr-u.bin` for local build/runtime testing. That path is ignored by
Git; only its format and hash may appear in local release provenance.

The canonical fleet plan and release contract live on CAB1 at:

- `C:\Arcade\docs\ctr-native\PLAN.md`
- `C:\Arcade\docs\ctr-native\WORKPACKAGES.md`
- `C:\Arcade\games\ctr-native\SETUP.md`

Source and build output stay at `C:\re-tools\ctr-native`. Only a tested,
versioned runtime bundle is promoted into `C:\Arcade\games\ctr-native`, and
CAB2 receives it through the fleet's existing rsync path.

## Integration order

1. Reproducible Win32 baseline.
2. Canonical state, input replay, and deterministic hashes.
3. Stable two-human-plus-bot roster and RNG ownership.
4. Native lockstep protocol and virtual-network fault tests.
5. Failure handling, results, and rematch.
6. CAB1 G29/kiosk gate.
7. Two-cabinet fleet acceptance.

Upstream updates land on a trial branch first. They are promoted to `arcade`
only after the deterministic replay and network suites pass.
