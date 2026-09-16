#ifndef MAIN_ARCADE_BOT_TICK_EVIDENCE_H
#define MAIN_ARCADE_BOT_TICK_EVIDENCE_H

#include "MAIN/MainArcadeBotSetup.h"
#include "platform/native_canonical_drivers_detailed.h"

#define MAIN_ARCADE_BOT_TICK_EVIDENCE_V1_MAGIC UINT32_C(0x31544245)
#define MAIN_ARCADE_BOT_TICK_EVIDENCE_V1_VERSION UINT32_C(1)
#define MAIN_ARCADE_BOT_TICK_EVIDENCE_V1_ENCODED_BYTES 1300u

/* A dormant proof record. It observes a proposed bot tick; it never computes one. */
struct MainArcadeBotTickEvidenceV1 {
	uint32_t frameNumber; uint8_t stableSlot; uint8_t reserved[3];
	uint8_t configDigest[32], botRulesDigest[32], setupPlanDigest[32];
	uint8_t beforeSlotFacts[NATIVE_CANONICAL_DRIVERS_SLOT_STREAM_BYTES];
	uint8_t afterSlotFacts[NATIVE_CANONICAL_DRIVERS_SLOT_STREAM_BYTES];
	uint8_t beforeSlotDigest[32], afterSlotDigest[32], rngBeforeDigest[32], rngAfterDigest[32];
	uint64_t botDrawCountBefore, botDrawCountAfter;
	uint8_t threadBehaviorIDBefore, threadBehaviorIDAfter; uint8_t tailReserved[2];
};

int MainArcadeBotTickEvidenceV1_Build(struct MainArcadeBotTickEvidenceV1 *out,
	uint32_t frameNumber, uint8_t stableSlot, const struct NativeMatchConfigV1 *config,
	const struct MainArcadeBotSetupPlan *setupPlan,
	const struct NativeCanonicalDriverSlotV1 *beforeSlot, const struct NativeCanonicalDriverSlotV1 *afterSlot,
	const struct NativeDeterministicRngBankV1 *rngBefore, const struct NativeDeterministicRngBankV1 *rngAfter);
int MainArcadeBotTickEvidenceV1_Validate(const struct MainArcadeBotTickEvidenceV1 *value);
int MainArcadeBotTickEvidenceV1_Encode(struct NativeCodecWriter *writer,const struct MainArcadeBotTickEvidenceV1 *value);
int MainArcadeBotTickEvidenceV1_Decode(struct NativeCodecReader *reader,struct MainArcadeBotTickEvidenceV1 *out);
int MainArcadeBotTickEvidenceV1_Digest(const struct MainArcadeBotTickEvidenceV1 *value,uint8_t digest[32]);
#endif
