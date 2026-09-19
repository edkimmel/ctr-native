/* Unit coverage for the bounded, renderer-free presentation invalidation gate. */
#include <platform/native_presentation_invalidation.h>

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #expression); return 1; } } while (0)

static struct NativePresentationRegistryEntry MakeEntry(unsigned int x, unsigned int y,
	unsigned int width, unsigned int height, unsigned int assetClass)
{
	struct NativePresentationRegistryEntry entry;

	memset(&entry, 0, sizeof(entry));
	entry.source.textureMode = NATIVE_PRESENTATION_TEXTURE_MODE_4_BIT;
	entry.source.tpage = 0x13u;
	entry.source.clut = 0x345u;
	entry.source.x = (u16)x;
	entry.source.y = (u16)y;
	entry.source.width = (u16)width;
	entry.source.height = (u16)height;
	entry.source.assetClass = (u8)assetClass;
	return entry;
}

static void InitEnabledRegistry(struct NativePresentationRegistry *registry)
{
	NativePresentationRegistry_Init(registry);
	registry->entries[0] = MakeEntry(194u, 266u, 3u, 14u,
		NATIVE_PRESENTATION_ASSET_CLASS_FONT);
	registry->entries[1] = MakeEntry(500u, 100u, 8u, 12u,
		NATIVE_PRESENTATION_ASSET_CLASS_CHARACTER_SPRITE);
	registry->entryCount = 2u;
	NativePresentationRegistry_SetEnabled(registry, 1);
}

static struct NativePresentationVramRect Rect(unsigned int x, unsigned int y,
	unsigned int width, unsigned int height)
{
	struct NativePresentationVramRect rect;
	rect.x = x;
	rect.y = y;
	rect.width = width;
	rect.height = height;
	return rect;
}

int main(void)
{
	struct NativePresentationRegistry registry;
	struct NativePresentationInvalidationTracker tracker;
	struct NativePresentationRegistryEntry copiedEntry;
	struct NativePresentationVramRect rect;
	struct NativePresentationVramRect otherRect;

	InitEnabledRegistry(&registry);
	NativePresentationInvalidationTracker_Init(&tracker);

	/* The first legitimate host selection records a source but is not a write. */
	CHECK(NativePresentationInvalidationTracker_ObserveEnabledEntry(&tracker, &registry,
		&registry.entries[0]));
	CHECK(tracker.observedCount == 1u);
	CHECK(tracker.records[0].invalidated == 0);
	CHECK(NativePresentationInvalidationTracker_GetLastError(&tracker) ==
		NATIVE_PRESENTATION_INVALIDATION_ERROR_NONE);

	/* A write touching that exact physical source permanently falls back. */
	rect = Rect(196u, 270u, 1u, 1u);
	CHECK(NativePresentationInvalidationTracker_NotifyVramWrite(&tracker, &rect));
	CHECK(tracker.records[0].invalidated == 1);
	CHECK(tracker.records[0].cause == NATIVE_PRESENTATION_INVALIDATION_CAUSE_VRAM_WRITE);
	CHECK(!NativePresentationInvalidationTracker_ObserveEnabledEntry(&tracker, &registry,
		&registry.entries[0]));
	CHECK(NativePresentationInvalidationTracker_GetLastError(&tracker) ==
		NATIVE_PRESENTATION_INVALIDATION_ERROR_INVALIDATED);
	CHECK(NativePresentationInvalidationTracker_ObserveEnabledEntry(&tracker, &registry,
		&registry.entries[1]));

	/* Writes before first observed use are intentionally not self-invalidating. */
	NativePresentationInvalidationTracker_Reset(&tracker);
	rect = Rect(194u, 266u, 3u, 14u);
	CHECK(NativePresentationInvalidationTracker_NotifyVramWrite(&tracker, &rect));
	CHECK(tracker.observedCount == 0u);
	CHECK(NativePresentationInvalidationTracker_ObserveEnabledEntry(&tracker, &registry,
		&registry.entries[0]));

	/* Copy is conservatively hazardous on either source or destination side. */
	NativePresentationInvalidationTracker_Reset(&tracker);
	CHECK(NativePresentationInvalidationTracker_ObserveEnabledEntry(&tracker, &registry,
		&registry.entries[0]));
	rect = Rect(194u, 266u, 3u, 14u);
	otherRect = Rect(800u, 400u, 3u, 14u);
	CHECK(NativePresentationInvalidationTracker_NotifyVramCopy(&tracker,
		&rect, &otherRect));
	CHECK(!NativePresentationInvalidationTracker_ObserveEnabledEntry(&tracker, &registry,
		&registry.entries[0]));
	NativePresentationInvalidationTracker_Reset(&tracker);
	CHECK(NativePresentationInvalidationTracker_ObserveEnabledEntry(&tracker, &registry,
		&registry.entries[0]));
	rect = Rect(800u, 400u, 3u, 14u);
	otherRect = Rect(194u, 266u, 3u, 14u);
	CHECK(NativePresentationInvalidationTracker_NotifyVramCopy(&tracker,
		&rect, &otherRect));
	CHECK(!NativePresentationInvalidationTracker_ObserveEnabledEntry(&tracker, &registry,
		&registry.entries[0]));

	/* Readback and feedback use the same overlap gate without GL state. */
	NativePresentationInvalidationTracker_Reset(&tracker);
	CHECK(NativePresentationInvalidationTracker_ObserveEnabledEntry(&tracker, &registry,
		&registry.entries[1]));
	rect = Rect(504u, 105u, 1u, 1u);
	CHECK(NativePresentationInvalidationTracker_NotifyVramReadback(&tracker,
		&rect));
	CHECK(!NativePresentationInvalidationTracker_ObserveEnabledEntry(&tracker, &registry,
		&registry.entries[1]));
	NativePresentationInvalidationTracker_Reset(&tracker);
	CHECK(NativePresentationInvalidationTracker_ObserveEnabledEntry(&tracker, &registry,
		&registry.entries[1]));
	rect = Rect(500u, 100u, 8u, 12u);
	CHECK(NativePresentationInvalidationTracker_NotifyFramebufferFeedback(&tracker,
		&rect));
	CHECK(!NativePresentationInvalidationTracker_ObserveEnabledEntry(&tracker, &registry,
		&registry.entries[1]));

	/* Unknown or malformed operation bounds invalidate every observed source. */
	NativePresentationInvalidationTracker_Reset(&tracker);
	CHECK(NativePresentationInvalidationTracker_ObserveEnabledEntry(&tracker, &registry,
		&registry.entries[0]));
	CHECK(NativePresentationInvalidationTracker_ObserveEnabledEntry(&tracker, &registry,
		&registry.entries[1]));
	NativePresentationInvalidationTracker_NotifyUnknownFramebufferFeedback(&tracker);
	CHECK(!NativePresentationInvalidationTracker_ObserveEnabledEntry(&tracker, &registry,
		&registry.entries[0]));
	CHECK(!NativePresentationInvalidationTracker_ObserveEnabledEntry(&tracker, &registry,
		&registry.entries[1]));
	NativePresentationInvalidationTracker_Reset(&tracker);
	CHECK(NativePresentationInvalidationTracker_ObserveEnabledEntry(&tracker, &registry,
		&registry.entries[0]));
	rect = Rect(1024u, 0u, 1u, 1u);
	CHECK(!NativePresentationInvalidationTracker_NotifyVramWrite(&tracker, &rect));
	CHECK(!NativePresentationInvalidationTracker_ObserveEnabledEntry(&tracker, &registry,
		&registry.entries[0]));

	/* Disabled registries, copied entries, and malformed source bounds are not authority. */
	NativePresentationInvalidationTracker_Reset(&tracker);
	NativePresentationRegistry_SetEnabled(&registry, 0);
	CHECK(!NativePresentationInvalidationTracker_ObserveEnabledEntry(&tracker, &registry,
		&registry.entries[0]));
	CHECK(NativePresentationInvalidationTracker_GetLastError(&tracker) ==
		NATIVE_PRESENTATION_INVALIDATION_ERROR_REGISTRY_DISABLED);
	NativePresentationRegistry_SetEnabled(&registry, 1);
	copiedEntry = registry.entries[0];
	CHECK(!NativePresentationInvalidationTracker_ObserveEnabledEntry(&tracker, &registry, &copiedEntry));
	CHECK(NativePresentationInvalidationTracker_GetLastError(&tracker) ==
		NATIVE_PRESENTATION_INVALIDATION_ERROR_ENTRY_OWNERSHIP);
	registry.entries[0].source.width = 0u;
	CHECK(!NativePresentationInvalidationTracker_ObserveEnabledEntry(&tracker, &registry,
		&registry.entries[0]));
	CHECK(NativePresentationInvalidationTracker_GetLastError(&tracker) ==
		NATIVE_PRESENTATION_INVALIDATION_ERROR_SOURCE_RECT);

	CHECK(strcmp(NativePresentationInvalidationTracker_ErrorName(
		NATIVE_PRESENTATION_INVALIDATION_ERROR_INVALIDATED), "invalidated") == 0);
	CHECK(strcmp(NativePresentationInvalidationTracker_ErrorName(
		(enum NativePresentationInvalidationError)99), "invalid-invalidation-error") == 0);
	puts("native_presentation_invalidation_test: PASS");
	return 0;
}
