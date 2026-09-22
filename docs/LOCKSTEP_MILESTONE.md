# Lockstep milestone

Self-contained design brief and task list for integration step 4, the native
lockstep protocol, on branch `arcade`. Read `AGENTS.md` and `docs/HANDOFF.md`
first. Line numbers below were verified at commit `dffe3e8b7`; re-check them
before quoting to a subagent.

This document is an expansion of the milestone statement in
`docs/HANDOFF.md:146-153` ("Next work") and adds no scope beyond it:

> Integration step 4. Design the lockstep protocol against the existing seams:
> use `NativeMatchConfigV1` as durable match identity, `NativeCanonicalStateV4`
> digests for per-frame verification, and `native_virtual_datagram` for fault
> injection. Define the fixed-delay frame bundle and the first-divergence
> report, add unit and isolation tests, and keep the protocol
> transport-agnostic until a real wired-LAN socket layer is separately gated.

## 1. Verified seam facts

Everything in this section was read out of the tree, not inferred. Where it
contradicts an earlier summary, section 8 records the contradiction.

### 1.1 Match identity: `NativeMatchConfigV1`

`include/platform/native_match_config.h`.

- Constants (`:10-22`): `NATIVE_MATCH_CONFIG_V1_MAGIC` `0x31434d4e`,
  `..._VERSION` 1, `..._PROTOCOL_VERSION` 1,
  `..._CANONICAL_SCHEMA_VERSION` 5, `..._REPLAY_FORMAT_VERSION` 4,
  `..._SLOT_COUNT` 8, `..._SLOT_RESERVED_BYTES` 4,
  `..._RESERVED_BYTES` 28, `..._ENCODED_BYTES` 256,
  `..._DIGEST_ALGORITHM_NAME "SHA-256"`.
- `enum NativeMatchSlotRole` (`:24-30`): `INACTIVE` 0, `CAB1_HUMAN` 1,
  `CAB2_HUMAN` 2, `BOT` 3. Roles never transition (`:90`).
- `enum NativeMatchSlotLifecycle` (`:32-38`): `INACTIVE` 0, `ACTIVE` 1,
  `DISCONNECTED` 2, `FINISHED` 3. Only lifecycle is mutable, through
  `NativeMatchSlotLifecycle_CanTransition` / `_Transition` (`:91-92`).
- `struct NativeMatchConfigSlotV1` (`:40-47`): `uint8_t role`,
  `initialLifecycle`, `characterID`, `difficulty`, `reserved[4]`. Eight slots.
- `struct NativeMatchConfigV1` (`:53-74`), in declaration order:
  `configurationVersion`, `profile`, `trackID`, `gameMode1`, `gameMode2`,
  `rules`, `lapCount`, `tickRateNumerator`, `tickRateDenominator` (all
  `uint32_t`), `masterSeed` (`uint64_t`), `rngDerivationVersion`,
  `canonicalSchemaVersion`, `replayFormatVersion`, `protocolVersion` (all
  `uint32_t`), `buildIdentity[32]`, `contentIdentity[32]`,
  `slots[8]`, `botRulesDigest[32]`, `reserved[28]`.
- `NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES` is the **fixed** encoded width, 256
  bytes, and is self-describing on the wire: `Encode` writes the magic then the
  literal 256 as a size field (`platform/native_match_config.c:151`), `Decode`
  requires `NativeCodecReader_Remaining(reader) == 256` exactly (`:192`) and
  rejects a size field that is not 256 (`:226`). `EncodedSize()` returns the
  constant (`:134-136`).
- `NativeMatchConfigV1_Digest` (`platform/native_match_config.c:236-257`)
  encodes the config into a local 256-byte buffer, requires the writer to have
  produced exactly 256 bytes, and returns `SHA-256` of those bytes as a
  32-byte digest. It is a pure function of the encoded config.
- Profiles (`platform/native_match_config.c:41-78`): two-cab sets slot 0 =
  `CAB1_HUMAN`, slot 1 = `CAB2_HUMAN`, slots 2-5 = `BOT`, slots 6-7 inactive;
  one-cab sets slot 0 = `CAB1_HUMAN` and slots 1-7 = `BOT`. So in the
  two-cabinet profile **each peer owns exactly one human slot**, resolvable
  with `NativeMatchConfigV1_FindRoleSlot` (`native_match_config.h:88`).

### 1.2 Per-frame digest: `NativeCanonicalStateV4`

`include/platform/native_canonical_state_v4.h`.

- `struct NativeCanonicalStateV4` (`:25-38`) ends with
  `uint64_t domainDigests[NATIVE_CANONICAL_DOMAIN_COUNT], combinedDigest;`
  (`:37`). `NATIVE_CANONICAL_DOMAIN_COUNT` is 6
  (`include/platform/native_canonical_codec.h:25`).
- The call sequence a caller uses to obtain a frame digest is:

  1. Project or fill the `struct NativeCanonicalStateV4` for the frame.
  2. Call `NativeCanonicalStateV4_ComputeDigests(state)`
     (`native_canonical_state_v4.h:42`), which validates the state, encodes
     each of the six domains into a scratch buffer in
     `NativeCanonicalDomainOrder` and writes `state->domainDigests[i]` and
     `state->combinedDigest` transactionally
     (`platform/native_canonical_state_v4.c:31`). The workspace variant
     `NativeCanonicalStateV4_ComputeDigestsInPlaceWithScratch` (`:45-46` of the
     header, `:32-43` of the source) avoids the whole-state copy and may leave
     digest fields modified on failure.
  3. Read `state->combinedDigest` (a `uint64_t`) and, for diagnostics,
     `state->domainDigests[0..5]`.

  There is **no** separate `..._Digest()` entry point and no SHA-256 over the
  whole V4 state. The per-domain digest is `NativeCodecDigest64`, i.e. FNV-1a
  64 (`platform/native_canonical_state_v4.c:26`,
  `native_canonical_codec.h:23`). The combined digest is FNV-1a 64 over the
  60-byte sequence `{domainID u32, domainDigest u64} x 6` in
  `NativeCanonicalDomainOrder`, written into a 72-byte staging buffer
  (`platform/native_canonical_state_v4.c:27`).
- `NativeCanonicalDomainOrder` is
  `CONTROL, RNG, INPUT, DRIVERS, WORLD, TOPOLOGY`
  (`platform/native_canonical_codec.c:9-16`, IDs 1..6 from
  `native_canonical_codec.h:27-35`). Index `i` in `domainDigests` corresponds
  to `NativeCanonicalDomainOrder[i]`.
- Input carried in canonical state is
  `struct NativeCanonicalInputV1 { uint32_t padCount; struct
  NativeCanonicalInputPadV1 pads[NATIVE_CANONICAL_INPUT_PAD_COUNT]; }`
  (`include/platform/native_canonical_state.h:52-56`) with
  `struct NativeCanonicalInputPadV1 { uint8_t status, id, buttons[2],
  analog[4], connected; }` (`:43-50`). Encoded, one pad is 9 bytes and the
  whole INPUT domain is 40 bytes (`V4_INPUT_BYTES`,
  `platform/native_canonical_state_v4.c:7`; encoder at `:16`). `padCount` is
  fixed at 4 and validated (`:18`, `:29`, `:28`).

### 1.3 First-mismatch latch: `NativeReplaySchedulerV4`

`include/platform/native_replay_scheduler_v4.h`.

- `struct NativeReplaySchedulerV4MismatchReport` (`:17-18`) is exactly:

  ```c
  struct NativeReplaySchedulerV4MismatchReport { uint32_t mask, canonicalDomainMask; uint32_t expectedFrame, liveFrame;
      uint32_t expectedVsyncCount, liveVsyncCount; };
  ```

- `enum NativeReplaySchedulerV4MismatchMask` (`:10-12`): `OBSERVATION` 1,
  `PAD` 2, `VSYNC` 4, `CANONICAL_DOMAIN` 8, `COMBINED` 16.
- The latch is `Match()` (`platform/native_replay_scheduler_v4.c:16`). It is
  **latch-once**: the whole body is guarded by
  `if (s->mode != NATIVE_REPLAY_SCHEDULER_V4_MISMATCH)`, so once the mode is
  `MISMATCH` a later mismatch cannot overwrite the report. It zeroes the report
  first, then fills mask, domain mask, expected/live frame and expected/live
  VSync counts, then sets `mode = MISMATCH`. It always returns 0.
- `Poison()` (`:11`) is the sibling terminal state and deliberately refuses to
  clobber an existing `MISMATCH`:
  `if (s && s->mode != MISMATCH) s->mode = POISON;`. So a diagnostic mismatch
  always survives a subsequent protocol fault.
- The domain mask is built in `EndFrame`
  (`platform/native_replay_scheduler_v4.c:24`) as
  `domains |= UINT32_C(1) << i` per differing `domainDigests[i]`, and
  `MISMATCH_COMBINED` is set separately when `combinedDigest` differs.
- The accessor (`native_replay_scheduler_v4.h:54`,
  `platform/native_replay_scheduler_v4.c:27`) returns
  `const struct NativeReplaySchedulerV4MismatchReport *` and is
  `s && s->mode == MISMATCH ? &s->mismatch : NULL`.

This is the exact pattern the lockstep first-divergence report mirrors.

### 1.4 Test transport: `native_virtual_datagram`

`include/platform/native_virtual_datagram.h`.

- Signatures, verbatim:

  ```c
  int NativeVirtualDatagramPair_Init(struct NativeVirtualDatagramPair *pair,
      struct NativeVirtualDatagramSlot *slots, size_t queueCapacity,
      uint8_t *payloadStorage, size_t payloadCapacity);                          /* :68-70 */

  int NativeVirtualDatagramPair_AdvanceTo(struct NativeVirtualDatagramPair *pair,
      uint64_t deliveryStep);                                                    /* :73-74 */

  int NativeVirtualDatagramPair_Send(struct NativeVirtualDatagramPair *pair,
      uint32_t sender, const void *bytes, size_t byteCount,
      const struct NativeVirtualDatagramRoute *route);                           /* :79-81 */

  enum NativeVirtualDatagramReceiveResult NativeVirtualDatagramPair_Receive(
      struct NativeVirtualDatagramPair *pair, uint32_t destination, void *bytesOut,
      size_t *byteCountInOut, struct NativeVirtualDatagramMetadata *metadataOut); /* :86-88 */
  ```

- `enum NativeVirtualDatagramReceiveResult` (`:20-26`) has exactly four
  values: `EMPTY` 0, `OK` 1, `TOO_SMALL` 2, `INVALID` 3. `TOO_SMALL` writes the
  required byte count into `*byteCountInOut` and leaves the record queued
  (`platform/native_virtual_datagram.c:168-171`).
- **There is no maximum payload constant.** `payloadCapacity` is entirely
  caller-owned: `Init` rejects zero and overflow (`:87-88`), storage is
  `queueCapacity` contiguous regions of `payloadCapacity` bytes
  (`native_virtual_datagram.h:44-46`, indexing at
  `platform/native_virtual_datagram.c:70`), and `Send` fails when
  `byteCount > pair->payloadCapacity` (`:116`). A fixture chooses the width; the
  existing unit test uses 16 (`tests/native_virtual_datagram_test.c:43`, `63`).
- Fault injection is `enum NativeVirtualDatagramAction` (`:13-18`): `DROP` 0,
  `DELIVER` 1, `DUPLICATE` 2, plus `struct NativeVirtualDatagramRoute`
  (`:28-33`) carrying `firstDeliveryStep` and `secondDeliveryStep`.
  `RouteValid` (`platform/native_virtual_datagram.c:19-35`) requires DROP to
  have both steps zero, DELIVER to have `firstDeliveryStep >= currentStep` and
  `secondDeliveryStep == 0`, and DUPLICATE to have both steps `>= currentStep`.
  Therefore the supported faults are:
  - **loss** = `DROP`;
  - **delay** = `DELIVER` with a `firstDeliveryStep` far in the future;
  - **reorder** = two sends whose delivery steps invert their send order
    (delivery is ordered by `(deliveryStep, serial, deliveryOrdinal)`,
    `ComesBefore` at `:45-53`);
  - **duplication** = `DUPLICATE`, which creates two queued deliveries at
    independently chosen steps.
  There is **no corruption / bit-flip injection**, no partial delivery and no
  MTU model. `AdvanceTo` is strictly monotonic (`:73` of the header;
  `tests/native_virtual_datagram_test.c:67` asserts a backwards step fails).

### 1.5 Codec byte order

`include/platform/native_canonical_codec.h` and
`platform/native_canonical_codec.c`.

- `struct NativeCodecWriter { uint8_t *data; size_t capacity, offset; int
  failed; struct NativeCodecDigest64 *digest; }` (`:44-51`);
  `struct NativeCodecReader { const uint8_t *data; size_t size, offset; int
  failed; }` (`:53-59`).
- The codec is **fixed little-endian**, not host-endian:
  `WriteU16` (`platform/native_canonical_codec.c:165-170`), `WriteU32`
  (`:172-177`) and `WriteU64` (`:179-193`) emit an explicit byte array
  least-significant byte first. The signed writers funnel into the unsigned
  ones (`:196-213`) and the readers reconstruct two's complement explicitly
  (`NativeCodec_S8/16/32/64FromBits`, `:48-85`). Nothing casts a struct to
  bytes. The wire is therefore identical on any host regardless of native
  endianness, which is exactly what a cross-cabinet protocol requires.
- `NativeCodecDigest64` is FNV-1a 64 (`native_canonical_codec.h:23`, offset
  basis and prime at `platform/native_canonical_codec.c:6-7`,
  `Init`/`Update` at `:88`, `:96`). A writer with a non-NULL `digest` folds in
  only bytes that were successfully written
  (`native_canonical_codec.h:64`, `platform/native_canonical_codec.c:154`).
- Capacity is checked before every write and never grows
  (`NativeCodecWriter_CanWrite`, `:18-31`); a single failure latches
  `writer->failed`. The same holds for the reader (`:33-46`).

### 1.6 How a seam and its tests are registered

- Library declaration pattern (`CMakeLists.txt:155-163`, the virtual datagram
  leaf): a comment stating what the target must *not* depend on, then
  `add_library(<target> STATIC platform/<file>.c)`,
  `set_target_properties(<target> PROPERTIES C_STANDARD 17
  C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)`,
  `target_include_directories(<target> PUBLIC ${CMAKE_SOURCE_DIR}/include)`,
  and `target_link_libraries` only if genuinely needed
  (`ctr_native_match_config` links `ctr_native_canonical_codec` and
  `ctr_native_sha256`, `CMakeLists.txt:110`).
- Test registration lives under `include(CTest)` / `if(BUILD_TESTING)`
  (`CMakeLists.txt:622-623`). The pattern (`:701-710`) is
  `add_executable(<name>_test tests/<name>_test.c)`, the same three
  `set_target_properties` C17 flags, `target_link_libraries(... PRIVATE ...)`,
  `add_test(NAME <name>_unit COMMAND $<TARGET_FILE:<name>_test>)` and, for a
  structural rule, `add_test(NAME <name>_isolation COMMAND "${CMAKE_COMMAND}"
  -P "${CMAKE_SOURCE_DIR}/tests/<name>_isolation_test.cmake")`.
- Test source style (`tests/native_virtual_datagram_test.c:1-6`): include the
  seam header, `<stdio.h>`, `<string.h>`, and one
  `#define CHECK(expression) do { if (!(expression)) { fprintf(stderr,
  "%d: %s\n", __LINE__, #expression); return 1; } } while (0)`. `main(void)`
  returns 0 on success. All fixtures are automatic storage; no allocation.
- Isolation-test style (`tests/native_virtual_datagram_isolation_test.cmake`):
  `file(READ ...)` the repository `CMakeLists.txt` and the seam sources, then
  `string(FIND ...)` / `string(REGEX MATCHALL ...)` assertions with
  `message(FATAL_ERROR ...)`. That specific file also
  - requires the standalone `add_library` line verbatim (`:2-6`),
  - forbids the target from linking anything (`:7-10`),
  - **requires exactly one `target_link_libraries` consumer, and it must be
    `native_virtual_datagram_test`** (`:11-20`),
  - forbids the tokens `winsock WinSock WSA socket Socket UDP udp SDL "main.c"
    MainMain "native_input" "native_replay" "game/" "Game_" protocol Protocol
    packet Packet admission Admission` inside
    `include/platform/native_virtual_datagram.h` and
    `platform/native_virtual_datagram.c` (`:21-35`).

  The consumer-count rule at `:11-20` is a hard blocker for Task 4 and is
  handled explicitly there.
- The lockstep module is `platform/native_*` host code, so it is a standalone
  library in `CMakeLists.txt`. It must **not** be added to
  `game/game_unity.h`; that chain is for game `.c` files only
  (`AGENTS.md:20-21`).

## 2. Decided design

### 2.1 Fixed-delay lockstep, no rollback, no prediction

Each peer buffers its locally sampled input for `D` frames. Simulation frame
`N` consumes only inputs that were sampled at frame `N - D` by every peer.
The simulation is therefore a pure function of a fully known input set at the
moment it runs: nothing is ever guessed, nothing is ever re-run.

Rationale, and why the alternatives are rejected:

- The existing determinism stack is built on the simulation being a pure
  function of a committed input set. `NativeReplaySchedulerV4` records one
  frame's inputs plus that frame's canonical digests and replays them exactly
  (`native_replay_scheduler_v4.h:37-50`). Fixed delay preserves that property
  verbatim, so a lockstep match is still recordable and replayable by the
  existing V4 scheduler with no format change.
- Rollback requires re-simulating already-presented frames, which means a
  savestate/restore of the full canonical domain set every frame and a
  simulation that tolerates being run twice for the same frame index. Neither
  exists, and adding either would perturb the RNG ownership work of
  integration step 3.
- Prediction requires a speculative input source, which would make the pad
  bytes fed to the simulation a function of arrival timing. That breaks
  bit-identity across cabinets and breaks replay.

**Rollback and prediction are out of scope for this milestone and must not be
added by it.** A later milestone may revisit them only with a new brief.

`D` is a session parameter, not a wire-negotiated one in this milestone: both
peers are configured with the same `D` and a mismatch is a protocol error
detected on the first received bundle. Bounds:
`NATIVE_LOCKSTEP_MIN_INPUT_DELAY 1`, `NATIVE_LOCKSTEP_MAX_INPUT_DELAY 6`.
Default 2 (33 ms of buffered input at 60 Hz, comfortably above wired-LAN
round trip on a two-cabinet switch).

### 2.2 Delay / reorder buffer

One fixed-capacity ring per remote peer, indexed by `frameIndex % capacity`:

- `NATIVE_LOCKSTEP_RING_CAPACITY 8`, a power of two, chosen so that
  `capacity >= NATIVE_LOCKSTEP_MAX_INPUT_DELAY + 1 = 7` with one frame of
  slack. The invariant `capacity >= D + 1` is asserted at open for the
  configured `D`.
- Storage per slot: the received bundle's exact encoded bytes
  (`uint8_t bytes[NATIVE_LOCKSTEP_RING_CAPACITY][NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES]`)
  plus the decoded `struct NativeLockstepBundleV1`. Keeping the raw bytes makes
  duplicate equality exact and portable; `memcmp` over a struct would compare
  padding.
- Occupancy is a `uint32_t occupancyMask` with bit `frameIndex % capacity`.
  Eight slots fit in eight bits; a `uint32_t` keeps it a single word and leaves
  headroom if the capacity is ever raised.
- `consumedFrame` is the next frame index the local simulation will consume.
  The acceptance window is `[consumedFrame, consumedFrame + capacity - 1]`.

Arrival policy, exhaustively:

| Case | Condition | Action |
| --- | --- | --- |
| Fresh in-window | in window, slot bit clear | accept, store bytes and decoded bundle, set bit |
| Out-of-order in-window | in window, `frameIndex > consumedFrame`, bit clear | accept; identical to the fresh case, because the ring is indexed by frame, not by arrival order |
| Duplicate | in window, bit set | `memcmp` the stored encoded bytes; byte-identical -> accept as a no-op and increment `duplicateAcceptCount`; any difference -> protocol error `NATIVE_LOCKSTEP_FAULT_CONFLICTING_INPUT` |
| Stale | `frameIndex < consumedFrame` | drop, increment `staleDropCount`; **not** an error, because a duplicating or delaying transport legitimately re-delivers a frame the simulation already consumed |
| Ahead of window | `frameIndex >= consumedFrame + capacity` | protocol error `NATIVE_LOCKSTEP_FAULT_WINDOW_OVERRUN`; the ring never grows and never evicts an unconsumed frame |

Consumption: `NativeLockstepSession_TakeRemoteInput(session, frameIndex, ...)`
requires `frameIndex == consumedFrame` and the bit set. If the bit is clear it
returns a **stall** result, which is not an error and not latched: the caller
must not advance the simulation and should pump the transport and retry.
On success the bit is cleared and `consumedFrame` becomes `frameIndex + 1`.

No dynamic allocation anywhere: the ring, the staging buffers and the reports
are all members of caller-owned structs, in the style of
`struct NativeVirtualDatagramPair` (`native_virtual_datagram.h:44-46`) and
`struct NativeReplaySchedulerV4` (`native_replay_scheduler_v4.h:19-28`).

### 2.3 Frame bundle wire shape

One bundle per peer per frame, fixed width, encoded strictly through
`NativeCodecWriter` primitives, hence fixed little-endian (section 1.5).

`NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES` = **128**.
`NATIVE_LOCKSTEP_BUNDLE_V1_MAGIC` = `UINT32_C(0x31424c4e)` ("NLB1" read low
byte first, the same convention as `NATIVE_MATCH_CONFIG_V1_MAGIC` 0x31434d4e
= "NMC1" and `NATIVE_CANONICAL_STATE_V4_MAGIC` 0x3456434e).
`NATIVE_LOCKSTEP_BUNDLE_V1_VERSION` = 1.
`NATIVE_LOCKSTEP_BUNDLE_PAD_CAPACITY` = 2.

| Off | Size | Field | Writer call | Meaning |
| --- | --- | --- | --- | --- |
| 0 | 4 | `magic` | `WriteU32` | `NATIVE_LOCKSTEP_BUNDLE_V1_MAGIC` |
| 4 | 4 | `bundleVersion` | `WriteU32` | `NATIVE_LOCKSTEP_BUNDLE_V1_VERSION` |
| 8 | 4 | `encodedSize` | `WriteU32` | literal 128; self-describing width, mirroring `native_match_config.c:151`/`:226` |
| 12 | 4 | `protocolVersion` | `WriteU32` | copied from `config.protocolVersion`; must equal the local config's |
| 16 | 8 | `matchIdentity[8]` | `WriteBytes` | first 8 bytes of `NativeMatchConfigV1_Digest` |
| 24 | 4 | `frameIndex` | `WriteU32` | the simulation frame these inputs are **for** |
| 28 | 4 | `inputDelay` | `WriteU32` | sender's `D`; must equal the receiver's |
| 32 | 1 | `senderSlot` | `WriteU8` | config slot index the sender owns (`FindRoleSlot`) |
| 33 | 1 | `padCount` | `WriteU8` | 0..`NATIVE_LOCKSTEP_BUNDLE_PAD_CAPACITY` |
| 34 | 1 | `verifiedPresent` | `WriteU8` | 0 or 1 |
| 35 | 1 | `reserved0` | `WriteU8` | zero, rejected if nonzero |
| 36 | 10 | pad entry 0 | `WriteU8` + `WriteU8`/`WriteBytes` | `slotIndex u8`, then `status u8`, `id u8`, `buttons[2]`, `analog[4]`, `connected u8` in `NativeCanonicalInputPadV1` order (`native_canonical_state.h:43-50`, encoder `native_canonical_state_v4.c:16`) |
| 46 | 10 | pad entry 1 | as above | unused entry is `slotIndex` `0xFF` and nine zero bytes |
| 56 | 4 | `verifiedFrameIndex` | `WriteU32` | the already-simulated frame this digest describes |
| 60 | 48 | `verifiedDomainDigests[6]` | 6 x `WriteU64` | `state.domainDigests[i]` in `NativeCanonicalDomainOrder` |
| 108 | 8 | `verifiedCombinedDigest` | `WriteU64` | `state.combinedDigest` |
| 116 | 4 | `reserved1[4]` | `WriteBytes` | zero, rejected if nonzero |
| 120 | 8 | `bundleDigest` | `WriteU64` | FNV-1a 64 over bytes 0..119 |

Encoding procedure (the digest field must not digest itself): initialize one
`NativeCodecWriter` over a local 128-byte staging buffer with a non-NULL
`struct NativeCodecDigest64 *`, write offsets 0..119, read the digest value,
set `writer.digest = NULL`, then `WriteU64` the value. Decode recomputes
FNV-1a 64 over the first 120 received bytes and compares.

Field justifications:

- **Match identity is a truncated 8-byte prefix of the 32-byte SHA-256 config
  digest.** The full 32-byte digest is compared once, at session open, from the
  whole `struct NativeMatchConfigV1` both peers hold; per frame the field only
  has to make a cross-session or cross-configuration mix-up impossible in
  practice. 64 bits over a match of a few thousand frames is far beyond
  sufficient for a misconfiguration check on a two-cabinet LAN, and it is not a
  security token, so the remaining 24 bytes would be 24 wasted bytes on every
  frame. This is a deliberate truncation, recorded here so a later reviewer does
  not mistake it for an oversight.
- **Per-slot input is a fixed 2-entry array, always fully encoded.** The
  two-cabinet profile gives each peer exactly one human slot
  (`native_match_config.c:49-50`), so `padCount` is 1 in practice. The capacity
  of 2 covers the one-cabinet profile and any future two-humans-on-one-cabinet
  arrangement without a version bump, and always encoding both entries keeps the
  bundle a constant 128 bytes, which makes the truncation test, the oversize
  test and the fixture storage width trivial.
- **All six domain digests travel, not just the combined digest.** The combined
  digest alone can only report "something differs". Carrying
  `domainDigests[0..5]` lets the divergence report fill a
  `canonicalDomainMask` of the same shape as
  `NativeReplaySchedulerV4MismatchReport.canonicalDomainMask`
  (`native_replay_scheduler_v4.c:24`), naming the domain that diverged
  (control, RNG, input, drivers, world, topology). 48 bytes per frame per peer
  is 2.9 KiB/s at 60 Hz, which is nothing on a wired LAN and is the single most
  valuable diagnostic in the whole milestone.

**Verification lags simulation, necessarily.** Timeline for a peer at
simulation frame `S`:

1. Sample local input `I(S)`, tagged for consumption at frame `S + D`.
2. Compose and send the bundle: `frameIndex = S + D`, pads = `I(S)`.
3. Simulate frame `S` using the inputs tagged `S` (sampled at `S - D`).
4. Compute `NativeCanonicalStateV4_ComputeDigests` for frame `S`.

The bundle in step 2 is composed *before* step 4, so the newest digest that
exists when it is sent is frame `S - 1`. Therefore
`verifiedFrameIndex = frameIndex - D - 1`, a fixed lag of `D + 1` frames behind
the input frame the bundle carries, and `D + 1` frames behind the frame being
simulated when it is consumed. It cannot be the same frame: a frame's digest
does not exist until that frame has been simulated, and the bundle must arrive
before that frame can be simulated at all, because it carries the input that
frame needs. A divergence at frame `F` is therefore detectable no earlier than
frame `F + D + 1` (50 ms at `D = 2`, 60 Hz). For the first `D + 1` frames of a
session `verifiedPresent` is 0 and the digest fields are zero.

Maximum encoded size versus the transport: 128 bytes. There is no maximum
payload constant in `native_virtual_datagram` (section 1.4) - `payloadCapacity`
is chosen by the fixture - so the fault test declares
`uint8_t storage[queueCapacity][NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES]` and
128 is by construction exactly the capacity. A real socket layer, when it is
separately gated, fits 128 bytes inside any Ethernet MTU with room to spare.

### 2.4 First-divergence report

Mirrors section 1.3 exactly: latch the first divergence, never overwrite it,
expose it through a `const`-or-`NULL` accessor.

```c
enum NativeLockstepDivergenceMask {
    NATIVE_LOCKSTEP_DIVERGENCE_COMBINED = 1u,
    NATIVE_LOCKSTEP_DIVERGENCE_CANONICAL_DOMAIN = 2u,
    NATIVE_LOCKSTEP_DIVERGENCE_FRAME_UNAVAILABLE = 4u
};

struct NativeLockstepDivergenceReport {
    uint32_t mask, canonicalDomainMask;
    uint32_t frameIndex;
    uint32_t senderSlot;
    uint64_t localCombinedDigest, remoteCombinedDigest;
    uint64_t localDomainDigests[NATIVE_CANONICAL_DOMAIN_COUNT];
    uint64_t remoteDomainDigests[NATIVE_CANONICAL_DOMAIN_COUNT];
};
```

- `mask` uses the same bit-flag idiom as
  `enum NativeReplaySchedulerV4MismatchMask`
  (`native_replay_scheduler_v4.h:10-12`). `canonicalDomainMask` is
  `UINT32_C(1) << i` per differing `domainDigests[i]`, identical to
  `platform/native_replay_scheduler_v4.c:24`.
- `frameIndex` is the `verifiedFrameIndex` from the peer's bundle: the frame
  that actually diverged, not the frame on which the disagreement was noticed.
  `senderSlot` names which peer reported it.
- `FRAME_UNAVAILABLE` covers the case where the peer reports a digest for a
  frame the local side cannot compare (never simulated, or already retired from
  the local digest history). It is a divergence, not a protocol error: the
  bundle is well formed but the two simulations are no longer comparable.
- The latch, in the shape of `Match()`
  (`platform/native_replay_scheduler_v4.c:16`): the entire body guarded by
  `if (session->mode != NATIVE_LOCKSTEP_DIVERGED)`, zero the report, fill it,
  set `mode = NATIVE_LOCKSTEP_DIVERGED`, return 0.
- The accessor
  `const struct NativeLockstepDivergenceReport *NativeLockstepSession_FirstDivergence(const struct NativeLockstepSession *session)`
  returns `&session->divergence` when `mode == NATIVE_LOCKSTEP_DIVERGED` and
  `NULL` otherwise.
- Protocol faults are a **separate, second** latch, so a malformed bundle
  arriving after a divergence cannot erase the divergence - the same asymmetry
  `Poison()` gives `Match()` (`platform/native_replay_scheduler_v4.c:11`).
  `NativeLockstepSession_FirstFault()` returns a
  `struct NativeLockstepFaultReport { uint32_t cause; uint32_t frameIndex;
  uint32_t senderSlot; uint32_t detail; }` or `NULL`. Causes:
  `BAD_MAGIC`, `BAD_VERSION`, `BAD_SIZE`, `BAD_DIGEST`, `BAD_RESERVED`,
  `MATCH_IDENTITY`, `PROTOCOL_VERSION`, `INPUT_DELAY`, `BAD_SLOT`,
  `BAD_PAD_COUNT`, `CONFLICTING_INPUT`, `WINDOW_OVERRUN`, `VERIFY_LAG`.
  Session modes: `IDLE`, `RUNNING`, `DIVERGED`, `FAULTED`. `DIVERGED` and
  `FAULTED` are both terminal for simulation; only `DIVERGED` outranks
  `FAULTED` for retention.

### 2.5 Transport-agnostic

`platform/native_lockstep_protocol.c` and the session module encode to and
decode from caller-owned byte buffers only. They must compile and be fully
testable with no socket or OS networking header, no SDL, no clock and no game
dependency - the same posture the virtual datagram leaf already documents
(`CMakeLists.txt:155-156`). `native_virtual_datagram` is the *test* transport
and the fault-injection harness; it is linked only by test fixtures, never by
the protocol library. A real wired-LAN socket layer, peer discovery and a lobby
are separately gated and **out of scope for this milestone**
(`docs/HANDOFF.md:72-79`).

## 3. Constraints

Restated as binding rules for every task below.

1. **The topology lease is untouched.** No acquire, activate, capture or
   publish wiring. No lease owner in checkpoints, replay or canonical state. No
   retire hook on `LOAD_Hub_ReadFile`. The lease stays retire-only exactly as
   `docs/HANDOFF.md:81-90`, `AGENTS.md:37-41` and the existing lease isolation
   tests (`CMakeLists.txt:865-866`, `:873-874`, `:884-885`, `:893-894`)
   describe it. This milestone does not authorize any change there.
2. **No changes to the canonical-state, replay or match-config wire formats.**
   `NativeMatchConfigV1` stays exactly 256 encoded bytes
   (`native_match_config.h:22`). `NativeCanonicalStateV4` and the V4 replay
   format are read-only inputs here: the protocol consumes
   `state.domainDigests` and `state.combinedDigest` and writes nothing back.
   The lockstep bundle is a new, sibling format; it never reinterprets an
   existing one.
3. **No new dynamic allocation.** No `malloc`, `calloc`, `realloc` or `free`
   in any new source. All buffers are fixed-size members of caller-owned
   structs.
4. **Portable C17, extensions disabled.** Every new target carries
   `C_STANDARD 17`, `C_STANDARD_REQUIRED ON`, `C_EXTENSIONS OFF`. Host and
   platform code stays behind `CTR_NATIVE` and the `platform/native_*` naming.
   New game `.c` files would go in `game/game_unity.h`; this milestone adds
   none.
5. **Every new seam gets a unit test; every structural rule gets an isolation
   test** (`AGENTS.md:31-32`).
6. **A task is not done** until `cmake --build build-msvc-x86 --config Debug`
   succeeds and the full `ctest --test-dir build-msvc-x86 -C Debug` suite
   passes. Never weaken a test to make it pass; report the failure.
7. **No push to any remote.** Commit on `arcade` only.

## 4. Task list

Each task is one implementer-sized, independently verifiable unit. Run the full
suite after each:

```sh
cmake --preset windows-msvc-x86
cmake --build build-msvc-x86 --config Debug
ctest --test-dir build-msvc-x86 -C Debug --output-on-failure
```

Baseline at commit `dffe3e8b7`: 72 tests, 100% passed. Each task must leave the
count at 72 plus whatever it adds.

### Task 1 - frame-bundle codec

Dependencies: none.

Creates:
- `include/platform/native_lockstep_protocol.h` - the constants of section
  2.3, `struct NativeLockstepBundlePadV1 { uint8_t slotIndex; struct
  NativeCanonicalInputPadV1 pad; }`, `struct NativeLockstepBundleV1`, the fault
  cause enum, `NativeLockstepBundleV1_EncodedSize(void)`,
  `NativeLockstepBundleV1_Validate`, `NativeLockstepBundleV1_Encode(struct
  NativeCodecWriter *, const struct NativeLockstepBundleV1 *)` and
  `NativeLockstepBundleV1_Decode(struct NativeCodecReader *, const uint8_t
  expectedMatchIdentity[8], uint32_t expectedProtocolVersion, uint32_t
  expectedInputDelay, struct NativeLockstepBundleV1 *, uint32_t *faultCauseOut)`.
- `platform/native_lockstep_protocol.c` - transactional encode/decode in the
  style of `NativeMatchConfigV1_Encode`/`_Decode`
  (`platform/native_match_config.c:138-234`): operate on a local copy of the
  writer/reader and commit only on full success.
- `tests/native_lockstep_protocol_test.c`.

Touches:
- `CMakeLists.txt` - `add_library(ctr_native_lockstep_protocol STATIC
  platform/native_lockstep_protocol.c)` beside the other leaves, linking
  `ctr_native_canonical_codec` only (it needs
  `NativeCanonicalInputPadV1` from `native_canonical_state.h` and the codec
  primitives; it must **not** link `ctr_native_virtual_datagram`), plus the
  `add_executable`/`add_test` pair inside `if(BUILD_TESTING)`.

Acceptance test `native_lockstep_protocol_unit` must cover:
- round trip: encode then decode yields a field-for-field equal bundle, and
  `NativeCodecWriter_Size` is exactly 128;
- byte-exactness: at least one hand-written expected byte array proving
  little-endian layout at the offsets of the section 2.3 table;
- truncation: decode of every length 0..127 fails and leaves the output
  untouched;
- bad magic, bad `bundleVersion`, bad `encodedSize`;
- bad match identity (any of the 8 bytes differing);
- `protocolVersion` mismatch, `inputDelay` mismatch;
- corrupted `bundleDigest` and a corrupted body byte with a stale digest;
- nonzero `reserved0` / `reserved1`;
- oversize rejection: a writer with capacity 127 fails and writes nothing;
- `padCount > NATIVE_LOCKSTEP_BUNDLE_PAD_CAPACITY`, `senderSlot >= 8`, an
  unused pad entry whose `slotIndex` is not `0xFF`;
- `verifiedPresent == 0` with nonzero digest fields is rejected;
- every failure path reports a distinct fault cause.

### Task 2 - delay / reorder buffer

Dependencies: Task 1 (it stores decoded bundles).

Creates:
- `include/platform/native_lockstep_input_window.h`,
  `platform/native_lockstep_input_window.c` - `struct
  NativeLockstepInputWindow` with the ring, `occupancyMask`, `consumedFrame`,
  `staleDropCount`, `duplicateAcceptCount`;
  `NativeLockstepInputWindow_Init/Offer/Take/Peek` and a stall result enum.
- `tests/native_lockstep_input_window_test.c`.

Touches:
- `CMakeLists.txt` - a second leaf target linking `ctr_native_lockstep_protocol`
  plus its test registration. (It may instead be a second source on the Task 1
  target; if so, the Task 5 isolation test must name both sources. Prefer the
  separate target: the window has no wire format and the protocol has no ring,
  so they are genuinely separate seams.)

Acceptance test `native_lockstep_input_window_unit` must cover every row of the
section 2.2 table:
- fresh in-window accept; out-of-order accept (offer `N+2` then `N+1` then
  `N`, take in order `N`, `N+1`, `N+2`);
- byte-identical duplicate accepted as a no-op, `duplicateAcceptCount`
  incremented, stored bytes unchanged;
- differing duplicate rejected with `CONFLICTING_INPUT`;
- stale arrival below `consumedFrame` dropped with `staleDropCount`
  incremented and no error;
- arrival at exactly `consumedFrame + capacity` rejected with
  `WINDOW_OVERRUN`, and the ring contents provably unchanged (`memcmp` the
  whole struct before and after, as
  `tests/native_virtual_datagram_test.c:8-18` does);
- `Take` of an empty slot returns the stall result, does not latch anything,
  and leaves `consumedFrame` unmoved;
- `Take` of a frame other than `consumedFrame` is rejected;
- the full window filled to capacity and drained, then refilled, proving the
  modular index wraps correctly;
- `Init` rejects `D` outside `[1, NATIVE_LOCKSTEP_MAX_INPUT_DELAY]` and any
  `D` with `D + 1 > NATIVE_LOCKSTEP_RING_CAPACITY`.

### Task 3 - lockstep session and first-divergence report

Dependencies: Tasks 1 and 2.

Creates:
- `include/platform/native_lockstep_session.h`,
  `platform/native_lockstep_session.c` - `struct NativeLockstepSession`
  holding the mode, the local input delay line, one
  `struct NativeLockstepInputWindow` per remote peer, a small ring of the last
  `D + 2` local `{frameIndex, domainDigests[6], combinedDigest}` records for
  comparison, the latched divergence and fault reports.
  API: `_Init`, `_Open(session, const struct NativeMatchConfigV1 *, uint32_t
  inputDelay, uint8_t localSlot)` (which computes the full 32-byte
  `NativeMatchConfigV1_Digest`, stores the 8-byte prefix, and validates
  `D` against the ring capacity), `_SubmitLocalInput`,
  `_RecordLocalDigests(session, const struct NativeCanonicalStateV4 *)`,
  `_ComposeBundle(session, uint32_t frameIndex, uint8_t *bytes, size_t
  capacity, size_t *sizeOut)`, `_AcceptBundle(session, const uint8_t *bytes,
  size_t size)`, `_TakeFrameInputs(session, uint32_t frameIndex, ...)`,
  `_FirstDivergence`, `_FirstFault`, `_Mode`.
- `tests/native_lockstep_session_test.c`.

Touches:
- `CMakeLists.txt` - session target linking
  `ctr_native_lockstep_protocol`, `ctr_native_lockstep_input_window` and
  `ctr_native_canonical_state_v4` (for `struct NativeCanonicalStateV4`), plus
  its test registration.

Acceptance test `native_lockstep_session_unit` must:
- drive two in-process sessions (CAB1 slot 0, CAB2 slot 1, two-cab profile)
  for enough frames to exercise the `D + 1` verification lag, handing each
  composed bundle straight to the other's `_AcceptBundle`, and assert both
  reach `mode == RUNNING` with `_FirstDivergence() == NULL` and
  `_FirstFault() == NULL`;
- assert `verifiedPresent == 0` for exactly the first `D + 1` frames and
  `verifiedFrameIndex == frameIndex - D - 1` thereafter;
- force a divergence by perturbing one domain digest on one side only, then
  assert the other side latches exactly one report with the correct
  `frameIndex` (the diverged frame, not the detecting frame), the correct
  `canonicalDomainMask` bit, both digests recorded, `COMBINED` set;
- assert the latch is once-only: a second, different divergence on a later
  frame does not change the report;
- assert a protocol fault arriving after a divergence leaves
  `_FirstDivergence()` intact and `mode == DIVERGED`;
- assert `FRAME_UNAVAILABLE` when a peer reports a `verifiedFrameIndex` the
  local side never simulated;
- assert a mismatched `inputDelay` or match identity at the very first bundle
  produces the right fault and no divergence.

### Task 4 - fault-injection integration test over `native_virtual_datagram`

Dependencies: Task 3.

Creates:
- `tests/native_lockstep_transport_fault_test.c` - two sessions plus one
  `struct NativeVirtualDatagramPair` with
  `payloadCapacity = NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES`, driving the
  four supported faults of section 1.4:
  - **loss** (`DROP`) followed by a retransmit of the same frame's bundle:
    both peers must still reach identical canonical digests;
  - **delay** (`DELIVER` with a late `firstDeliveryStep` still inside the
    window): the receiver stalls on `_TakeFrameInputs`, then proceeds;
  - **reorder** (two sends with inverted delivery steps): accepted, consumed in
    frame order;
  - **duplication** (`DUPLICATE` with two steps): the second copy is a
    byte-identical no-op and `duplicateAcceptCount` is 2 minus 1 per duplicated
    frame.
  Then the negative case: a bundle delayed or withheld long enough that the
  sender runs past `consumedFrame + capacity - 1` must produce a clean
  `WINDOW_OVERRUN` fault with no divergence latched and no memory unsafety.

Touches:
- `CMakeLists.txt` - `add_executable` linking
  `ctr_native_lockstep_session` and `ctr_native_virtual_datagram`, plus
  `add_test(NAME native_lockstep_transport_fault_unit ...)`.
- `tests/native_virtual_datagram_isolation_test.cmake:11-20` - **required
  edit.** That rule currently asserts `ctr_native_virtual_datagram` has exactly
  one `target_link_libraries` consumer and that it is
  `native_virtual_datagram_test`. Linking the harness into a second fixture
  makes the existing `native_virtual_datagram_isolation` test fail. Relax it to
  exactly two consumers, named explicitly:
  `native_virtual_datagram_test` and `native_lockstep_transport_fault_test`.
  Keep the count assertion (`NOT consumer_count EQUAL 2`) and keep the
  name check for each consumer, so the harness still cannot leak into a
  production target. Do **not** relax the `:7-10` rule that the harness links
  nothing, and do **not** touch the forbidden-token list at `:21-35` - that
  list scans only `native_virtual_datagram.{h,c}`, which this milestone does
  not modify, so the `protocol`/`packet` tokens in it are not a conflict.

Acceptance: `native_lockstep_transport_fault_unit` passes and
`native_virtual_datagram_isolation` still passes after the relaxation.

Note for the implementer: the harness cannot corrupt bytes (section 1.4), so
corruption coverage stays where Task 1 put it, at the codec seam. Do not add a
corruption action to `native_virtual_datagram`; that would change a frozen
leaf and is out of scope.

### Task 5 - isolation test

Dependencies: Tasks 1-4.

Creates:
- `tests/native_lockstep_isolation_test.cmake`, registered as
  `add_test(NAME native_lockstep_isolation COMMAND "${CMAKE_COMMAND}" -P
  "${CMAKE_SOURCE_DIR}/tests/native_lockstep_isolation_test.cmake")` next to
  the other `-P` tests. Written in the style of
  `tests/native_virtual_datagram_isolation_test.cmake`. It must assert, over
  `include/platform/native_lockstep_protocol.h`,
  `platform/native_lockstep_protocol.c`,
  `include/platform/native_lockstep_input_window.h`,
  `platform/native_lockstep_input_window.c`,
  `include/platform/native_lockstep_session.h` and
  `platform/native_lockstep_session.c`:
  1. **No socket or OS networking header or symbol**: forbid
     `winsock`, `WinSock`, `WSA`, `socket`, `Socket`, `AF_INET`, `sockaddr`,
     `htons`, `htonl`, `ntohs`, `ntohl`, `SDL_net`, `SDL`, `<sys/`,
     `netinet`, `getaddrinfo`, `select(`, `poll(`.
  2. **No topology-lease symbol**: forbid `TopologyLease`,
     `topology_lease`, `LeaseAuthority`, `LeaseRuntime`, `LeaseOwner`,
     `Acquire`, `Activate`, `Publish`, `Retire`, `LOAD_Hub_ReadFile`.
  3. **No dynamic allocation**: forbid `malloc`, `calloc`, `realloc`, `free(`,
     `alloca`.
  4. **No clock, game, or presentation dependency**: forbid `time(`, `clock(`,
     `QueryPerformance`, `game/`, `Game_`, `main.c`, `native_renderer`,
     `native_display_config`, `native_frame_capture`, `texture_filter`.
  5. `ctr_native_lockstep_protocol` and `ctr_native_lockstep_input_window` do
     not link `ctr_native_virtual_datagram` (regex the repository
     `CMakeLists.txt`).
  6. The declared constants are frozen:
     `NATIVE_LOCKSTEP_BUNDLE_V1_ENCODED_BYTES 128`,
     `NATIVE_LOCKSTEP_BUNDLE_V1_MAGIC UINT32_C(0x31424c4e)`,
     `NATIVE_LOCKSTEP_BUNDLE_V1_VERSION UINT32_C(1)`,
     `NATIVE_LOCKSTEP_RING_CAPACITY 8`,
     `NATIVE_LOCKSTEP_MAX_INPUT_DELAY 6`.
  7. No lockstep identifier appears in
     `platform/native_canonical_state*.c`, `platform/native_replay_*.c`,
     `platform/native_match_config.c` or `game/` - the protocol consumes those
     seams, never the reverse. Forbid `lockstep`, `Lockstep`,
     `LOCKSTEP` in those files.
  8. All three new targets carry `C_STANDARD 17`, `C_STANDARD_REQUIRED ON`,
     `C_EXTENSIONS OFF` in `CMakeLists.txt`.

Acceptance: `native_lockstep_isolation` passes, and the implementer must
demonstrate it *can* fail by temporarily introducing one forbidden token and
observing the `FATAL_ERROR`.

### Task 6 - docs

Dependencies: Tasks 1-5.

Touches:
- `docs/REPLAYS.md` - a new section after "Render-scale determinism sweep"
  (`:61-86`) stating that a lockstep match is still a V4 replay: the bundle
  carries the same `NativeCanonicalInputPadV1` bytes the INPUT domain records
  and the same `domainDigests`/`combinedDigest` the V4 scheduler compares, so a
  recorded lockstep match replays with no format change. Note the `D + 1`
  verification lag so a reader does not expect a lockstep divergence frame and a
  replay mismatch frame to coincide.
- `docs/HANDOFF.md` - update "Networking" (`:70-79`) to describe the protocol
  as it then exists, change the integration-order line for step 4
  (`:34`) from "not started", and rewrite "Next work" (`:146-153`) to point at
  integration step 5 (failure handling, results, rematch) or at the real
  socket-layer gate, whichever the operator chooses. Add the new files to
  "Key files" (`:105-125`). Present-state wording only: no history, no
  changelog, per the banner at `:3-6`.
- `docs/LOCKSTEP_MILESTONE.md` - mark task status, as
  `docs/TEXTURE_FILTER_MILESTONE.md:257-260` does.

Acceptance: docs-only; the full suite must still pass, because
`native_build_identity_cmake` and the isolation tests read repository files.

## 5. Risks and open questions

1. `D` is configured, not negotiated. A cabinet misconfiguration surfaces as a
   fault on the first bundle rather than as a graceful renegotiation. That is
   the right trade for a two-cabinet fleet with a common build, but a lobby
   milestone will want a handshake.
2. Stall handling is the caller's problem in this milestone. The session
   reports a stall; it does not decide how long to wait, whether to drop the
   peer, or what to show on screen. That is integration step 5.
3. The local digest history is `D + 2` frames deep. A peer whose verification
   falls further behind than that produces `FRAME_UNAVAILABLE` rather than a
   digest comparison. The depth is a constant and can be raised without a wire
   change.
4. The bundle has no sequence number distinct from `frameIndex`, so a
   transport that both duplicates and delays can only be diagnosed by frame,
   not by send attempt. The virtual harness already exposes
   `serial`/`deliveryOrdinal` in its metadata
   (`native_virtual_datagram.h:35-42`) for the test's own bookkeeping.
5. There is no retransmission policy: the protocol accepts whatever arrives and
   stalls otherwise. Deciding who retransmits, and when, belongs to the socket
   layer gate.
6. `native_virtual_datagram` cannot corrupt payload bytes, so the end-to-end
   corruption path (bad bytes surviving a real NIC) is only covered at the
   codec seam. Accepted: the `bundleDigest` field is the mitigation and Task 1
   tests it directly.
7. Bot inputs are not on the wire. Both peers run the original CTR bots from a
   shared seed and roster (`docs/HANDOFF.md:50-58`), so bot divergence shows up
   as a DRIVERS or RNG domain mismatch rather than as an input disagreement.
   That is the intended design and it is why carrying all six domain digests
   matters.

## 6. What this milestone must not do

- Not touch the topology lease in any way (section 3.1).
- Not add rollback, prediction, or any re-simulation of a past frame.
- Not add sockets, peer discovery, a lobby, or a wire commitment for
  `NativeMatchConfigV1.protocolVersion` beyond copying it into the bundle and
  comparing it.
- Not change `NATIVE_MATCH_CONFIG_V1_ENCODED_BYTES`, the V4 canonical schema,
  or any replay format.
- Not modify `platform/native_virtual_datagram.{c,h}`. Only its isolation
  test's consumer list changes, and only as Task 4 specifies.
- Not add anything to `game/game_unity.h`.

## 7. Orchestrator protocol

Use fresh-context subagents, one per task, with explicit files, the section 3
constraints and the exact verification commands. Run the full ctest suite after
every task and never claim a pass a subagent did not report. A reviewer is
required for Task 4 (it relaxes an existing structural rule) and Task 5 (an
isolation test that is too weak is worse than none). Commit per task on
`arcade`. Do not push.

## 8. Where the real headers contradicted the design sketch

Recorded so a later reader does not re-derive them:

1. **V4 domain digests are FNV-1a 64, not SHA-256.** `docs/HANDOFF.md:41-44`
   says the six domains "are SHA-256 digested and folded into a combined
   digest". The code digests each encoded domain with `NativeCodecDigest64`,
   i.e. FNV-1a 64 (`platform/native_canonical_state_v4.c:26`, algorithm named
   at `include/platform/native_canonical_codec.h:23`), and folds the six
   `uint64_t` results with the same function (`:27`). SHA-256 appears in V4
   only as the 32-byte `identity.build`, `identity.content` and `configDigest`
   fields (`native_canonical_state_v4.h:27-28`). The bundle therefore carries
   `uint64_t` digests, not 32-byte ones, and the whole per-frame verification
   path is 64-bit. `docs/HANDOFF.md` should be corrected in Task 6.
2. **There is no `NativeCanonicalStateV4` digest function.** A caller does not
   call a `..._Digest()`; it calls `NativeCanonicalStateV4_ComputeDigests` (or
   the scratch variant) and then reads the `domainDigests` and `combinedDigest`
   members of the state. Section 1.2 records the real sequence.
3. **`native_virtual_datagram` has no max payload constant.** The brief assumed
   one to check 128 bytes against. `payloadCapacity` is a caller-supplied
   parameter validated at `platform/native_virtual_datagram.c:87-88` and
   enforced per send at `:116`. The check becomes "the fixture declares the
   capacity as the bundle size", which is stronger.
4. **`native_virtual_datagram` supports loss, delay, reorder and duplication
   but not corruption.** Only three actions exist (`DROP`, `DELIVER`,
   `DUPLICATE`); delay and reorder are expressed through the delivery-step
   fields, not through separate actions. So the "clean protocol error when the
   transport does not deliver" case cannot be produced by `DROP` alone - a
   `DROP` produces a *stall*, which is deliberately not an error. The protocol
   error comes from the window-overrun boundary, and Task 4 is written that
   way.
5. **`NativeVirtualDatagramReceiveResult` has four values, not a
   truncation-specific set**: `EMPTY`, `OK`, `TOO_SMALL`, `INVALID`
   (`native_virtual_datagram.h:20-26`).
6. **The virtual datagram isolation test forbids a second consumer.**
   `tests/native_virtual_datagram_isolation_test.cmake:11-20` requires exactly
   one `target_link_libraries` mentioning the harness, and requires it to be
   `native_virtual_datagram_test`. Task 4's fault test cannot link the harness
   without editing that rule. The sketch did not anticipate this; Task 4 now
   specifies the exact, minimal relaxation and flags it for review.
7. **The two-cabinet profile gives each peer exactly one human slot**
   (`platform/native_match_config.c:49-50`), and canonical input has only four
   pads (`NATIVE_CANONICAL_INPUT_PAD_COUNT`,
   `include/platform/native_canonical_state.h:55`), not eight. So "the
   per-slot input payload" is not an eight-wide array: the bundle carries a
   fixed two-entry array of `{slotIndex, NativeCanonicalInputPadV1}`, which is
   why section 2.3 justifies the capacity explicitly.
8. **`NativeReplaySchedulerV4MismatchReport` carries no digest values.** Its
   six fields are `mask`, `canonicalDomainMask`, `expectedFrame`, `liveFrame`,
   `expectedVsyncCount`, `liveVsyncCount`
   (`include/platform/native_replay_scheduler_v4.h:17-18`) - the digests
   themselves are not retained. The lockstep report deliberately goes further
   and keeps both sides' digests, because unlike the replay scheduler the
   lockstep peer cannot re-read the other side's state afterwards. The
   latch-once mechanics and the `const`-or-`NULL` accessor are copied exactly.
