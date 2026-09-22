# Failure handling milestone

Design-and-status record for integration step 5, failure handling, results,
and rematch, on branch `arcade`. This document is an expansion of the "Next
work" section in the `docs/HANDOFF.md` revision it replaces:

> Integration step 5: failure handling, results, and rematch. The lockstep
> session already reports a stall, a first-divergence, and a first-fault
> condition (see Networking); step 5 decides what the game does with each of
> those (how long to wait on a stall, when to drop a peer, what the results
> and rematch flow show), and, following this milestone's own pattern, that
> logic can be designed and fault-tested against `native_virtual_datagram`
> exactly as the lockstep protocol was, before a real wired-LAN socket layer
> exists. The real socket/transport layer (winsock or SDL_net, peer
> discovery, a lobby) remains a separately gated piece of work with its own
> live-cabinet evidence requirement, needed before step 6 (CAB1 G29/kiosk
> gate) and step 7 (two-cabinet fleet acceptance) can run on real hardware.

Unlike `docs/LOCKSTEP_MILESTONE.md`, this is a retrospective record of five
small, already-landed tasks, not a prospective plan: there is no task list or
open-questions section here, because nothing in this milestone was left
unbuilt at the point this document was written.

## Decided design

### Stall timeout

`include/platform/native_lockstep_match_outcome.h:30-41` defines three
constants:

- `NATIVE_LOCKSTEP_STALL_TIMEOUT_DEFAULT_FRAMES` `180u` (3 s at 60 Hz) — the
  milestone's deliberate default: a two-cabinet wired-LAN hiccup should
  recover well inside 3 s.
- `NATIVE_LOCKSTEP_STALL_TIMEOUT_MIN_FRAMES` `30u` (0.5 s at 60 Hz) — below
  this a transient network hiccup could not plausibly recover in time.
- `NATIVE_LOCKSTEP_STALL_TIMEOUT_MAX_FRAMES` `600u` (10 s at 60 Hz) — a hard
  ceiling so a truly dead peer does not stall the cabinet forever.

`NativeLockstepMatchOutcome_Init` (`platform/native_lockstep_match_outcome.c:5-23`)
zeroes the tracker and uses the default when the caller passes `0`, otherwise
requires the value in `[MIN_FRAMES, MAX_FRAMES]` and fails (leaving the
tracker untouched) outside that range.

`NativeLockstepMatchOutcome_Poll` (`platform/native_lockstep_match_outcome.c:25-81`)
is called once per simulation tick, right after
`NativeLockstepSession_TakeFrameInputs`, and is a no-op once the tracker is
already latched. Its priority order, mirroring the session's own
DIVERGED-outranks-FAULTED priority:

1. `NativeLockstepSession_FirstDivergence(session)` non-`NULL` → latch cause
   `NATIVE_LOCKSTEP_MATCH_OUTCOME_DIVERGED` (`:41-50`).
2. Else `NativeLockstepSession_FirstFault(session)` non-`NULL` → latch cause
   `NATIVE_LOCKSTEP_MATCH_OUTCOME_FAULTED` (`:52-61`).
3. Else, if `lastTakeResult == NATIVE_LOCKSTEP_SESSION_STALL`, increment
   `consecutiveStallFrames`, and once it reaches `stallTimeoutFrames`, latch
   cause `NATIVE_LOCKSTEP_MATCH_OUTCOME_STALL_TIMEOUT` with `senderSlot` set
   to the unattributed sentinel `NATIVE_LOCKSTEP_MATCH_OUTCOME_UNATTRIBUTED_SLOT`
   (`:63-74`), because the session does not expose which remote window is
   empty.
4. Any other result resets `consecutiveStallFrames` to `0` (`:75-78`).

`NativeLockstepMatchOutcome_FirstOutcome` returns `&tracker->report` once
latched, `NULL` otherwise (`platform/native_lockstep_match_outcome.c:83-91`),
the same `const`-or-`NULL` accessor idiom as
`NativeLockstepSession_FirstDivergence`/`_FirstFault`. The module reads the
session only through those two public accessors and never mutates it
(`include/platform/native_lockstep_match_outcome.h:18-23`).

### Peer drop

`struct NativeLockstepMatchRoster` (`include/platform/native_lockstep_match_roster.h:18-22`)
tracks `role[]` and `lifecycle[]` per slot in its own array, entirely
separate from `struct NativeMatchConfigV1`. The header explains why
(`:9-17`): `slot->initialLifecycle` is pinned by `NativeMatchConfigV1_Validate`
to the role-fixed initial value and is part of the 256-byte encoded/digested
config, so the roster tracks *current* lifecycle itself and never writes
back into `NativeMatchConfigV1`.

`NativeLockstepMatchRoster_DropSlot` (`platform/native_lockstep_match_roster.c:22-33`)
only transitions a slot whose role is `CAB1_HUMAN` or `CAB2_HUMAN`
(`include/platform/native_lockstep_match_roster.h:34-43`); a bot role makes
it a no-op returning `0`. Bots are never networked and are never dropped.

`NativeLockstepMatchRoster_ApplyOutcome` (`platform/native_lockstep_match_roster.c:35-71`)
takes a latched, non-`NONE` outcome report and drops every human slot except
`localSlot`: the local human is never dropped by its own outcome report
(`include/platform/native_lockstep_match_roster.h:46-55`). It returns the
count of slots whose lifecycle value actually moved, treating a harmless
self-transition (already `DISCONNECTED`/`FINISHED`) as not counted, per the
comment at `platform/native_lockstep_match_roster.c:57-63`.

### Rematch

`NativeLockstepRematch_BuildConfig` (`platform/native_lockstep_rematch.c:5-50`)
preserves, from the previous config: `trackID`, `gameMode1`, `gameMode2`,
`rules`, `lapCount`, `tickRateNumerator`/`tickRateDenominator`,
`buildIdentity`, `contentIdentity`, `botRulesDigest`, and every slot's
`characterID`/`difficulty` (`:25-41`). It regenerates everything else from
scratch by calling `NativeMatchConfigV1_InitArcadeTwoCab` or
`_InitArcadeOneCab` again based on `previous->profile` (`:16-23`), rather
than copying the previous config's runtime state. The new `masterSeed` is
mandatory: the function returns `0`, with `*next` completely untouched, if
`newMasterSeed == previous->masterSeed`
(`include/platform/native_lockstep_rematch.h:33-37`,
`platform/native_lockstep_rematch.c:10-14`), because a rematch must use a
different seed so item, hazard, and bot RNG streams are not replayed
bit-identically.

The header's block comment (`include/platform/native_lockstep_rematch.h:6-27`)
documents the explicit contract that building this config is only half the
story: it does not create or touch a `NativeLockstepSession` or a
`NativeReplaySchedulerV4`. A caller holding a session left in `DIVERGED` or
`FAULTED` mode, or a replay scheduler left in `MISMATCH` or `POISON` mode,
must never resume or reuse it for the rematch — both of those latch-once
terminal states exist specifically so a poisoned or diverged run is never
silently continued. A rematch always means opening a brand-new
`NativeLockstepSession` (`_Init` then `_Open` on a fresh struct) and a
brand-new `NativeReplaySchedulerV4` recording (`_Init` then `_OpenRecord`
with a fresh output path), never reusing the old scheduler or session struct
in place. `native_lockstep_rematch.c` has no dependency on the session or
replay scheduler headers at all — it only produces the config value.

### Fault-testing posture

Following the same milestone pattern as integration step 4 (the lockstep
protocol), this failure-handling logic is fault-tested against
`native_virtual_datagram` before any real socket/transport layer exists.
`tests/native_lockstep_failure_handling_fault_test.c` drives four scenarios,
each over a `struct NativeVirtualDatagramPair`:

1. **Stall timeout** (`TestStallTimeoutDropsPeer`, `:241-284`) — one side
   never receives anything from its peer, so every
   `NativeLockstepSession_TakeFrameInputs` call stalls; the outcome tracker
   turns that into a latched `STALL_TIMEOUT` after exactly
   `stallTimeoutFrames` consecutive stalled polls, and the roster then drops
   the unreachable remote peer, all while the session's own mode stays
   `RUNNING`.
2. **Divergence** (`TestDivergenceDropsPeer`, `:293-351`) — a real divergence
   is forced over the virtual datagram pair; the outcome tracker turns the
   session's own latched divergence report into a `DIVERGED` outcome and the
   roster drops the diverging peer.
3. **Fault** (`TestFaultDropsPeer`, `:361-417`) — two sessions opened on
   configs with different `trackID` (hence different match identity) produce
   a clean `MATCH_IDENTITY` protocol fault on decode; the outcome tracker
   turns the latched fault into a `FAULTED` outcome and the roster drops the
   faulting peer.
4. **Rematch starts clean** (`TestRematchStartsCleanSession`, `:426-486`) — a
   rematch config built from scenario 2's now-`DIVERGED` config carries no
   terminal state forward: the old, still-`DIVERGED` session refuses to
   reopen even on the rematch config, and a brand-new
   session/tracker/roster triple opened on that same config runs several
   clean frames with no divergence, fault, or dropped peer.

Each of the three modules also has its own focused unit test
(`tests/native_lockstep_match_outcome_test.c`,
`tests/native_lockstep_match_roster_test.c`,
`tests/native_lockstep_rematch_test.c`, registered in `CMakeLists.txt` next
to the fault-injection test), in addition to the shared fault-injection
integration test above.

## Status

Tasks are done on `arcade`: match-outcome and match-roster
(`31453939f`), rematch config builder (`c0446376b`), the fault-injection
integration test (`5d2d03266`), and the structural isolation test
(`29202b230`). Full suite: 82 tests, 100% passed.

## What this milestone deliberately does not do

- No real socket/transport wiring. That remains separately gated, per
  `docs/HANDOFF.md`.
- No game-loop integration yet. No file under `game/` references
  `NativeLockstepMatchOutcome`, `NativeLockstepMatchRoster`, or
  `NativeLockstepRematch` (enforced by the isolation test); the three
  modules remain standalone `platform/native_*` libraries, exactly like the
  lockstep protocol/window/session before them.
- No change to the topology lease, canonical state, or replay wire formats.
- No UI or rendering for the results/rematch screen itself. This milestone
  builds the data model and policy the eventual screen will read, not the
  screen.
