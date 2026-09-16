#include "MAIN/MainArcadeSetupV4.h"

#include <string.h>

static int BytesEqual(const uint8_t *a, const uint8_t *b, size_t count)
{
	for (size_t i = 0; i < count; ++i) if (a[i] != b[i]) return 0;
	return 1;
}

static int RngEqual(const struct NativeDeterministicRngBankV1 *a, const struct NativeDeterministicRngBankV1 *b)
{
	if (a->bankVersion != b->bankVersion || a->derivationVersion != b->derivationVersion || a->masterSeed != b->masterSeed) return 0;
	for (uint32_t i = 0; i < NATIVE_DETERMINISTIC_RNG_STREAM_COUNT; ++i) {
		if (a->streams[i].tag != b->streams[i].tag || a->streams[i].stableSlot != b->streams[i].stableSlot ||
			a->streams[i].streamIndex != b->streams[i].streamIndex || a->streams[i].drawCount != b->streams[i].drawCount) return 0;
		for (uint32_t n = 0; n < 4; ++n) if (a->streams[i].state[n] != b->streams[i].state[n]) return 0;
	}
	return 1;
}

static int PlanEqual(const struct MainArcadeRosterPlan *a, const struct MainArcadeRosterPlan *b)
{
	if (a->profile != b->profile || a->presenceMask != b->presenceMask || a->humanCount != b->humanCount ||
		a->botCount != b->botCount || a->driverCount != b->driverCount || a->locked != b->locked ||
		!BytesEqual(a->matchConfigDigest, b->matchConfigDigest, NATIVE_SHA256_DIGEST_BYTES)) return 0;
	for (uint8_t i = 0; i < MAIN_ARCADE_ROSTER_SLOT_COUNT; ++i) {
		const struct MainArcadeRosterPlanSlot *x = &a->slots[i], *y = &b->slots[i];
		if (x->present != y->present || x->stableSlot != y->stableSlot || x->role != y->role ||
			x->initialLifecycle != y->initialLifecycle || x->characterID != y->characterID ||
			x->difficulty != y->difficulty || x->kind != y->kind || x->reserved != y->reserved) return 0;
	}
	return 1;
}

static int ValidatedEqual(const struct MainArcadeRosterValidated *a, const struct MainArcadeRosterValidated *b)
{
	const struct NativeCanonicalDriversPreludeV1 *x = &a->roster.prelude, *y = &b->roster.prelude;
	if (a->presenceMask != b->presenceMask || a->humanCount != b->humanCount || a->botCount != b->botCount ||
		a->driverCount != b->driverCount || a->reserved != b->reserved ||
		!BytesEqual(a->matchConfigDigest, b->matchConfigDigest, NATIVE_SHA256_DIGEST_BYTES) ||
		x->slotCount != y->slotCount || x->presenceMask != y->presenceMask || x->playerCount != y->playerCount ||
		x->activeBotCount != y->activeBotCount || x->numLaps != y->numLaps || x->winnerCount != y->winnerCount ||
		x->raceOrderCount != y->raceOrderCount || x->detailedVersion != y->detailedVersion ||
		!BytesEqual(x->raceOrder, y->raceOrder, 8) || !BytesEqual(x->winnerSlots, y->winnerSlots, 4) ||
		!BytesEqual(x->humanPlayerPositions, y->humanPlayerPositions, 8) || !BytesEqual(x->navListCount, y->navListCount, 3) ||
		!BytesEqual(&x->navListOrder[0][0], &y->navListOrder[0][0], 24) ||
		!BytesEqual(a->roster.behaviorID, b->roster.behaviorID, 8) || !BytesEqual(a->roster.threadBehaviorID, b->roster.threadBehaviorID, 8) ||
		!BytesEqual(a->roster.kind, b->roster.kind, 8)) return 0;
	for (uint8_t i = 0; i < MAIN_ARCADE_ROSTER_SLOT_COUNT; ++i) {
		const struct MainArcadeRosterNativeSlotFacts *p = &a->slots[i], *q = &b->slots[i];
		if (p->present != q->present || p->driverID != q->driverID || p->role != q->role ||
			p->initialLifecycle != q->initialLifecycle || p->characterID != q->characterID || p->difficulty != q->difficulty ||
			!BytesEqual(p->reserved, q->reserved, sizeof(p->reserved))) return 0;
	}
	return 1;
}

static int SetupEqual(const struct MainArcadeBotSetupPlan *a, const struct MainArcadeBotSetupPlan *b)
{
	if (a->profile != b->profile || a->botMask != b->botMask || a->botCount != b->botCount || a->locked != b->locked ||
		!BytesEqual(a->reserved, b->reserved, sizeof(a->reserved)) ||
		!BytesEqual(a->matchConfigDigest, b->matchConfigDigest, NATIVE_SHA256_DIGEST_BYTES) ||
		!BytesEqual(a->rngBeforeDigest, b->rngBeforeDigest, NATIVE_SHA256_DIGEST_BYTES) ||
		!BytesEqual(a->rngAfterDigest, b->rngAfterDigest, NATIVE_SHA256_DIGEST_BYTES)) return 0;
	for (uint8_t i = 0; i < MAIN_ARCADE_BOT_SETUP_SLOT_COUNT; ++i) {
		const struct MainArcadeBotSetupAssignment *x = &a->assignments[i], *y = &b->assignments[i];
		if (x->enabled != y->enabled || x->stableSlot != y->stableSlot || x->nativeDriverSlot != y->nativeDriverSlot ||
			x->characterID != y->characterID || x->difficulty != y->difficulty || x->spawnOrder != y->spawnOrder ||
			x->navPathIndex != y->navPathIndex || x->accelerationOrder != y->accelerationOrder || x->setupSequence != y->setupSequence ||
			x->setupRandom != y->setupRandom || !BytesEqual(x->reserved, y->reserved, sizeof(x->reserved))) return 0;
	}
	return 1;
}

static int ContextRevalidate(const struct MainArcadeSetupV4Context *context)
{
	struct MainCanonicalStateV4Context projector;
	struct MainArcadeRosterPlan plan;
	struct MainArcadeRosterValidated validated;
	struct MainArcadeBotSetupPlan setup;
	struct NativeDeterministicRngBankV1 rngAfter;
	uint64_t rosterDigest;
	if (context == NULL || !MainCanonicalStateV4Context_Init(&projector, &context->projector.config) ||
		!BytesEqual(projector.configDigest, context->projector.configDigest, NATIVE_SHA256_DIGEST_BYTES) ||
		!MainArcadeRoster_BuildPlan(&context->projector.config, &plan) ||
		!MainArcadeRoster_ValidateNativeFacts(&plan, &context->projector.config, &context->rosterFacts, &validated) ||
		MainArcadeBotSetup_Plan(&context->projector.config, &plan, &validated, &context->setupFacts,
			&context->rngBefore, &setup, &rngAfter) != MAIN_ARCADE_BOT_SETUP_OK ||
		!NativeCanonicalDriversPreludeV1_Digest(&validated.roster.prelude, &rosterDigest)) return 0;
	return PlanEqual(&plan, &context->rosterPlan) && ValidatedEqual(&validated, &context->validatedRoster) &&
		SetupEqual(&setup, &context->setupPlan) && RngEqual(&rngAfter, &context->rngAfter) &&
		context->driversPresenceMask == validated.presenceMask && context->driversRosterMetaDigest == rosterDigest;
}

int MainArcadeSetupV4Context_Init(struct MainArcadeSetupV4Context *context,
	const struct NativeMatchConfigV1 *config, const struct MainArcadeRosterNativeFacts *rosterFacts,
	const struct MainArcadeBotSetupSourceFacts *setupFacts, const struct NativeDeterministicRngBankV1 *rngBefore)
{
	struct MainArcadeSetupV4Context candidate;
	if (context == NULL || config == NULL || rosterFacts == NULL || setupFacts == NULL || rngBefore == NULL ||
		!MainCanonicalStateV4Context_Init(&candidate.projector, config)) return 0;
	candidate.rosterFacts = *rosterFacts;
	candidate.setupFacts = *setupFacts;
	candidate.rngBefore = *rngBefore;
	if (!MainArcadeRoster_BuildPlan(config, &candidate.rosterPlan) ||
		!MainArcadeRoster_ValidateNativeFacts(&candidate.rosterPlan, config, rosterFacts, &candidate.validatedRoster) ||
		MainArcadeBotSetup_Plan(config, &candidate.rosterPlan, &candidate.validatedRoster, setupFacts, rngBefore,
			&candidate.setupPlan, &candidate.rngAfter) != MAIN_ARCADE_BOT_SETUP_OK ||
		!NativeCanonicalDriversPreludeV1_Digest(&candidate.validatedRoster.roster.prelude, &candidate.driversRosterMetaDigest)) return 0;
	candidate.driversPresenceMask = candidate.validatedRoster.presenceMask;
	*context = candidate;
	return 1;
}

int MainArcadeSetupV4_Project(struct NativeCanonicalStateV4 *state, const struct MainArcadeSetupV4Context *context,
	const struct NativeIdentityV1 *identity, uint32_t replayFrameNumber, const struct NativeCanonicalControlV1 *control,
	const struct NativeCanonicalRngV1 *retailRng, const struct NativeCanonicalInputV1 *input,
	const struct NativeCanonicalDriversV1 *drivers, const struct NativeCanonicalWorldCountersV1 *worldCounters,
	const struct NativeCanonicalWorldMineRegistryV1 *mineRegistry, const struct NativeCanonicalTopologyV1 *topology)
{
	if (state == NULL || drivers == NULL || !ContextRevalidate(context) || !NativeCanonicalDriversV1_Validate(drivers) ||
		drivers->presenceMask != context->driversPresenceMask || drivers->rosterMetaDigest != context->driversRosterMetaDigest) return 0;
	return MainCanonicalState_ProjectV4(state, &context->projector, identity, replayFrameNumber, control, retailRng,
		&context->rngAfter, input, drivers, worldCounters, mineRegistry, topology);
}
