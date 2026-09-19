#ifndef PLATFORM_NATIVE_PRESENTATION_INVALIDATION_H
#define PLATFORM_NATIVE_PRESENTATION_INVALIDATION_H

/*
 * Local host-presentation source invalidation gate.
 *
 * A replacement texture is a snapshot of an immutable retail VRAM source
 * rectangle.  Once a source has actually been selected for host rendering,
 * later VRAM operations that touch that rectangle make the replacement
 * unsafe.  This tracker records only those observed exact registry entries
 * and permanently falls back to native rendering for them until Reset.
 *
 * It deliberately has no GPU, renderer, or filesystem dependency.  The GPU
 * packet path must call the Notify helpers for every relevant write, copy,
 * readback, and framebuffer-feedback operation before asking to use an
 * override.  Operations that cannot describe a valid bounded rectangle
 * invalidate every observed entry rather than risk a stale replacement.
 */

#include <platform/native_presentation_registry.h>

#define NATIVE_PRESENTATION_INVALIDATION_VRAM_WIDTH 1024u
#define NATIVE_PRESENTATION_INVALIDATION_VRAM_HEIGHT 512u
#define NATIVE_PRESENTATION_INVALIDATION_MAX_OBSERVED_ENTRIES \
	NATIVE_PRESENTATION_REGISTRY_MAX_ENTRIES

enum NativePresentationInvalidationCause
{
	NATIVE_PRESENTATION_INVALIDATION_CAUSE_NONE = 0,
	NATIVE_PRESENTATION_INVALIDATION_CAUSE_VRAM_WRITE,
	NATIVE_PRESENTATION_INVALIDATION_CAUSE_VRAM_COPY,
	NATIVE_PRESENTATION_INVALIDATION_CAUSE_VRAM_READBACK,
	NATIVE_PRESENTATION_INVALIDATION_CAUSE_FRAMEBUFFER_FEEDBACK,
	NATIVE_PRESENTATION_INVALIDATION_CAUSE_UNKNOWN,
};

enum NativePresentationInvalidationError
{
	NATIVE_PRESENTATION_INVALIDATION_ERROR_NONE = 0,
	NATIVE_PRESENTATION_INVALIDATION_ERROR_ARGUMENT,
	NATIVE_PRESENTATION_INVALIDATION_ERROR_REGISTRY_DISABLED,
	NATIVE_PRESENTATION_INVALIDATION_ERROR_ENTRY_OWNERSHIP,
	NATIVE_PRESENTATION_INVALIDATION_ERROR_SOURCE_RECT,
	NATIVE_PRESENTATION_INVALIDATION_ERROR_CAPACITY,
	NATIVE_PRESENTATION_INVALIDATION_ERROR_INVALIDATED,
};

/* Physical PS1 VRAM words, with a non-empty half-open rectangle. */
struct NativePresentationVramRect
{
	unsigned int x;
	unsigned int y;
	unsigned int width;
	unsigned int height;
};

struct NativePresentationInvalidationRecord
{
	struct NativePresentationSourceKey source;
	int invalidated;
	enum NativePresentationInvalidationCause cause;
};

struct NativePresentationInvalidationTracker
{
	unsigned int observedCount;
	enum NativePresentationInvalidationError lastError;
	struct NativePresentationInvalidationRecord
		records[NATIVE_PRESENTATION_INVALIDATION_MAX_OBSERVED_ENTRIES];
};

void NativePresentationInvalidationTracker_Init(
	struct NativePresentationInvalidationTracker *tracker);
void NativePresentationInvalidationTracker_Reset(
	struct NativePresentationInvalidationTracker *tracker);

/*
 * The exact authorization point for a future host replacement.  `entry` must
 * be the exact object owned by an explicitly enabled registry.  A first
 * observation records the immutable source without treating that normal load
 * as a mutation; an already-invalidated observation returns zero.
 */
int NativePresentationInvalidationTracker_ObserveEnabledEntry(
	struct NativePresentationInvalidationTracker *tracker,
	const struct NativePresentationRegistry *registry,
	const struct NativePresentationRegistryEntry *entry);

/*
 * Mark all already-observed sources overlapping `rect` unusable.  A valid
 * operation with no overlap is harmless.  Invalid/unknown rectangle details
 * invalidate every observed source and return zero.
 */
int NativePresentationInvalidationTracker_NotifyVramWrite(
	struct NativePresentationInvalidationTracker *tracker,
	const struct NativePresentationVramRect *rect);

/*
 * Conservatively invalidates sources that overlap either half of a copy:
 * source reads and destination writes can both participate in feedback or
 * self-copy semantics that a static host asset cannot reproduce.
 */
int NativePresentationInvalidationTracker_NotifyVramCopy(
	struct NativePresentationInvalidationTracker *tracker,
	const struct NativePresentationVramRect *source,
	const struct NativePresentationVramRect *destination);

int NativePresentationInvalidationTracker_NotifyVramReadback(
	struct NativePresentationInvalidationTracker *tracker,
	const struct NativePresentationVramRect *rect);
int NativePresentationInvalidationTracker_NotifyFramebufferFeedback(
	struct NativePresentationInvalidationTracker *tracker,
	const struct NativePresentationVramRect *rect);

/* Use when the feedback rectangle is unavailable; it always fails closed. */
void NativePresentationInvalidationTracker_NotifyUnknownFramebufferFeedback(
	struct NativePresentationInvalidationTracker *tracker);

enum NativePresentationInvalidationError
	NativePresentationInvalidationTracker_GetLastError(
		const struct NativePresentationInvalidationTracker *tracker);
const char *NativePresentationInvalidationTracker_ErrorName(
	enum NativePresentationInvalidationError error);

#endif
