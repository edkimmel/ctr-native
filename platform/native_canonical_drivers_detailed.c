#include "platform/native_canonical_drivers_detailed.h"

#include <string.h>

static int Zeros(const uint8_t *bytes, size_t count)
{
	for (size_t i = 0; i < count; i++) if (bytes[i] != 0) return 0;
	return 1;
}

static int ActiveTagValid(uint32_t tag) { return tag <= NATIVE_CANONICAL_DRIVER_ACTIVE_WARP; }
static int KindValid(uint8_t kind) { return kind == NATIVE_CANONICAL_DRIVER_KIND_HUMAN || kind == NATIVE_CANONICAL_DRIVER_KIND_BOT; }

static int WritePrelude(struct NativeCodecWriter *w, const struct NativeCanonicalDriversPreludeV1 *v)
{
	return NativeCodecWriter_WriteU32(w, v->slotCount) && NativeCodecWriter_WriteU32(w, v->presenceMask) &&
		NativeCodecWriter_WriteBytes(w, v->raceOrder, 8) && NativeCodecWriter_WriteU8(w, v->playerCount) &&
		NativeCodecWriter_WriteU8(w, v->activeBotCount) && NativeCodecWriter_WriteS8(w, v->numLaps) &&
		NativeCodecWriter_WriteU8(w, v->winnerCount) && NativeCodecWriter_WriteBytes(w, v->winnerSlots, 4) &&
		NativeCodecWriter_WriteBytes(w, v->humanPlayerPositions, 8) && NativeCodecWriter_WriteBytes(w, v->navListCount, 3) &&
		NativeCodecWriter_WriteBytes(w, v->navListOrder, 24) && NativeCodecWriter_WriteU8(w, v->raceOrderCount) &&
		NativeCodecWriter_WriteU32(w, v->detailedVersion);
}

static int WriteMeta(struct NativeCodecWriter *w, const struct NativeCanonicalDriverMetaV1 *v)
{
	return NativeCodecWriter_WriteU8(w,v->present)&&NativeCodecWriter_WriteU8(w,v->slotIndex)&&NativeCodecWriter_WriteU8(w,v->driverID)&&
		NativeCodecWriter_WriteU8(w,v->characterID)&&NativeCodecWriter_WriteU8(w,v->driverKind)&&NativeCodecWriter_WriteU8(w,v->behaviorID)&&
		NativeCodecWriter_WriteU8(w,v->threadBehaviorID)&&NativeCodecWriter_WriteU8(w,v->kartState)&&NativeCodecWriter_WriteU32(w,v->actionsFlagSet)&&
		NativeCodecWriter_WriteU32(w,v->actionsFlagSetPrevFrame)&&NativeCodecWriter_WriteU8(w,v->heldItemID)&&NativeCodecWriter_WriteU8(w,v->numHeldItems)&&
		NativeCodecWriter_WriteS8(w,v->numWumpas)&&NativeCodecWriter_WriteS8(w,v->numCrystals)&&NativeCodecWriter_WriteS8(w,v->numTimeCrates)&&
		NativeCodecWriter_WriteS8(w,v->accelConst)&&NativeCodecWriter_WriteS8(w,v->turnConst)&&NativeCodecWriter_WriteS8(w,v->turboConst)&&
		NativeCodecWriter_WriteU8(w,v->lapIndex)&&NativeCodecWriter_WriteS8(w,v->simpTurnState)&&NativeCodecWriter_WriteU8(w,v->currentTerrain)&&
		NativeCodecWriter_WriteU8(w,v->forcedJumpType)&&NativeCodecWriter_WriteS8(w,v->normalVecID)&&NativeCodecWriter_WriteU8(w,v->boolFirstFrameSinceRevEngine)&&
		NativeCodecWriter_WriteU8(w,v->clockSend)&&NativeCodecWriter_WriteS8(w,v->revEngineState)&&NativeCodecWriter_WriteU16(w,v->externalPresenceFlags)&&
		NativeCodecWriter_WriteU16(w,v->driverThreadSimFlags)&&NativeCodecWriter_WriteS16(w,v->collisionFlags)&&NativeCodecWriter_WriteS16(w,v->rainCloudEffect);
}

static int WriteRace(struct NativeCodecWriter *w, const struct NativeCanonicalDriverRaceV1 *v)
{
	const int16_t timers[] = {v->clockReceive,v->hazardTimer,v->superEngineTimer,v->itemRollTimer,v->noItemTimer,v->jumpMeter,v->jumpMeterTimer,v->numTurbos};
	for (size_t i=0;i<8;i++) if (!NativeCodecWriter_WriteS16(w,timers[i])) return 0;
	return NativeCodecWriter_WriteS32(w,v->invincibleTimer)&&NativeCodecWriter_WriteS32(w,v->invisibleTimer)&&NativeCodecWriter_WriteS32(w,v->lapTime)&&
		NativeCodecWriter_WriteS32(w,v->timeElapsedInRace)&&NativeCodecWriter_WriteS16(w,v->driverRank)&&NativeCodecWriter_WriteU8(w,v->checkpointBranchChoiceIndex)&&
		NativeCodecWriter_WriteU8(w,v->checkpointCurrentIndex)&&NativeCodecWriter_WriteU32(w,v->distanceToFinishCurr)&&
		NativeCodecWriter_WriteU32(w,v->distanceToFinishCheckpoint)&&NativeCodecWriter_WriteU32(w,v->distanceDrivenBackwards)&&
		NativeCodecWriter_WriteS32(w,v->battleNumLives)&&NativeCodecWriter_WriteS32(w,v->battleTeamID)&&NativeCodecWriter_WriteS32(w,v->pickupLetterCount);
}

static int WriteS16s(struct NativeCodecWriter *w, const int16_t *values, size_t count)
{
	for (size_t i=0;i<count;i++) if (!NativeCodecWriter_WriteS16(w,values[i])) return 0;
	return 1;
}
static int WriteS32s(struct NativeCodecWriter *w, const int32_t *values, size_t count)
{
	for (size_t i=0;i<count;i++) if (!NativeCodecWriter_WriteS32(w,values[i])) return 0;
	return 1;
}
static int WritePhysics(struct NativeCodecWriter *w, const struct NativeCanonicalDriverPhysicsV1 *v)
{
	return NativeCodecWriter_WriteU32(w,v->currQuadIndex)&&NativeCodecWriter_WriteU32(w,v->underDriverQuadIndex)&&NativeCodecWriter_WriteU32(w,v->lastValidQuadIndex)&&
		NativeCodecWriter_WriteU8(w,v->terrainMeta1Index)&&NativeCodecWriter_WriteU8(w,v->terrainMeta2Index)&&NativeCodecWriter_WriteU16(w,v->reserved0)&&
		NativeCodecWriter_WriteU32(w,v->stepFlagSet)&&NativeCodecWriter_WriteS32(w,v->quadBlockHeight)&&WriteS32s(w,v->velocity,3)&&
		WriteS32s(w,v->originToCenter,3)&&WriteS32s(w,v->posCurr,3)&&WriteS32s(w,v->posPrev,3)&&WriteS16s(w,v->normalVecUP,3)&&
		WriteS16s(w,v->spsHitPos,3)&&WriteS16s(w,v->spsNormalVec,3)&&WriteS16s(w,v->axisAngle1,3)&&WriteS16s(w,v->axisAngle2,3)&&
		WriteS16s(w,v->axisAngle3,3)&&WriteS16s(w,v->axisAngle4,3)&&WriteS16s(w,v->rotCurr,4)&&WriteS16s(w,v->rotPrev,4)&&
		WriteS16s(w,v->posWallColl,3)&&WriteS16s(w,v->forwardAccelVector,3)&&WriteS16s(w,v->accel,3);
}
static int WriteSlot(struct NativeCodecWriter *w, const struct NativeCanonicalDriverSlotV1 *v)
{
	return WriteMeta(w,&v->meta)&&WriteRace(w,&v->race)&&WritePhysics(w,&v->physics)&&WriteS16s(w,v->dynamics.field,NATIVE_CANONICAL_DRIVER_DYN_COUNT)&&
		NativeCodecWriter_WriteS32(w,v->dynamics.xSpeed)&&NativeCodecWriter_WriteS32(w,v->dynamics.ySpeed)&&NativeCodecWriter_WriteS32(w,v->dynamics.zSpeed)&&
		NativeCodecWriter_WriteU32(w,v->active.unionTag)&&NativeCodecWriter_WriteBytes(w,v->active.branchBytes,20)&&
		NativeCodecWriter_WriteBytes(w,v->bot.bytes,NATIVE_CANONICAL_DRIVERS_BOT_BYTES)&&NativeCodecWriter_WriteBytes(w,v->reservedTail,NATIVE_CANONICAL_DRIVERS_TAIL_BYTES);
}

static int SlotIsAllZero(const struct NativeCanonicalDriverSlotV1 *slot)
{
	uint8_t bytes[NATIVE_CANONICAL_DRIVERS_SLOT_STREAM_BYTES];
	struct NativeCodecWriter writer;
	NativeCodecWriter_Init(&writer,bytes,sizeof(bytes),NULL);
	return WriteSlot(&writer,slot) && NativeCodecWriter_Size(&writer)==sizeof(bytes) && Zeros(bytes,sizeof(bytes));
}

static int ValidateList(const uint8_t *list, uint8_t count, uint32_t presenceMask, uint8_t maximum, int humanOnly,
	const struct NativeCanonicalDriverSlotV1 *slots)
{
	uint32_t seen=0;
	if (count>maximum) return 0;
	for(uint8_t i=0;i<maximum;i++)
	{
		uint8_t slot=list[i];
		if(i>=count) { if(slot!=NATIVE_CANONICAL_DRIVERS_ABSENT_SLOT) return 0; continue; }
		if(slot>=NATIVE_CANONICAL_DRIVERS_SLOT_COUNT || (presenceMask&(UINT32_C(1)<<slot))==0 || (seen&(UINT32_C(1)<<slot))!=0) return 0;
		if(humanOnly && slots[slot].meta.driverKind!=NATIVE_CANONICAL_DRIVER_KIND_HUMAN) return 0;
		seen|=UINT32_C(1)<<slot;
	}
	return 1;
}
static int ValidateRanks(const uint8_t *ranks, uint8_t count)
{
	if(count>8)return 0;
	for(uint8_t i=0;i<8;i++) { if(i<count) { if(ranks[i]>7)return 0; } else if(ranks[i]!=NATIVE_CANONICAL_DRIVERS_ABSENT_SLOT)return 0; }
	return 1;
}

void NativeCanonicalDriversDetailedV1_Init(struct NativeCanonicalDriversDetailedV1 *value)
{
	if(value==NULL)return;
	memset(value,0,sizeof(*value));
	value->prelude.slotCount=NATIVE_CANONICAL_DRIVERS_SLOT_COUNT;
	value->prelude.detailedVersion=NATIVE_CANONICAL_DRIVERS_DETAILED_VERSION;
	memset(value->prelude.raceOrder,NATIVE_CANONICAL_DRIVERS_ABSENT_SLOT,sizeof(value->prelude.raceOrder));
	memset(value->prelude.winnerSlots,NATIVE_CANONICAL_DRIVERS_ABSENT_SLOT,sizeof(value->prelude.winnerSlots));
	memset(value->prelude.humanPlayerPositions,NATIVE_CANONICAL_DRIVERS_ABSENT_SLOT,sizeof(value->prelude.humanPlayerPositions));
	memset(value->prelude.navListOrder,NATIVE_CANONICAL_DRIVERS_ABSENT_SLOT,sizeof(value->prelude.navListOrder));
}

int NativeCanonicalDriversDetailedV1_Validate(const struct NativeCanonicalDriversDetailedV1 *value)
{
	const struct NativeCanonicalDriversPreludeV1 *p;
	uint32_t present=0,human=0,bot=0;
	if(value==NULL)return 0;p=&value->prelude;
	if(p->slotCount!=NATIVE_CANONICAL_DRIVERS_SLOT_COUNT || (p->presenceMask&~UINT32_C(0xff))!=0 || p->numLaps<0 ||
		p->detailedVersion!=NATIVE_CANONICAL_DRIVERS_DETAILED_VERSION || !ValidateList(p->raceOrder,p->raceOrderCount,p->presenceMask,8,0,value->slots) ||
		!ValidateList(p->winnerSlots,p->winnerCount,p->presenceMask,4,0,value->slots) || !ValidateRanks(p->humanPlayerPositions,p->playerCount)) return 0;
	for(uint32_t list=0;list<3;list++) if(!ValidateList(p->navListOrder[list],p->navListCount[list],p->presenceMask,8,0,value->slots))return 0;
	for(uint32_t i=0;i<8;i++)
	{
		const struct NativeCanonicalDriverSlotV1 *s=&value->slots[i];
		uint32_t resolvedActiveTag;
		if((p->presenceMask&(UINT32_C(1)<<i))==0) { if(!SlotIsAllZero(s))return 0; continue; }
		if(s->meta.present!=1 || s->meta.slotIndex!=i || !KindValid(s->meta.driverKind) || s->meta.boolFirstFrameSinceRevEngine>1 || s->physics.reserved0!=0 || !Zeros(s->reservedTail,sizeof(s->reservedTail)) ||
			!ActiveTagValid(s->active.unionTag) || (s->active.unionTag==NATIVE_CANONICAL_DRIVER_ACTIVE_NONE&&!Zeros(s->active.branchBytes,sizeof(s->active.branchBytes))))return 0;
		if((s->meta.externalPresenceFlags&~NATIVE_CANONICAL_DRIVER_EXTERNAL_KNOWN_MASK)!=0 ||
			(s->meta.driverThreadSimFlags&~NATIVE_CANONICAL_DRIVER_THREAD_SIM_KNOWN_MASK)!=0)return 0;
		if(!NativeCanonicalDriverBehavior_ValidateKind(s->meta.driverKind,s->meta.behaviorID,s->meta.threadBehaviorID) ||
			!NativeCanonicalDriverBehavior_ResolveActiveTag(s->meta.driverKind,s->meta.behaviorID,s->meta.kartState,&resolvedActiveTag) ||
			s->active.unionTag!=resolvedActiveTag)return 0;
		if((s->meta.externalPresenceFlags&NATIVE_CANONICAL_DRIVER_EXTERNAL_ACTIVE_MASK_GRAB_OBJECT)!=0 &&
			(s->meta.driverKind!=NATIVE_CANONICAL_DRIVER_KIND_HUMAN || resolvedActiveTag!=NATIVE_CANONICAL_DRIVER_ACTIVE_MASK_GRAB))return 0;
		if(s->meta.driverKind==NATIVE_CANONICAL_DRIVER_KIND_HUMAN) { human++; if(!Zeros(s->bot.bytes,sizeof(s->bot.bytes)))return 0; }
		else { bot++; if(s->active.unionTag!=NATIVE_CANONICAL_DRIVER_ACTIVE_NONE||!Zeros(s->active.branchBytes,sizeof(s->active.branchBytes)))return 0; }
		present++;
	}
	return p->playerCount==human && p->activeBotCount==bot && present==human+bot;
}

size_t NativeCanonicalDriversDetailedV1_EncodedSize(void) { return NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES; }
int NativeCanonicalDriversDetailedV1_Encode(struct NativeCodecWriter *writer, const struct NativeCanonicalDriversDetailedV1 *value)
{
	struct NativeCodecWriter encoded;
	if(writer==NULL||!NativeCanonicalDriversDetailedV1_Validate(value)||writer->failed||writer->offset>writer->capacity||
		NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES>writer->capacity-writer->offset)return 0;
	encoded=*writer;if(!WritePrelude(&encoded,&value->prelude))return 0;
	for(uint32_t i=0;i<8;i++)if(!WriteSlot(&encoded,&value->slots[i]))return 0;
	*writer=encoded;return 1;
}
int NativeCanonicalDriversDetailedV1_BuildSummary(const struct NativeCanonicalDriversDetailedV1 *value, struct NativeCanonicalDriversV1 *summary)
{
	uint8_t bytes[NATIVE_CANONICAL_DRIVERS_NORMATIVE_BYTES];struct NativeCodecWriter writer;struct NativeCanonicalDriversV1 candidate;
	if(summary==NULL||!NativeCanonicalDriversDetailedV1_Validate(value))return 0;
	NativeCodecWriter_Init(&writer,bytes,sizeof(bytes),NULL);
	if(!NativeCanonicalDriversDetailedV1_Encode(&writer,value)||!NativeCanonicalDriversV1_FromNormativeStream(&candidate,value->prelude.presenceMask,bytes,sizeof(bytes))||
		candidate.version!=value->prelude.detailedVersion)return 0;
	*summary=candidate;return 1;
}
