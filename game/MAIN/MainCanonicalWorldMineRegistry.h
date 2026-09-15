#ifndef GAME_MAIN_CANONICAL_WORLD_MINE_REGISTRY_H
#define GAME_MAIN_CANONICAL_WORLD_MINE_REGISTRY_H

#include "platform/native_canonical_world_mine_registry.h"

struct GameTracker;
struct OverlayDATA_231;

/* Extracts only the resident racing/battle mine-pool links into the dormant
 * portable registry.  Non-RB overlays are an exact unavailable snapshot. */
int MainCanonicalWorldMineRegistry_ExtractV1(const struct GameTracker *gGT,
	const struct OverlayDATA_231 *source,
	struct NativeCanonicalWorldMineRegistryV1 *out);

#endif
