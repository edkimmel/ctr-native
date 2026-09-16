# Dormant topology lease authority

`MainCanonicalTopologyLeaseAuthority` is source-only groundwork.  No existing
game path initializes it, retires it, acquires a lease, or observes topology.
It is not current live lifecycle authority.

Before a live consumer is allowed, an approved integration must call `Init`
once after cold game-state construction, retire on full load, hub swap,
checkpoint restore, and mempack arena reset, and acquire/observe only after the
post-load initialization point that establishes every `NavHeader.last` value.
The inactive hub preload is intentionally not a retire event.  Every such call
site needs a new live-cabinet gate, deterministic capture evidence, and review.

The lease range is allocated bytes `[start, firstFreeByte)`, never pack
capacity.  A failed acquire or observation publishes no partial output.
