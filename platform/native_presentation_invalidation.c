#include <platform/native_presentation_invalidation.h>

#include <string.h>

static int NativePresentationInvalidationTracker_KeysEqual(
	const struct NativePresentationSourceKey *left,
	const struct NativePresentationSourceKey *right)
{
	return (left->textureMode == right->textureMode) &&
		(left->tpage == right->tpage) && (left->clut == right->clut) &&
		(left->x == right->x) && (left->y == right->y) &&
		(left->width == right->width) && (left->height == right->height) &&
		(left->assetClass == right->assetClass);
}

static int NativePresentationInvalidationTracker_IsValidRect(
	const struct NativePresentationVramRect *rect)
{
	return (rect != NULL) && (rect->width != 0u) && (rect->height != 0u) &&
		(rect->x < NATIVE_PRESENTATION_INVALIDATION_VRAM_WIDTH) &&
		(rect->y < NATIVE_PRESENTATION_INVALIDATION_VRAM_HEIGHT) &&
		(rect->width <= NATIVE_PRESENTATION_INVALIDATION_VRAM_WIDTH - rect->x) &&
		(rect->height <= NATIVE_PRESENTATION_INVALIDATION_VRAM_HEIGHT - rect->y);
}

static int NativePresentationInvalidationTracker_SourceRect(
	const struct NativePresentationSourceKey *source,
	struct NativePresentationVramRect *rect)
{
	if ((source == NULL) || (rect == NULL))
		return 0;
	if ((source->textureMode != NATIVE_PRESENTATION_TEXTURE_MODE_4_BIT) &&
		(source->textureMode != NATIVE_PRESENTATION_TEXTURE_MODE_8_BIT) &&
		(source->textureMode != NATIVE_PRESENTATION_TEXTURE_MODE_16_BIT))
	{
		return 0;
	}
	rect->x = source->x;
	rect->y = source->y;
	rect->width = source->width;
	rect->height = source->height;
	return NativePresentationInvalidationTracker_IsValidRect(rect);
}

static int NativePresentationInvalidationTracker_RectsOverlap(
	const struct NativePresentationVramRect *left,
	const struct NativePresentationVramRect *right)
{
	return (left->x < right->x + right->width) &&
		(right->x < left->x + left->width) &&
		(left->y < right->y + right->height) &&
		(right->y < left->y + left->height);
}

static void NativePresentationInvalidationTracker_SetError(
	struct NativePresentationInvalidationTracker *tracker,
	enum NativePresentationInvalidationError error)
{
	if (tracker != NULL)
		tracker->lastError = error;
}

static void NativePresentationInvalidationTracker_InvalidateAll(
	struct NativePresentationInvalidationTracker *tracker,
	enum NativePresentationInvalidationCause cause)
{
	unsigned int index;

	if (tracker == NULL)
		return;
	if (tracker->observedCount > NATIVE_PRESENTATION_INVALIDATION_MAX_OBSERVED_ENTRIES)
		tracker->observedCount = NATIVE_PRESENTATION_INVALIDATION_MAX_OBSERVED_ENTRIES;
	for (index = 0u; index < tracker->observedCount; ++index)
	{
		tracker->records[index].invalidated = 1;
		tracker->records[index].cause = cause;
	}
}

static int NativePresentationInvalidationTracker_InvalidateRect(
	struct NativePresentationInvalidationTracker *tracker,
	const struct NativePresentationVramRect *rect,
	enum NativePresentationInvalidationCause cause)
{
	unsigned int index;

	if (tracker == NULL)
		return 0;
	if ((tracker->observedCount > NATIVE_PRESENTATION_INVALIDATION_MAX_OBSERVED_ENTRIES) ||
		!NativePresentationInvalidationTracker_IsValidRect(rect))
	{
		NativePresentationInvalidationTracker_InvalidateAll(tracker,
			NATIVE_PRESENTATION_INVALIDATION_CAUSE_UNKNOWN);
		NativePresentationInvalidationTracker_SetError(tracker,
			NATIVE_PRESENTATION_INVALIDATION_ERROR_ARGUMENT);
		return 0;
	}
	for (index = 0u; index < tracker->observedCount; ++index)
	{
		struct NativePresentationVramRect sourceRect;

		if (!NativePresentationInvalidationTracker_SourceRect(&tracker->records[index].source,
			&sourceRect))
		{
			/* A corrupt record must never preserve a viable override. */
			NativePresentationInvalidationTracker_InvalidateAll(tracker,
				NATIVE_PRESENTATION_INVALIDATION_CAUSE_UNKNOWN);
			NativePresentationInvalidationTracker_SetError(tracker,
				NATIVE_PRESENTATION_INVALIDATION_ERROR_SOURCE_RECT);
			return 0;
		}
		if (NativePresentationInvalidationTracker_RectsOverlap(&sourceRect, rect))
		{
			tracker->records[index].invalidated = 1;
			tracker->records[index].cause = cause;
		}
	}
	NativePresentationInvalidationTracker_SetError(tracker,
		NATIVE_PRESENTATION_INVALIDATION_ERROR_NONE);
	return 1;
}

void NativePresentationInvalidationTracker_Init(
	struct NativePresentationInvalidationTracker *tracker)
{
	if (tracker != NULL)
		memset(tracker, 0, sizeof(*tracker));
}

void NativePresentationInvalidationTracker_Reset(
	struct NativePresentationInvalidationTracker *tracker)
{
	NativePresentationInvalidationTracker_Init(tracker);
}

int NativePresentationInvalidationTracker_ObserveEnabledEntry(
	struct NativePresentationInvalidationTracker *tracker,
	const struct NativePresentationRegistry *registry,
	const struct NativePresentationRegistryEntry *entry)
{
	struct NativePresentationVramRect sourceRect;
	unsigned int index;

	if (tracker == NULL)
		return 0;
	if ((registry == NULL) || (entry == NULL))
	{
		NativePresentationInvalidationTracker_SetError(tracker,
			NATIVE_PRESENTATION_INVALIDATION_ERROR_ARGUMENT);
		return 0;
	}
	if (!NativePresentationRegistry_IsEnabled(registry))
	{
		NativePresentationInvalidationTracker_SetError(tracker,
			NATIVE_PRESENTATION_INVALIDATION_ERROR_REGISTRY_DISABLED);
		return 0;
	}
	/* An arbitrary byte-for-byte entry copy is not selection authority. */
	if (NativePresentationRegistry_Lookup(registry, &entry->source) != entry)
	{
		NativePresentationInvalidationTracker_SetError(tracker,
			NATIVE_PRESENTATION_INVALIDATION_ERROR_ENTRY_OWNERSHIP);
		return 0;
	}
	if (!NativePresentationInvalidationTracker_SourceRect(&entry->source, &sourceRect))
	{
		NativePresentationInvalidationTracker_SetError(tracker,
			NATIVE_PRESENTATION_INVALIDATION_ERROR_SOURCE_RECT);
		return 0;
	}
	if (tracker->observedCount > NATIVE_PRESENTATION_INVALIDATION_MAX_OBSERVED_ENTRIES)
	{
		NativePresentationInvalidationTracker_InvalidateAll(tracker,
			NATIVE_PRESENTATION_INVALIDATION_CAUSE_UNKNOWN);
		NativePresentationInvalidationTracker_SetError(tracker,
			NATIVE_PRESENTATION_INVALIDATION_ERROR_CAPACITY);
		return 0;
	}
	for (index = 0u; index < tracker->observedCount; ++index)
	{
		if (NativePresentationInvalidationTracker_KeysEqual(&tracker->records[index].source,
			&entry->source))
		{
			if (tracker->records[index].invalidated != 0)
			{
				NativePresentationInvalidationTracker_SetError(tracker,
					NATIVE_PRESENTATION_INVALIDATION_ERROR_INVALIDATED);
				return 0;
			}
			NativePresentationInvalidationTracker_SetError(tracker,
				NATIVE_PRESENTATION_INVALIDATION_ERROR_NONE);
			return 1;
		}
	}
	if (tracker->observedCount == NATIVE_PRESENTATION_INVALIDATION_MAX_OBSERVED_ENTRIES)
	{
		NativePresentationInvalidationTracker_SetError(tracker,
			NATIVE_PRESENTATION_INVALIDATION_ERROR_CAPACITY);
		return 0;
	}
	tracker->records[tracker->observedCount].source = entry->source;
	tracker->records[tracker->observedCount].invalidated = 0;
	tracker->records[tracker->observedCount].cause = NATIVE_PRESENTATION_INVALIDATION_CAUSE_NONE;
	++tracker->observedCount;
	NativePresentationInvalidationTracker_SetError(tracker,
		NATIVE_PRESENTATION_INVALIDATION_ERROR_NONE);
	return 1;
}

int NativePresentationInvalidationTracker_NotifyVramWrite(
	struct NativePresentationInvalidationTracker *tracker,
	const struct NativePresentationVramRect *rect)
{
	return NativePresentationInvalidationTracker_InvalidateRect(tracker, rect,
		NATIVE_PRESENTATION_INVALIDATION_CAUSE_VRAM_WRITE);
}

int NativePresentationInvalidationTracker_NotifyVramCopy(
	struct NativePresentationInvalidationTracker *tracker,
	const struct NativePresentationVramRect *source,
	const struct NativePresentationVramRect *destination)
{
	if ((tracker == NULL) || !NativePresentationInvalidationTracker_IsValidRect(source) ||
		!NativePresentationInvalidationTracker_IsValidRect(destination))
	{
		NativePresentationInvalidationTracker_InvalidateAll(tracker,
			NATIVE_PRESENTATION_INVALIDATION_CAUSE_UNKNOWN);
		NativePresentationInvalidationTracker_SetError(tracker,
			NATIVE_PRESENTATION_INVALIDATION_ERROR_ARGUMENT);
		return 0;
	}
	if (!NativePresentationInvalidationTracker_InvalidateRect(tracker, source,
		NATIVE_PRESENTATION_INVALIDATION_CAUSE_VRAM_COPY))
	{
		return 0;
	}
	return NativePresentationInvalidationTracker_InvalidateRect(tracker, destination,
		NATIVE_PRESENTATION_INVALIDATION_CAUSE_VRAM_COPY);
}

int NativePresentationInvalidationTracker_NotifyVramReadback(
	struct NativePresentationInvalidationTracker *tracker,
	const struct NativePresentationVramRect *rect)
{
	return NativePresentationInvalidationTracker_InvalidateRect(tracker, rect,
		NATIVE_PRESENTATION_INVALIDATION_CAUSE_VRAM_READBACK);
}

int NativePresentationInvalidationTracker_NotifyFramebufferFeedback(
	struct NativePresentationInvalidationTracker *tracker,
	const struct NativePresentationVramRect *rect)
{
	return NativePresentationInvalidationTracker_InvalidateRect(tracker, rect,
		NATIVE_PRESENTATION_INVALIDATION_CAUSE_FRAMEBUFFER_FEEDBACK);
}

void NativePresentationInvalidationTracker_NotifyUnknownFramebufferFeedback(
	struct NativePresentationInvalidationTracker *tracker)
{
	NativePresentationInvalidationTracker_InvalidateAll(tracker,
		NATIVE_PRESENTATION_INVALIDATION_CAUSE_FRAMEBUFFER_FEEDBACK);
	NativePresentationInvalidationTracker_SetError(tracker,
		NATIVE_PRESENTATION_INVALIDATION_ERROR_NONE);
}

enum NativePresentationInvalidationError
NativePresentationInvalidationTracker_GetLastError(
	const struct NativePresentationInvalidationTracker *tracker)
{
	return tracker != NULL ? tracker->lastError : NATIVE_PRESENTATION_INVALIDATION_ERROR_ARGUMENT;
}

const char *NativePresentationInvalidationTracker_ErrorName(
	enum NativePresentationInvalidationError error)
{
	switch (error)
	{
	case NATIVE_PRESENTATION_INVALIDATION_ERROR_NONE: return "none";
	case NATIVE_PRESENTATION_INVALIDATION_ERROR_ARGUMENT: return "argument";
	case NATIVE_PRESENTATION_INVALIDATION_ERROR_REGISTRY_DISABLED: return "registry-disabled";
	case NATIVE_PRESENTATION_INVALIDATION_ERROR_ENTRY_OWNERSHIP: return "entry-ownership";
	case NATIVE_PRESENTATION_INVALIDATION_ERROR_SOURCE_RECT: return "source-rect";
	case NATIVE_PRESENTATION_INVALIDATION_ERROR_CAPACITY: return "capacity";
	case NATIVE_PRESENTATION_INVALIDATION_ERROR_INVALIDATED: return "invalidated";
	default: return "invalid-invalidation-error";
	}
}
