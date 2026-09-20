# Retire-only topology lease authority

`MainCanonicalTopologyLeaseAuthority` is a fail-closed source foundation. A
private, process-local owner now retires any existing generation before
`StateZero` clears `gGT` and records that distinct game-tracker-zero boundary;
it constructs only on first boot after that clear, then records and retires
before the physical native arena wipe, normal `LOAD_LevelFile`
entry, the direct cold-boot ten-stage full-load entry, active
`LOAD_Hub_SwapNow` repack, and a validated checkpoint restore before overlay
reset. The inactive `LOAD_Hub_ReadFile` preload deliberately has no hook.

This is retire-only, not live topology authority: no existing game path
acquires or observes a lease, captures topology, activates after post-init,
publishes canonical state, serializes/replays the owner, or sends network data.
The owner is process-local and excluded from checkpoint regions.

At cold boot, the arena reset retires the newly constructed generation. The
subsequent direct ten-stage load entry in `StateZero` is recorded as a source
boundary but is coalesced rather than creating a second retirement epoch.
Later mutations stay fail-closed because this slice intentionally has no activation.

The audited lifecycle reasons are game-tracker zero, full load, hub swap, checkpoint restore, and mempack arena reset; inactive hub preload is explicitly excluded.

Before a live consumer is allowed, an approved integration must call `Init`
once after cold game-state construction, then call matching `ActivatePostInit`
only at the post-load initialization point that establishes every
`NavHeader.last` value.  Retire advances the epoch once; matching activation
does not.  Acquire/observe is permitted only while active.
Every activation/capture call site needs a new live-cabinet gate,
deterministic capture evidence, and review.

The lease range is allocated bytes `[start, firstFreeByte)`, never pack
capacity.  A failed acquire or observation publishes no partial output.
