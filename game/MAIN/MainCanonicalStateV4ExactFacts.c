#include "MAIN/MainCanonicalStateV4ExactFacts.h"

int MainCanonicalState_ProjectV4FromExactFacts(
	struct NativeCanonicalStateV4 *state,
	const struct MainCanonicalStateV4Context *context,
	const struct NativeIdentityV1 *identity, uint32_t replayFrameNumber,
	const struct NativeCanonicalControlV1 *control,
	const struct NativeCanonicalRngV1 *retailRng,
	const struct NativeDeterministicRngBankV1 *deterministicRng,
	const struct NativeCanonicalInputV1 *input,
	const struct NativeCanonicalDriversDetailedV1 *driverFacts,
	const struct NativeCanonicalWorldCountersV1Facts *worldCounterFacts,
	const struct NativeCanonicalWorldMineRegistryV1Facts *mineFacts,
	const struct MainCanonicalTopologyFacts *topologyFacts,
	uint8_t *driverScratch, size_t driverScratchSize)
{
	struct NativeCanonicalDriversV1 drivers;
	struct NativeCanonicalWorldCountersV1 counters;
	struct NativeCanonicalWorldMineRegistryV1 mines;
	struct NativeCanonicalTopologyV1 topology;
	struct NativeCanonicalStateV4 candidate;

	if (state == NULL || driverFacts == NULL || worldCounterFacts == NULL ||
		mineFacts == NULL || topologyFacts == NULL || driverScratch == NULL ||
		driverScratchSize < NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES ||
		!NativeCanonicalDriversDetailedV1_BuildSummaryWithScratch(driverFacts,
			driverScratch, driverScratchSize, &drivers) ||
		!NativeCanonicalWorldCountersV1_FromFacts(&counters, worldCounterFacts) ||
		!NativeCanonicalWorldMineRegistryV1_FromFacts(&mines, mineFacts) ||
		!MainCanonicalTopologyFacts_ToV1(topologyFacts, &topology) ||
		!MainCanonicalState_ProjectV4(&candidate, context, identity, replayFrameNumber,
			control, retailRng, deterministicRng, input, &drivers, &counters,
			&mines, &topology)) return 0;
	*state = candidate;
	return 1;
}
