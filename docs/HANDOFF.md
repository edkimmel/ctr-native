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
3. Stable two-human-plus-bot roster and RNG ownership — functionally
   complete for both match profiles, the two-cabinet (two humans, four
   bots) and the single-cabinet (one human, seven bots) retail arcade race,
   proven by the live roster proof (ctests
   `arcade_roster_determinism_two_cab` and
   `arcade_roster_determinism_one_cab`) on one
   machine: both profiles over 900 race ticks, covering same-seed
   identity, seed divergence, and menu-history independence (demo-race
   launch and odd-timer-offset runs). See `docs/ROSTER_MILESTONE.md`.
   Linked races launch through its seam (Task 7) and run on it in
   lockstep (Task 8).
4. Native lockstep protocol and virtual-network fault tests — protocol design
   and fault-tolerant session logic complete; a real socket transport,
   connect/handshake protocol, and a lobby data/state layer exist and are
   tested, including live two-process, real-socket evidence (see Networking).
   The arcade-link lobby, match-select, results, rematch, and exit screens,
   their host adapter, and a dormant-by-default hook on the main-menu level
   exist and are tested. Match select lets each player pick a character
   and vote on the track and laps; a disagreement is a seeded draw, and
   the resolved config is re-validated by a relink handshake
   (`docs/MATCH_SELECT_MILESTONE.md`). Networked race launch (Task 7,
   `docs/RACE_LAUNCH_MILESTONE.md`) and in-race lockstep driving (Task 8,
   `docs/LOCKSTEP_RACE_MILESTONE.md`) are done: both cabinets agree on a
   launch commit, arm and launch the step-3 race setup seam with the
   agreed config, and drive the race in lockstep to its end, exchanging
   and comparing live V4 digests every race tick; a rematch launches the
   next race in the same process. The two-process gate
   (`arcade_link_launch`) proves three linked races live on one machine
   over loopback. Physical two-cabinet validation remains open before
   steps 6-7.
5. Failure handling, results, and rematch — stall-timeout policy, peer-drop
   roster, and rematch config builder complete and fault-tested against
   `native_virtual_datagram`; wired to the results/rematch screens through
   the arcade-link adapter, and every rematch goes back through match
   select. In a race the lockstep drive feeds every take's result to the
   outcome tracker, so a stall timeout, a peer drop, a desync, a protocol
   fault, a local failure (RL-11), and the finish each end the race on
   RESULTS (`docs/LOCKSTEP_RACE_MILESTONE.md` LR-12).
6. CAB1 G29/kiosk gate.
7. Two-cabinet fleet acceptance.
8. Stretch goal: automatic LAN discovery for up to 4 cabinets (Mario Kart
   Arcade GP DX style). Not started. The lobby's caller-supplied candidate
   list (`docs/LOBBY_MILESTONE.md` section 2.4) is the seam it plugs into:
   cabinets find each other on the subnet (a broadcast beacon is the
   preferred design), and the handshake still validates build and content
   identity. It needs a pairing rule (for example a configured cabinet group)
   and 3-4 human slots in the roster and match config. The match-select
   rules, message, and session are already sized for 4 humans, while
   `NativeMatchConfigV1` has two human roles. It follows step 7.
9. Stretch goal: Oxide Station in linked races. Not started. Retail
   offers it in 1P only (SEL-3), so first confirm the disc has a
   multiplayer level pack for it, or what 1P-only assumption blocks it.
   Then add it to the match-select track list and the bot rules.
10. Stretch goal: unlock everything in code and add Turbo Track. Not
    started. Unlock state must come from the build, not the host-local
    save, so both cabinets agree. It removes the SEL-3 reason for
    excluding Turbo Track, and the unlocked characters and tracks must be
    reflected in match select and the bot rules.
11. Stretch goal: native 16:9 at 1080p. Not started. `main.c` has an
    unwired `USE_16BY9` stub (1280x720 window only, no CMake option, no
    game-side changes). The goal is a proper widescreen projection and
    HUD/menu layout, including the arcade-link screens (512x216 layout
    space). It must stay presentation-only: no effect on simulation,
    replay, canonical state, or lockstep identity, with an isolation test
    like the texture filter's.
12. Stretch goal: G29 force feedback. Not started. Today retail pad
    vibration (`Platform_InputPadVibrate`, `platform/native_input.c:1290`)
    maps to `SDL_RumbleGamepad` only, and a G29 gets nothing ("direct G29
    force feedback remains a separate M6 hardware gate"). The goal is
    wheel effects (for example, collisions, terrain, and a steering
    self-centering spring) through SDL haptics. It is host-local output,
    kept out of simulation identity, and needs the CAB1 G29 hardware for
    validation.
13. Stretch goal: full-screen per-cabinet view. Not started. Linked races
    ship with the retail 2P split screen on both cabinets (owner decision,
    Task 8). The goal is for each cabinet to render only its own player,
    full screen. The owner accepts graphics-only effects going out of sync
    between cabinets. However, particles currently draw from the item RNG
    (MixRNG), so a per-cabinet viewport would desync the simulation, not
    just the graphics. That draw must first be split onto a
    presentation-only RNG, and the lockstep digest must prove the
    simulation still matches.

## Deterministic simulation

- **Canonical state.** V4 is current (schema 5, replay format 4) and hashes six
  domains: control, RNG, input, drivers, world counters and mine registry, and
  topology. Domains are FNV-1a 64 digested (`NativeCodecDigest64`) and folded
  into a combined digest with the same function; encode/decode are
  transactional and identity-checked. SHA-256 in V4 is used only for
  `identity.build`, `identity.content`, and `configDigest`. V1 and V3 remain
  for their existing consumers.
- **Deterministic RNG.** A xoshiro256\*\* bank derived with
  `SHA-256/CTRNRNG1`, with eleven streams (match setup, items, hazards, eight
  bots) and global versus per-bot slot ownership. It replaces no retail RNG
  call site: the retail RNGs stay the in-race simulation RNG (RS-5). An
  armed race setup draws from its MATCH_SETUP stream (below); ITEMS,
  HAZARDS, and the eight BOT streams are reserved and undrawn in bot rules
  v1.
- **Match config.** Portable match identity with arcade two-cabinet and
  one-cabinet profiles, a reserved `protocolVersion`, eight role-fixed slots
  (CAB1 human, CAB2 human, bot), and validated lifecycle transitions. The
  arcade race config is resolved per match by match select. The per-build
  fixture is the lobby base of a first match; a rematch's lobby base is
  derived from the proposal of the most recent lobby that reached READY
  (lastReadyConfig). `botRulesDigest` is the real bot-rules digest of the
  config's profile: `NativeArcadeBotRules_DigestV1` for two-cabinet
  configs (the fixture and every match-select config) and
  `NativeArcadeBotRules_Digest1PV1` for one-cabinet configs.
- **Bot rules.** `native_arcade_bot_rules` defines and versions the native
  rule choices of an arcade single race, per profile, with global
  difficulty from the retail table {0x50, 0xA0, 0xF0} and the retail seed
  recipe in both. TWO_CAB (two humans and four bots by the retail 2P AI
  set rule, `LOAD_Robots2P`) is a 111-byte canonical encoding whose
  SHA-256, `NativeArcadeBotRules_DigestV1`, has the golden
  `9022154eab793fb25d0a2d3b0c787d62fdaf9af490b7e3f1d48fbec8d4065eab`.
  ONE_CAB (one human and seven bots by the retail 1P rule,
  `LOAD_Robots1P`: every other base character, ascending) is a separate
  93-byte encoding whose SHA-256, `NativeArcadeBotRules_Digest1PV1`, has
  the golden
  `8d06649af8aa2594fbaca395aba3ebf1cce24689f8c193f5055f4441f1d8b0e3`.
  `NativeArcadeBotRules_ValidateConfigV1` accepts only a race these rules
  can build, and only with its own profile's digest.
- **RNG ownership.** At race init an armed setup seeds every retail RNG
  state that survives across races from the bank's MATCH_SETUP stream, in a
  fixed order (RS-7): randomNumber, advRng state0 and state1, the PSX BIOS
  rand seed, and audioRNG (the last two presentation-only and not
  canonical). deadcoed keeps its retail per-race reset. The per-bot setup
  draws follow on the same stream, so the post-setup bank has drawn
  MATCH_SETUP 9 times in a two-cabinet race and 12 in a single-cabinet
  race. The live V4 projection (below) projects that post-setup bank,
  `MainArcadeRaceSetup_Bank()`, on every race tick.
- **Live race setup.** `game/MAIN/MainArcadeRaceSetup` is the race setup
  seam (`_Arm`, `_Launch`, `_Status`, `_Digests`, `_Bank`, `_Disarm`)
  through which the live race caller and the roster proof launch races;
  the race caller and the roster proof read `MainArcadeRaceSetup_Bank()`
  and hand it to `MainArcadeRaceDigest`. It turns a validated TWO_CAB
  config into a retail 2P arcade race and a validated ONE_CAB config into
  a retail 1P arcade race (one human, seven bots, eight drivers) through
  the pure plan (encoding v2, the same for both profiles), facts, and
  decision-core libraries, with two hooks in
  `MainInit_FinalizeInit` (at its very start, and right after
  `MainInit_Drivers`) that verify the loaded fields, re-apply the mode
  words, seed the RNGs, and validate the live roster and bot setup facts,
  failing closed. It pins every non-transient mode bit (cheats included), the
  vibration bits to 0, and boolDemoMode to 0, and requires the 30/1 tick
  rate. On a cabinet with no memcard save a successful Arm marks
  `sdata->boolHasLoadedOptions` (only that flag, not restored at Disarm)
  without running the retail options load, so Launch's options
  precondition holds and the live audio settings stay
  (`docs/PACKAGING.md` "Fresh cabinet: game options"). Dormant unless
  armed; its state is never checkpointed, recorded, or canonical. The init
  of the main-menu level while VALIDATED (the return load after a linked
  race) is a no-op, and the owner Disarms on the first idle main-menu
  frame, which restores the saved vibration bits.
- **Race counter pins (RS-17, LR-8).** The same race-init hook pins the
  boot-relative counters that feed the race simulation or its RNG,
  `gGT->timer` and `gGT->frameTimer_Confetti`, to 0, and the root counter
  that elapsed time is computed from, `sdata->rcntTotalUnits` to 0 and
  `gGT->clockFrameStart` to -200: the elapsed-time arithmetic wraps on the
  absolute counter, so both cabinets must start the race on the same
  counter phase. `sdata->frameCounter` and `gGT->frameTimer_VsyncCallback`
  stay boot-relative (presentation and platform only), so cross-cabinet
  comparison uses race-relative control: the live V4 control domain
  projects both relative to race tick 0.
- **Live V4 projection (LR-10).** `game/MAIN/MainArcadeRaceDigest` is the
  one live caller of the V4 runtime (`MainCanonicalRuntime`). On every
  race tick of a linked race it projects race-relative control, the
  retail RNGs, the post-setup bank, the four pads the tick read, the
  complete drivers (Physics included), and WORLD (the world-counter and
  mine-registry extractors, compiled into `ctr_native` through the unity
  chain). The roster proof projects every
  logged race tick through it too. TOPOLOGY carries the unavailable
  summary, the same constant on both cabinets, so it is not compared;
  live topology waits for a lease-activation milestone. The drivers
  extraction reads each bot's `NavHeader.last` check-only (owner ruling
  LR-17; see Topology lease). The module is read-only, lease-free, and
  never serialized; it resets the runtime on race tick 0. Debug cost: a
  mean of about 1.3 ms per tick.
- **Live roster proof.** In internal builds `--arcade-roster-proof` launches
  the configured race from the title or the attract demo race with
  scripted pads and logs per-tick V1 control, race-relative control, RNG,
  input, and topology-free drivers digests, and the V4 combined and domain
  digests through `MainArcadeRaceDigest`.
  `--arcade-roster-proof-profile two-cab|one-cab` (default `two-cab`)
  picks the race: two-cab resolves the fixture through match select;
  one-cab builds a single-cabinet config from the fixture's track, laps,
  CAB1 character, and bot difficulty, with the retail 1P bots and the seed
  as its masterSeed (match select is not used). The report (format v11)
  names the profile. `tools/arcade-roster-proof-check.ps1` runs eleven
  proofs, split across two ctests by `-Group`:
  `arcade_roster_determinism_two_cab` runs A-E and K, and
  `arcade_roster_determinism_one_cab` runs F-J plus its own A (see Build
  and test). Two-cab, 900 race ticks
  each: A and B (one seed, from the title) must be byte-identical; C (from
  the attract demo race) and E (37 ticks late; its launch timer offset
  from A must be odd) must equal A in the setup digests, the seeded and
  slot lines, and at every tick the rng, input, drivers, and rcontrol
  digests, and start race tick 0 with A's pinned counters (the full
  control digest is informational only); D (another seed) must differ from
  A in the config digest, the bank digest, and the tick 0 rng digest.
  One-cab, 900 race ticks each: F and G (one seed) must be byte-identical;
  H (another seed) must differ from F in the same three digests; F must
  differ from A in the config and race plan digests, and its input digests
  must equal A's at race tick 0, differ from A's at every later tick, and
  equal H's at every tick; I (from the attract demo race) and J (37 ticks
  late, an odd launch timer offset from F) must equal F as C and E equal
  A. K is A with `--arcade-roster-proof-hold`, a stall hold of 45 tick
  periods at race tick 300 in the race hold loop, and must equal A in
  every line but its hold line. Wherever the checker requires rng, input,
  and drivers equal, it also requires the V4 digests equal, and every
  tick's V4 topology digest must be the unavailable summary's. The
  one-cab runs go through the green light, bot driving, and 1P race
  physics, digested through the rng, rcontrol, and topology-free drivers
  digests (whose drivers candidate leaves the Physics group out, RS-13)
  and the V4 digests (whose drivers domain includes it); the retail
  1P HUD's uninitialized `pos.y` read (`game/UI/UI_Rank.c`), which stops
  a Debug 1P race unfixed, is fixed in place (a98dccbe8).
- **Fixed VBlank pacing (RS-18, LR-7).** The roster proof and a linked
  race run with host-local fixed VBlank pacing
  (`Platform_SetFixedVBlankPacing`), so a late host frame emits no
  catch-up VBlanks and every race tick emits exactly 2. `main.c` turns it
  on for the proof; the arcade-link host turns it on for a linked race on
  the Launch frame (`NativeArcadeLinkHost_RaceBegin`) and off on the
  Disarm frame and at shutdown, touching only a pacing it turned on. Every
  other run keeps the default catch-up pacing, in which a late frame
  raises `elapsedTimeMS` and so changes the race. Each extra VBlank also
  increments `gGT->frameTimer_Confetti` (`game/MAIN/MainDrawCb.c:25`,
  while not paused), which feeds the particle oscillators and through them
  MixRNG draws. In a linked race under fixed pacing, a host hitch instead
  puts that cabinet behind for good: the other cabinet leads by up to
  `D + 1` ticks, then holds.
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

The native lockstep protocol is complete and transport-agnostic. A real
socket transport, a connect/handshake protocol, a lobby data/state layer, and
the arcade-link screens and host adapter exist and are tested on top of it
(see below).

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
  `NativeReplaySchedulerV4`'s latch-once mismatch report. A peer digest for
  a frame not yet recorded locally is parked, at most `D` per peer, and
  compared when that frame is recorded, so a cabinet that leads by up to
  `D + 1` ticks is not a desync; a digest further ahead is the
  session-local fault `VERIFY_AHEAD`, never on the wire
  (`docs/LOCKSTEP_RACE_MILESTONE.md` LR-11).
- The whole protocol/window/session stack is fully covered by unit tests, a
  fault-injection integration test that drives loss, delay, reorder, and
  duplication over `native_virtual_datagram`, and a structural isolation test.
- `native_virtual_datagram` remains a test-only, two-endpoint in-memory
  delivery harness (DROP, DELIVER, DUPLICATE, explicit reordering); it has no
  wire encoding, OS transport, peer search, or version commitment, and the
  protocol library never links it.
- `NativeMatchConfigV1.protocolVersion` is a reserved field, not a wire
  format, though the lockstep bundle copies and compares it.
- **Failure handling, peer drop, and rematch** (`native_lockstep_match_outcome.c`/`.h`,
  `native_lockstep_match_roster.c`/`.h`, `native_lockstep_rematch.c`/`.h`)
  build a policy layer on top of the session. `NativeLockstepMatchOutcome`
  turns an indefinitely retried stall into a latch-once, bounded
  `STALL_TIMEOUT` outcome after a configurable number of consecutive stalled
  polls (default 180 frames, range 30-600; the arcade-link adapter defaults
  to 90, 3 s at the 30 Hz game loop), mirroring the
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
  same way the protocol/window/session stack is. Only the arcade-link
  adapter composes it, driving the results and rematch screens (see
  Arcade-link wiring); in a race the lockstep drive feeds it every take's
  result through `NativeArcadeNetplay_OnTakeResult`. It makes no change to
  the topology lease, canonical state, or replay wire formats.
- **Real transport, handshake, and lobby** (`native_udp_transport.c`/`.h`,
  `native_lockstep_handshake.c`/`.h`, `native_lockstep_peer_link.c`/`.h`,
  `native_lobby_state.c`/`.h`) add a real socket underneath the stack above.
  A Winsock2 UDP socket transport leaf moves raw bytes over an actual OS
  socket, proven with a real two-OS-process loopback test. A
  connect/handshake protocol exchanges and validates a full
  `NativeMatchConfigV1` proposal between two peers — an explicit
  accept/reject negotiation, not automatic reconciliation of differing
  proposals — before a lockstep session opens, and rejects the match
  cleanly before any session opens when identities disagree. It is transport-agnostic (encodes to
  and decodes from caller-owned buffers only, no socket dependency) and is
  fault-tested against `native_virtual_datagram` the same way the lockstep
  protocol was, in addition to running over the real transport. A
  real-transport integration module is the first production code calling
  `NativeLockstepSession_Open`/`_ComposeBundle`/`_AcceptBundle` against a
  real OS socket instead of a test harness, proven with a genuine
  two-OS-process test that opens real sockets, completes a real handshake,
  opens a real session, and exchanges real lockstep bundles for dozens of
  frames with both sides reaching session mode `RUNNING` and no divergence
  or fault. The peer link drops and counts any bundle of another match
  identity instead of handing it to the session, both while staged and
  while RUNNING, so a stale bundle from a finished match never faults a
  rematch's session (a corrupt record still faults); and it has a verbatim
  128-byte bundle send for the race drive's resends
  (`NativeLockstepPeerLink_SendBundleVerbatim`). A lobby/waiting-flow
  policy layer cycles through a
  caller-supplied candidate peer-address list with a bounded per-candidate
  attempt budget, exposing a small state (`WAITING_FOR_PEER` /
  `HANDSHAKING` / `READY` / `REJECTED` / `PEER_LOST`) that the arcade-link
  adapter maps onto the lobby screens; the layer itself stays a data/state
  layer with no menu, input, or drawing.
  `native_virtual_datagram` remains test-only and none of these new modules
  link it in production; only their fault-injection test fixtures do, the
  same posture the lockstep protocol library already established. See
  `docs/LOBBY_MILESTONE.md` for full design detail.
- **Arcade-link wiring** connects both layers to a small set of screens.
  `native_arcade_menu_input` turns one player's held buttons into
  release-to-arm, rising-edge wheel/pad navigation; `native_arcade_flow` is
  the pure screen state machine (lobby, match found, select, select result,
  racing, results, rematch wait, exit); `native_arcade_netplay` is the only
  module that composes the match-select session with the lobby, outcome,
  roster, and rematch layers (the host glue also reads the match-select
  rules tables, for its previews only), and agrees a rematch implicitly by
  deriving the new seed from the proposal of the most recent lobby that
  reached READY;
  `native_arcade_link_options` parses the host-local CLI options and builds
  the fixed fixture (the lobby base) on the real bot rules (bots by the
  retail 2P AI set at medium difficulty, `botRulesDigest` from
  `NativeArcadeBotRules_DigestV1`); `native_arcade_link_host` is the
  game-facing singleton.
  Match select runs between MATCH FOUND and the race: each player picks a
  character and votes on the track and the lap count, and a disagreement
  resolves to a draw seeded from both cabinets' nonces. Three pure modules
  implement it: `native_match_select_rules` (tables, seed, draws, the
  unique-character and retail 2P bot rules, and the resolved-config
  builder), `native_match_select_message` (a 64-byte select record), and
  `native_match_select_session` (one cabinet's select state machine). The
  records travel on a generic 64-byte aux route of
  `native_lockstep_peer_link`. The resolved config is re-validated by a
  relink handshake and is what START_RACE launches.
  The race launch (Task 7, `docs/RACE_LAUNCH_MILESTONE.md`): after the
  relink the netplay adapter runs a launch agreement, exchanging 64-byte
  launch records (`native_arcade_launch`) on the same aux route, and the
  flow starts the race (START_RACE) only on the relink READY with the
  launch committed; a relink that completes on one side only launches
  neither cabinet. The host exposes the exact agreed config
  (`NativeArcadeLinkHost_GetAgreedConfig`), the racing query, and a local
  race-failure report that ends RACING as RESULTS LINK ERROR without
  telling the peer.
  Under `game/MAIN/`, `MainArcadeLinkLayout` (pure layout),
  `MainArcadeLinkPolicy` (pure frame-ownership policy), and `MainArcadeLink`
  (a thin `CTR_NATIVE` hook at the RECTMENU seam in
  `MainFrame_RenderFrame`) draw the screens with the retail menu primitives.
  Race frames (the flow on RACING off the idle main-menu level) are ticked,
  not owned: the hook ticks the host and leaves the retail race's input
  alone. On START_RACE the hook logs the agreed match and hands the launch
  to the live race caller `MainArcadeRaceLaunch`, which
  `MainFrame_RenderFrame` steps right after the hook and whose decisions
  come from the pure core `MainArcadeRaceLaunchCore`. From the title
  launch window it arms and launches `MainArcadeRaceSetup` with the agreed
  config, logs one line per validated race with the config, race plan,
  bot setup plan, and bank digests, reports an Arm, Launch, setup, or
  bounded-wait failure as a local race failure, returns to the main-menu
  level after the race, and Disarms there (at once after an Arm or Launch
  failure at the title; nothing to Disarm after a window timeout). From
  race tick 0 it drives the race in lockstep (below). While a race setup
  is not IDLE the pause-menu vibration toggle does nothing, and sound IDs
  stay out of cross-cabinet identity (isolation-tested).
  Game code names none of the lockstep, failure-handling, or match-select
  modules, and isolation tests enforce it. Everything is dormant unless
  `--arcade-link` or `--arcade-link-preview` is given; both are rejected
  with any replay
  option, and quick states are disabled while either is active. See
  `docs/GAME_LOOP_UI_MILESTONE.md`, `docs/MATCH_SELECT_MILESTONE.md`,
  `docs/RACE_LAUNCH_MILESTONE.md`, and `docs/LOCKSTEP_RACE_MILESTONE.md`.
- **In-race lockstep drive** (Task 8, `docs/LOCKSTEP_RACE_MILESTONE.md`,
  defaults LR-1..LR-76). One lockstep frame is one race tick at 30 Hz,
  with input delay `D` = 2 (the drive refuses `D` above 3). The pure drive
  core (`native_arcade_race_drive`) and its glue in
  `native_arcade_link_host` hold the per-tick decisions and the I/O; the
  race caller reaches them only through `NativeArcadeLinkHost_RaceBegin`,
  `_RaceStep`, `_RaceHold`, and `_RaceEnd`, and game code names no
  lockstep token. On each race tick, right after the arcade-link hook, the
  caller projects the live V4 state, and the drive records its digests,
  submits the cabinet's own sample (host controller slot 0: wheel, pad,
  or keyboard, read by
  `Platform_InputSampleLocalPad` without touching the installed pads),
  sends the new bundle and resends the kept ones verbatim, polls, and
  takes the frame. Both cabinets then install the same committed pads:
  CAB1's player on pad 0, CAB2's on pad 1, every pad normalized to a
  connected pad with START released, so a linked race cannot pause.
  - A stalled take holds the simulation in `MainArcadeRaceHold` with no
    VBlank, pumping host events and servicing the link and the launch
    linger; after 10 periods it shows WAITING FOR OPPONENT in the game
    font (read-only from the host's VRAM copy; a 5x7 block font is the
    fallback). The race resumes when input arrives.
  - The race ends on END_OF_RACE, or 900 ticks after the first human
    finish (the finish grace), with an 18000-tick backstop, each as RACE
    COMPLETE on both cabinets. A stall of 90 periods (3 s) or a dropped
    peer ends as OPPONENT DISCONNECTED (the start wait allows 900 periods
    for the other cabinet's load); a desync as RACE OUT OF SYNC, logged
    at most once per race by a detecting cabinet with the race tick, the
    domain mask, and both digests (a cabinet that does not detect it may
    time out instead); a protocol fault or a local drive failure as LINK
    ERROR. Every end reaches RESULTS, then the return load, with neutral
    pads from the end frame until the clear.
  - The drive state, kept bundles, parked digests, pacing flag, hold, and
    banner are host-local: never in a checkpoint, replay, or canonical
    state.
- **Two-process launch gate (internal builds).**
  `--arcade-link-autopilot <report path>` (needs `--arcade-link`; rejected
  with replay options, `--arcade-roster-proof`, `--exit-after-frame`, and in
  non-internal builds) drives one link cabinet through START, the select,
  race 1, REMATCH, race 2, REMATCH, race 3, and EXIT by replacing the
  hook's enter decision and held menu buttons on owned LINK frames. In a
  race it supplies the cabinet's sample: CROSS held and steering toward
  the next restart point, closed-loop. Its internal options also set a
  race tick cap (`--arcade-link-autopilot-race-ticks`) and inject faults
  (`--arcade-link-autopilot-freeze <t>`, `--arcade-link-autopilot-desync
  <t>`); while it runs, every race tick logs its V4 digest line.
  `tools/arcade-link-launch-check.ps1` (ctest `arcade_link_launch`) runs
  cab1 and cab2 over 127.0.0.1 (ports 7101 and 7102 under ctest; the
  script's default is 7001 and 7002) with a 6000-tick cap:
  race 1 is a natural finish in which cab2 freezes for 45 periods at race
  tick 600 and cab1 must hold and resume; in race 2 cab2 flips a CONTROL
  digest at race tick 300 and a cabinet must report RACE OUT OF SYNC for
  that tick; in race 3 cab2 is killed and cab1 must reach OPPONENT
  DISCONNECTED within the stall timeout plus a 25% margin, then EXIT with
  a `result PASS (0)` report. It requires every per-tick digest line both
  cabinets logged to be equal, race 1 to end the same way on the same
  tick on both (END_OF_RACE or the finish grace), race 1's hold banners
  to be in the game font, each race's agreed match and digests equal
  across the two, and each race's config different from the one before.
  Each cabinet writes one frame capture in race 1 (retail imagery, never
  committed).
- None of this has been exercised over real two-cabinet LAN hardware or with
  a real G29 (only two real OS processes on one machine over loopback); that
  remains open before step 6.

## Topology lease

The topology lease authority is **retire-only**. A private, process-local owner
retires it at audited destructive boundaries (before `StateZero` clears `gGT`,
before the MEMPACK arena wipe, on `LOAD_LevelFile`, on the cold-boot ten-stage
load entry, on `LOAD_Hub_SwapNow`, and on a validated checkpoint restore before
overlay reset). The inactive `LOAD_Hub_ReadFile` preload is intentionally not
hooked. No game path acquires, observes, captures, activates, publishes,
serializes, or networks a lease, and the owner is excluded from checkpoint
regions. The live V4 drivers extraction reads each bot's nav-path pointer
`NavHeader.last` in `MainCanonicalDrivers_BotNavIndex`, check-only, to
prove the bot's nav frame lies in its own path (owner ruling LR-17). That
read is not a lease operation: it is never written and never used to
acquire, activate, capture, or publish the lease, and an isolation pin
(`main_arcade_race_digest_isolation`) allows a `NavHeader` `last` read in
the first-party canonical and arcade game sources only there and in
`MainCanonicalTopologyLease_ObservePostInit`.

## Build and test

Run from the repository root:

```sh
cmake --preset windows-msvc-x86
cmake --build build-msvc-x86 --config Debug
# Inner loop, while iterating on a change: the fast suite (163 tests).
ctest --test-dir build-msvc-x86 -C Debug -LE live -j 8 --output-on-failure
# Task scope: also each live area the change reaches.
ctest --test-dir build-msvc-x86 -C Debug -L live-link --output-on-failure
ctest --test-dir build-msvc-x86 -C Debug -L live-roster -j 8 --output-on-failure
ctest --test-dir build-msvc-x86 -C Debug -L live-render --output-on-failure
ctest --test-dir build-msvc-x86 -C Debug -L live-package --output-on-failure
# Milestone gate, once before the work is done: the full suite (168 tests).
ctest --test-dir build-msvc-x86 -C Debug -j 8 --output-on-failure
```

The inner-loop and task-scope runs are checks while working; they do not
replace the full suite. The full parallel suite runs once at the milestone
gate, and the work is not done until it passes.

Use `build-msvc-x86`; other `build-msvc-x86-*` directories are from earlier
milestones. Keep `-C Debug` even with `-N`: this multi-configuration build
tree sets the test labels per configuration, so without `-C` the label
filters select nothing (`-L`) or everything (`-LE`). LF-to-CRLF warnings
are benign.

Five tests carry the ctest label `live` plus one area label:
`arcade_link_preview_render` (`live-render`, about 47 s),
`arcade_roster_determinism_two_cab` and `arcade_roster_determinism_one_cab`
(`live-roster`, about 273 s each), `arcade_link_launch` (`live-link`, about
242 s), and `package_arcade_smoke` (`live-package`, about 242 s).
`ctest -LE live` excludes all five; the default run includes them. They are
parallel-safe (no RUN_SERIAL or RESOURCE_LOCK). Measured in Debug: the fast
suite (163 tests) takes 86 s serial and 36 s with `-j 8`, and all five live
tests together with `-L live -j 8` take 273 s (the old serial full suite
took about 605 s); `-j 16` gave the fast suite no gain over `-j 8`. Each
live test writes only under its own directory of the build tree, and the
two link gates use distinct loopback ports (`arcade_link_launch` 7101 and
7102, `package_arcade_smoke` the package's 7001 and 7002; the fast suite's
socket tests use 48000-48600).
Runs from the build tree still read the repository's `memcards\` and write
the gitignored `Crash Team Racing.log` in the repository root (shared,
diagnostic only, never read by a check). All live tests are Windows only
and skip (77) when `assets/ctr-u.bin` is absent, no display is available,
or the build rejects the internal-only option they use. A skip is not a
pass.

The `arcade_link_preview_render` test renders all 17 arcade-link previews
with `ctr_native.exe` and checks each capture (the game's own window
framebuffer, not the desktop), under
`build-msvc-x86\arcade_link_preview_captures\<config>`. The two
`arcade_roster_determinism_*` tests run
`tools/arcade-roster-proof-check.ps1` (docs/ROSTER_MILESTONE.md section
3.4) with `-Group two-cab` (runs A-E and the stall hold K) and `-Group
one-cab` (runs F-J plus its own A, the base of the cross-profile checks);
together they run every check of the unsplit eleven-run proof (`-Group all`,
still the script's default), each prints which checks it ran, and the fast
test `arcade_roster_proof_groups` pins the split. They write under
`build-msvc-x86\arcade_roster_proof\<group>\<config>`. The
`arcade_link_launch` test runs `tools/arcade-link-launch-check.ps1`, the
two-process three-race lockstep gate (`docs/RACE_LAUNCH_MILESTONE.md`
RL-15, `docs/LOCKSTEP_RACE_MILESTONE.md` LR-16 and LR-76), on ports 7101
and 7102 (`-Cab1Port`/`-Cab2Port`; the script's default is 7001 and 7002);
it also skips without a known build identity (a build from a tree with
uncommitted or untracked changes), because `--arcade-link` needs one. It
writes its reports, logs, and the two race-1 frame captures
(`cab1.race1.bmp`, `cab2.race1.bmp`, retail imagery, never committed)
under `build-msvc-x86\arcade_link_launch\<config>`; the checker's own
limit is 780 s and ctest's TIMEOUT 900 s. The `package_arcade_smoke` test
(docs/PACKAGING.md "Package smoke gate") runs the same gate on a staged
package copy under `build-msvc-x86\package_smoke\<config>`, with the
package's config files.

`ctr_native.exe` needs a connected desktop session with a display. Without
one, platform init fails, the SDL error is logged, and the exe exits 1. SDL
assertions are logged and ignored rather than shown as a dialog, and the G29
stays enabled (HIDAPI is not disabled). When the exe owns its console window
(e.g. launched by double-click), every early exit waits for Enter.

## Packaging

See `docs/PACKAGING.md` (decisions PK-1..PK-10).

- `tools/package-arcade.ps1`, run from a clean tree after the full Debug
  and Release suites pass, builds the Release `ctr_native` (the tested
  `CTR_INTERNAL` build, PK-1) and writes one self-contained folder,
  `build-msvc-x86\package\ctr-arcade-<short12>\`: the static
  `ctr_native.exe`, the config templates `cab1.cfg` and `cab2.cfg`,
  `README.txt`, and `MANIFEST.txt` (every file's size and SHA-256). Its
  retail-data guard refuses any other file. The package holds no game
  data.
- Each cabinet runs from `arcade.cfg` next to the exe, a copy of its
  template (`--config <path>` selects another file). Its keys are
  `data_dir` (the folder with the cabinet's own `ctr-u.bin`), `seat`,
  `port`, `peer`, and `fullscreen`; the link group goes through the same
  parser as the `--arcade-link` flags, and the command line overrides the
  file per group. The config is host-local: it never reaches match
  identity, simulation, replay, or canonical state.
- A fresh cabinet needs no `memcards\` save: the first linked race's Arm
  marks the game options loaded (see Live race setup) and logs "no
  memcard options loaded; marked the options loaded (live settings kept)".
- `tools/package-arcade-smoke.ps1` runs the two-process loopback gate on a
  copy of a package, with no `memcards\`, driven by the package's own
  config files. The live ctest `package_arcade_smoke` runs it on a staged
  package of the build's own exe; the fast ctests `package_arcade_stage`
  and `package_arcade_content_guard` check the stage mode and the guard.
- The owner's per-cabinet steps (data, config lines, the firewall rule,
  the same-exe and same-disc hash checks, starting) are
  `docs/PACKAGING.md` "Per-cabinet setup". A linked cabinet needs the disc
  image `ctr-u.bin` (the link's content identity); extracted files serve
  an unlinked run only, and a linked `data_dir` holds only `ctr-u.bin`
  (extracted files beside it override the disc image unhashed; PK-6).
  `arcade.cfg`, `memcards\`, and the log are
  per-cabinet and stay out of any folder sync (PK-10).

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
  first-fault reports, parked early peer digests).
- Failure handling: `platform/native_lockstep_match_outcome.c`,
  `include/platform/native_lockstep_match_outcome.h` (stall-timeout policy
  and outcome latch); `platform/native_lockstep_match_roster.c`,
  `include/platform/native_lockstep_match_roster.h` (peer-lifecycle roster
  and drop policy); `platform/native_lockstep_rematch.c`,
  `include/platform/native_lockstep_rematch.h` (rematch config builder).
- Real transport, handshake, and lobby: `platform/native_udp_transport.c`,
  `include/platform/native_udp_transport.h` (Winsock2 UDP socket leaf);
  `platform/native_lockstep_handshake.c`,
  `include/platform/native_lockstep_handshake.h` (connect/handshake
  protocol); `platform/native_lockstep_peer_link.c`,
  `include/platform/native_lockstep_peer_link.h` (real-transport session
  integration, the foreign-identity drop, the verbatim bundle send, and the
  64-byte aux route: `NativeLockstepPeerLink_SendAux`, `_TakeAux`);
  `platform/native_lobby_state.c`,
  `include/platform/native_lobby_state.h` (candidate-cycling lobby policy
  layer).
- Arcade-link wiring: `platform/native_arcade_menu_input.c`,
  `include/platform/native_arcade_menu_input.h` (menu navigation);
  `platform/native_arcade_flow.c`, `include/platform/native_arcade_flow.h`
  (screen state machine); `platform/native_arcade_netplay.c`,
  `include/platform/native_arcade_netplay.h` (host adapter);
  `platform/native_arcade_link_options.c`,
  `include/platform/native_arcade_link_options.h` (CLI options and fixture);
  `platform/native_arcade_link_host.c`,
  `include/platform/native_arcade_link_host.h` (game-facing singleton);
  `game/MAIN/MainArcadeLinkLayout.{c,h}` (screen layout),
  `game/MAIN/MainArcadeLinkPolicy.{c,h}` (frame-ownership policy),
  `game/MAIN/MainArcadeLink.{c,h}` (live hook, called from
  `game/MAIN/MainFrame_RenderFrame.c`);
  `tests/main_arcade_link_view_layout_test.c` (every preview and live host
  view passes the layout).
- Race launch (Task 7): `platform/native_arcade_launch.c`,
  `include/platform/native_arcade_launch.h` (launch record codec and
  agreement state); `game/MAIN/MainArcadeRaceLaunchCore.{c,h}` (pure
  launch decision core, library `ctr_native_arcade_race_launch_core`);
  `game/MAIN/MainArcadeRaceLaunch.{c,h}` (live race caller, stepped from
  `game/MAIN/MainFrame_RenderFrame.c`); the RL-13 vibration guard in
  `game/MAIN/MainFreeze.c`. Internal two-process gate:
  `platform/native_arcade_link_autopilot.c`,
  `include/platform/native_arcade_link_autopilot.h` (options, steering,
  fault injections, three-race decisions, report);
  `game/MAIN/MainArcadeLinkAutopilot.{c,h}` (glue);
  `tools/arcade-link-launch-check.ps1` (checker).
- In-race lockstep drive (Task 8): `platform/native_arcade_race_drive.c`,
  `include/platform/native_arcade_race_drive.h` (pure drive core, library
  `ctr_native_arcade_race_drive`); the drive glue and `RaceBegin`,
  `RaceStep`, `RaceHold`, `RaceEnd` in `platform/native_arcade_link_host.c`;
  `NativeArcadeNetplay_OnTakeResult` and the race hold service
  `NativeArcadeNetplay_RaceService` in `platform/native_arcade_netplay.c`;
  `Platform_InputSampleLocalPad` in `platform/native_input.c` (local
  sample seam);
  `game/MAIN/MainArcadeRaceDigest.{c,h}` (live V4 projection);
  `game/MAIN/MainArcadeRaceHold.{c,h}` (stall hold loop and banner) and
  `game/MAIN/MainArcadeRaceHoldCore.{c,h}` (its pure period core, library
  `ctr_native_arcade_race_hold_core`); `platform/native_hold_banner.c`,
  `include/platform/native_hold_banner.h` (game-font banner decode and
  layout, library `ctr_native_hold_banner`); `Platform_HostWaitMs`,
  `Platform_HostClockUs`, and `Platform_PresentVRAMDisplayBanner{,Glyphs}`
  in `include/platform.h` (host-local wait, clock, and banner present,
  called only by the hold module). Isolation tests:
  `tests/native_arcade_race_drive_isolation_test.cmake`,
  `tests/main_arcade_race_hold_isolation_test.cmake`,
  `tests/native_host_wait_isolation_test.cmake`,
  `tests/main_arcade_race_digest_isolation_test.cmake`, and
  `tests/arcade_roster_proof_autopilot_isolation_test.cmake`.
- Match select: `platform/native_match_select_rules.c`,
  `include/platform/native_match_select_rules.h` (tables, seed, draws,
  resolution, resolved-config builder);
  `platform/native_match_select_message.c`,
  `include/platform/native_match_select_message.h` (64-byte select wire
  codec); `platform/native_match_select_session.c`,
  `include/platform/native_match_select_session.h` (select state machine).
- Roster and RNG ownership (step 3): `platform/native_arcade_bot_rules.c`,
  `include/platform/native_arcade_bot_rules.h` (bot rules per profile, the
  TWO_CAB v1 and ONE_CAB 1P v1 encodings and digests, seed derivation,
  config validator);
  `game/MAIN/MainArcadeRaceSetupPlan.{c,h}` (pure plan and mode-bit
  audit), `game/MAIN/MainArcadeRaceSetupFacts.{c,h}` (pure facts builder),
  `game/MAIN/MainArcadeRaceSetupCore.{c,h}` (pure decision core, state
  machine, RS-17 counter audit), `game/MAIN/MainArcadeRaceSetup.{c,h}`
  (live adapter and the seam the Task 7 race caller uses, hooked from
  `MainInit_FinalizeInit`).
- Live roster proof (internal builds): `platform/native_arcade_roster_proof.c`,
  `include/platform/native_arcade_roster_proof.h` (options, proof config,
  scripted pads, report format, exit codes);
  `game/MAIN/MainArcadeRosterProof.{c,h}` (game hook);
  `platform/native_vblank_pacing.c`, `include/platform/native_vblank_pacing.h`
  (the pure pacing decision behind fixed VBlank pacing, used by the proof
  and by linked races);
  `tools/arcade-roster-proof-check.ps1` (the checker: eleven runs, five
  two-cab and five one-cab runs and the two-cab hold run K, split by
  `-Group two-cab|one-cab` into the two `arcade_roster_determinism_*`
  ctests; `tests/arcade_roster_proof_groups_test.cmake` pins the split).
- Presentation options (host-local): `platform/native_display_config.c`,
  `include/platform/native_display_config.h` (render scale, texture filter),
  `platform/native_frame_capture.c`, `include/platform/native_frame_capture.h`
  (unattended frame capture); renderer seam in `platform/native_renderer.c`.
- Preview capture check (offline, never linked by `ctr_native`):
  `platform/native_capture_check.c`, `include/platform/native_capture_check.h`
  (RGB-only BMP checker for arcade-link preview captures);
  `tools/arcade_link_capture_check.c` (CLI
  `ctr_native_arcade_link_capture_check <capture.bmp> <screen>`);
  `tools/arcade-link-preview-check.ps1` (renders and checks all 17 previews
  plus the default path; `-Png` writes alpha-stripped review PNGs, since the
  capture alpha byte is the PS1 mask bit).
- Startup robustness: `platform/native_sdl_assert.c`,
  `include/platform/native_sdl_assert.h` (SDL assertion handler);
  `Platform_Init` in `platform/native_platform.c` (fail-fast platform init).
- Packaging: `tools/package-arcade.ps1` (package, stage mode, and
  retail-data guard), `tools/package-arcade-smoke.ps1` (smoke gate),
  `tools/package/cab1.cfg`, `cab2.cfg`, `README.txt` (templates and
  operator guide); `platform/native_arcade_config.c`,
  `include/platform/native_arcade_config.h` (config file parser, called
  only by `main.c`).
- Unity build chain: `game/game_unity.h` (ordered includes; add new game `.c`
  files here). Standalone libraries are declared in `CMakeLists.txt`.
- Related docs: `docs/ARCADE_FORK.md`, `docs/TOPOLOGY_LEASE_AUTHORITY.md`,
  `docs/REPLAYS.md`, `docs/MEMORY_MODEL.md`, `docs/G29_INPUT.md`,
  `docs/GAME_LOOP_UI_MILESTONE.md`, `docs/MATCH_SELECT_MILESTONE.md`,
  `docs/ROSTER_MILESTONE.md`, `docs/RACE_LAUNCH_MILESTONE.md`,
  `docs/LOCKSTEP_MILESTONE.md`, `docs/LOCKSTEP_RACE_MILESTONE.md`,
  `docs/PACKAGING.md`.

## Rules and constraints

- Do not wire acquire, activate, capture, or publish for the topology lease
  without a new live-cabinet gate, deterministic capture evidence, and review.
- Do not add the lease owner to checkpoints, replay, or canonical state.
  Isolation tests enforce this.
- Do not add a retire hook to `LOAD_Hub_ReadFile`.
- Owner ruling LR-17: the live lockstep digest may read the bot nav-path
  pointer `NavHeader.last`, check-only. That does not permit writing it or
  any lease acquire, activate, capture, or publish.
- Game code names no lockstep, failure-handling, or match-select module;
  the race caller reaches the drive only through the host's calls.
  Isolation tests enforce this.
- The repository must never contain retail game data, BIOS files, or extracted
  retail assets. The operator supplies `assets/ctr-u.bin` (gitignored, raw
  MODE2/2352 layout). Frame captures of a race or a hold banner are retail
  imagery: they stay under `build-msvc-x86` and are never committed.
- Source and build output stay in `C:\re-tools\ctr-native`. Only a tested,
  versioned bundle is promoted to `C:\Arcade\games\ctr-native`, and CAB2
  receives it through the fleet rsync path. The canonical fleet plan lives on
  CAB1 in `C:\Arcade\docs\ctr-native`.
- Upstream updates land on a trial branch first and are promoted to `arcade`
  only after the deterministic replay and network suites pass.
- First-party native code targets portable C17 with compiler extensions
  disabled.

## Next work

Tasks 7 and 8 and v1 packaging are complete.
- Linked races launch and run in lockstep end to end
  (`docs/LOCKSTEP_RACE_MILESTONE.md`, defaults LR-1..LR-76).
- `tools/package-arcade.ps1` builds a self-contained Release package from a
  clean tree (`docs/PACKAGING.md`, defaults PK-1..PK-10, and the Packaging
  section above). Its smoke test, `package_arcade_smoke`, races three times
  from the package folder, with no memcard save present.
- The suite is 168 tests: the full run with `-j 8` takes about 280 s, and
  `-LE live -j 8` about 35 s. Live tests carry area labels (`live-link`,
  `live-roster`, `live-render`, `live-package`). Per-change checks use the
  fast suite plus the affected area; the full suite runs once per
  milestone.
- Menu sounds SND-1..11 are assumed approved until live testing. The
  input delay (D = 2, 3 ticks) awaits a feel test on the cabinets.
- v1 ships the tested `CTR_INTERNAL` build; its autopilot and fault
  options are opt-in command-line flags only. A non-internal release
  target (a CMake switch, its own build, and a smoke test) waits for the
  owner.

Deployed. v1 runs on both cabinets from `C:\Arcade\games\ctr-native`.
Each cabinet provisions and checks itself after every fleet sync
(`C:\Arcade\scripts\setup-ctr-native.ps1 -Auto`). A linked race over the
real LAN with both G29s works (owner-confirmed). The owner accepts the 4:3
black bars and keeps the split screen.

Owner priorities: finish the last stretch goals, so the port could ship as
open source.

1. Single-player race (the solo / ONE_CAB flow; `docs/SOLO_CAB_MILESTONE.md`,
   SOLO defaults). When the peer is silent, the lobby offers a solo race
   (1 human + 7 bots). The link poll keeps running, and a peer that wakes
   is linked at the next lobby. Done so far: the `arcade.cfg` keys
   `render_scale` and `texture_filter`, the plan, and SOLO-S1; later slices
   are in progress.
2. Automatic discovery (stretch goal 8). Cabinets find each other on the
   subnet with no configured IPs, so a new install needs no per-cabinet
   network config. A continuous background poll finds peers that wake
   later. ONE_CAB versus TWO_CAB follows from whether a peer is found.
   Discovery replaces the static `peer` in `arcade.cfg`, which stays as an
   optional override. The handshake still checks build and content
   identity.
3. Small cabinet polish, queued behind 1 and 2:
   - Skip the Sony and Naughty Dog boot splashes on arcade/link configs.
     Boot still runs all of StateZero's init, and levelID goes straight to
     MAIN_MENU_LEVEL (`game/MAIN/MainMain.c:744`).
   - Always draw other karts as full 3D in arcade races. The far LOD looks
     like stacked sprites in the split screen. The LOD choice is
     presentation-only; see `MainFrame_RenderFrame.c:750-753` and
     `RenderBucket_QueueExecute.c:1213-1269`, to be verified.
4. Shelved (owner): stretch goals 9 and 10 (Oxide Station, unlock
   everything and Turbo Track). Dropped (owner): 11 (16:9) and 13
   (per-cabinet full screen). Not prioritised: 12 (G29 force feedback).
5. Open items, none blocking:
   - Task 8:
     - A divergence found only by the Tick that closes the link while
       leaving RESULTS is not logged (LR-70).
     - The detecting cabinet logs no "drive end" line when the flow
       leaves RACING.
     - The race-tick cap is checked across cabinets only by the gate
       (LR-60).
     - Audio during a hold has not been observed.
     - Whether RESULTS shows standings is open (risk 16).
     - TOPOLOGY is not compared (risk 17).
   - Test hardening from packaging:
     - Nothing pins the CMake registration of the roster split or its
       `-Ticks` value.
     - The one-cab group's run A is not byte-checked against the two-cab
       group's.
     - No test pins the `README.txt` content.
   - With neGcon/Jogcon pads, a different saved `data.rwd` on the two
     cabinets could affect steering. This predates packaging.
