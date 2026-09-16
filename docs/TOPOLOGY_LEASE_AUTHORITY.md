# Dormant topology lease authority

`MainCanonicalTopologyLeaseAuthority` is source-only groundwork.  No existing
game path initializes it, retires it, acquires a lease, or observes topology.
It is not current live lifecycle authority.

Before a live consumer is allowed, an approved integration must call `Init`
once after cold game-state construction, retire on full load, hub swap,
checkpoint restore, and mempack arena reset, then call matching `ActivatePostInit`
only at the post-load initialization point that establishes every
`NavHeader.last` value.  Retire advances the epoch once; matching activation
does not.  Acquire/observe is permitted only while active.
The inactive hub preload is intentionally not a retire event.  Every such call
site needs a new live-cabinet gate, deterministic capture evidence, and review.

The lease range is allocated bytes `[start, firstFreeByte)`, never pack
capacity.  A failed acquire or observation publishes no partial output.
