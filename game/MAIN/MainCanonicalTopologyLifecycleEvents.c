#include "MAIN/MainCanonicalTopologyLifecycleEvents.h"

#include <string.h>

static int ValidConstructedReason(uint32_t reason)
{
	return reason == MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_COLD_BOOT ||
		reason == MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_TEST;
}

static int ValidMutationReason(uint32_t reason)
{
	return reason == MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_FULL_LOAD ||
		reason == MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_HUB_SWAP ||
		reason == MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_CHECKPOINT_RESTORE ||
		reason == MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_ARENA_RESET ||
		reason == MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_GAME_TRACKER_ZERO;
}

static int ValidEvent(const struct MainCanonicalTopologyLifecycleEventV1 *event)
{
	if (!event) return 0;
	if (event->kind == MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENT_CONSTRUCTED)
		return ValidConstructedReason(event->reason);
	if (event->kind == MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENT_MUTATION ||
		event->kind == MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENT_POST_INIT)
		return ValidMutationReason(event->reason);
	return 0;
}

static int IsZeroEvent(const struct MainCanonicalTopologyLifecycleEventV1 *event)
{
	return event->kind == 0 && event->reason == 0 && event->opaqueSourceStep == 0;
}

void MainCanonicalTopologyLifecycleEventsV1_Init(struct MainCanonicalTopologyLifecycleEventsV1 *recorder)
{
	if (!recorder) return;
	memset(recorder, 0, sizeof(*recorder));
	recorder->version = MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENTS_V1_VERSION;
}

int MainCanonicalTopologyLifecycleEventsV1_Validate(const struct MainCanonicalTopologyLifecycleEventsV1 *recorder)
{
	uint32_t index;
	if (!recorder || recorder->version != MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENTS_V1_VERSION ||
		recorder->count > MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENTS_V1_CAPACITY)
		return 0;
	for (index = 0; index < recorder->count; index++)
		if (!ValidEvent(&recorder->events[index])) return 0;
	for (; index < MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENTS_V1_CAPACITY; index++)
		if (!IsZeroEvent(&recorder->events[index])) return 0;
	return 1;
}

int MainCanonicalTopologyLifecycleEventsV1_Append(
	struct MainCanonicalTopologyLifecycleEventsV1 *recorder,
	const struct MainCanonicalTopologyLifecycleEventV1 *events, uint32_t count)
{
	struct MainCanonicalTopologyLifecycleEventsV1 candidate;
	uint32_t index;

	if (!recorder || !events || count == 0 || !MainCanonicalTopologyLifecycleEventsV1_Validate(recorder) ||
		count > MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENTS_V1_CAPACITY - recorder->count)
		return 0;
	for (index = 0; index < count; index++)
		if (!ValidEvent(&events[index])) return 0;
	candidate = *recorder;
	for (index = 0; index < count; index++) candidate.events[candidate.count + index] = events[index];
	candidate.count += count;
	*recorder = candidate;
	return 1;
}
