#include "common.h"
#include "MainCanonicalDrivers.h"
#include "functions.h"
#include "platform/native_canonical_pool.h"

#include <limits.h>
#include <stdlib.h>

CTR_STATIC_ASSERT(NATIVE_CANONICAL_DRIVERS_RACE_BYTES == 60u);
CTR_STATIC_ASSERT(sizeof(struct NativeCanonicalDriverRaceV1) == NATIVE_CANONICAL_DRIVERS_RACE_BYTES);
CTR_STATIC_ASSERT(NATIVE_CANONICAL_DRIVER_DYN_COUNT == 52u);
CTR_STATIC_ASSERT(NATIVE_CANONICAL_DRIVERS_DYNAMICS_BYTES == 116u);
CTR_STATIC_ASSERT(sizeof(struct NativeCanonicalDriverDynamicsV1) == NATIVE_CANONICAL_DRIVERS_DYNAMICS_BYTES);
CTR_STATIC_ASSERT(NATIVE_CANONICAL_DRIVERS_ACTIVE_BYTES == 24u);
CTR_STATIC_ASSERT(sizeof(struct NativeCanonicalDriverActiveV1) == NATIVE_CANONICAL_DRIVERS_ACTIVE_BYTES);
CTR_STATIC_ASSERT(NATIVE_CANONICAL_DRIVERS_PHYSICS_BYTES == 148u);
CTR_STATIC_ASSERT(sizeof(struct NativeCanonicalDriverPhysicsV1) == NATIVE_CANONICAL_DRIVERS_PHYSICS_BYTES);
/* Native source fields declared as int are persisted as explicit signed
 * int32_t values.  Do not permit a host where that conversion changes range. */
CTR_STATIC_ASSERT(sizeof(int) == sizeof(int32_t));
CTR_STATIC_ASSERT(INT_MIN == INT32_MIN);
CTR_STATIC_ASSERT(INT_MAX == INT32_MAX);

/* Object tokens are deliberately separate from function pointers. The typed
 * matcher below is the only bridge from game callbacks to the portable codec. */
#define TOKENS(X) \
 X(none) X(driving_init) X(rev_init) X(freeze_init) X(warp_init) X(rip_init) X(tumble_init) X(plant_init) X(spin_init) X(drift_set_init) X(spin_set_init) \
 X(driving_update) X(driving_linear) X(driving_audio) X(general_angular) X(apply_forces) X(moved_search) X(collide_drivers) X(fixed_search) X(jump_friction) X(translate) X(frame_driving) X(emitter) \
 X(freeze_linear) X(freeze_update) X(freeze_reverse) X(drift_linear) X(drift_update) X(drift_angular) X(slam_update) X(slam_linear) X(slam_angular) X(slam_animate) X(spin_linear) X(spin_angular) X(frame_spinning) X(spin_update) X(last_update) X(last_linear) X(last_angular) X(frame_last) X(stop_update) X(stop_linear) X(stop_angular) X(stop_animate) X(mask_update) X(mask_linear) X(mask_animate) X(plant_update) X(plant_linear) X(plant_animate) X(rev_update) X(rev_linear) X(rev_animate) X(tumble_update) X(tumble_linear) X(tumble_angular) X(tumble_animate) X(warp_angular)
#define DECLARE_TOKEN(name) static uint8_t token_##name;
TOKENS(DECLARE_TOKEN)
#define T(name) ((const void *)&token_##name)
#define Z T(none)

static const DriverFunc initFunctions[11]={NULL,VehPhysProc_Driving_Init,VehStuckProc_RevEngine_Init,VehPhysProc_FreezeEndEvent_Init,VehStuckProc_Warp_Init,VehStuckProc_RIP_Init,VehStuckProc_Tumble_Init,VehStuckProc_PlantEaten_Init,VehPhysProc_SpinFirst_Init,VehPhysProc_PowerSlide_InitSetUpdate,VehPhysProc_SpinFirst_InitSetUpdate};
static const DriverFunc suffixFunctions[17][12]={
 {NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL},
 {VehPhysProc_Driving_Update,VehPhysProc_Driving_PhysLinear,VehPhysProc_Driving_Audio,VehPhysGeneral_PhysAngular,VehPhysForce_OnApplyForces,COLL_MOVED_PlayerSearch,VehPhysForce_CollideDrivers,COLL_FIXED_PlayerSearch,VehPhysGeneral_JumpAndFriction,VehPhysForce_TranslateMatrix,VehFrameProc_Driving,VehEmitter_DriverMain},
 {NULL,VehPhysProc_FreezeEndEvent_PhysLinear,VehPhysProc_Driving_Audio,VehPhysGeneral_PhysAngular,VehPhysForce_OnApplyForces,COLL_MOVED_PlayerSearch,VehPhysForce_CollideDrivers,COLL_FIXED_PlayerSearch,VehPhysGeneral_JumpAndFriction,VehPhysForce_TranslateMatrix,VehFrameProc_Driving,VehEmitter_DriverMain},
 {VehPhysProc_FreezeVShift_Update,VehPhysProc_Driving_PhysLinear,VehPhysProc_Driving_Audio,VehPhysGeneral_PhysAngular,VehPhysForce_OnApplyForces,COLL_MOVED_PlayerSearch,VehPhysForce_CollideDrivers,COLL_FIXED_PlayerSearch,VehPhysProc_FreezeVShift_ReverseOneFrame,VehPhysForce_TranslateMatrix,VehFrameProc_Driving,VehEmitter_DriverMain},
 {NULL,VehPhysProc_PowerSlide_PhysLinear,VehPhysProc_Driving_Audio,VehPhysProc_PowerSlide_PhysAngular,VehPhysForce_OnApplyForces,COLL_MOVED_PlayerSearch,VehPhysForce_CollideDrivers,COLL_FIXED_PlayerSearch,VehPhysGeneral_JumpAndFriction,VehPhysForce_TranslateMatrix,VehFrameProc_Driving,VehEmitter_DriverMain},
 {VehPhysProc_PowerSlide_Update,VehPhysProc_PowerSlide_PhysLinear,VehPhysProc_Driving_Audio,VehPhysProc_PowerSlide_PhysAngular,VehPhysForce_OnApplyForces,COLL_MOVED_PlayerSearch,VehPhysForce_CollideDrivers,COLL_FIXED_PlayerSearch,VehPhysGeneral_JumpAndFriction,VehPhysForce_TranslateMatrix,VehFrameProc_Driving,VehEmitter_DriverMain},
 {VehPhysProc_SlamWall_Update,VehPhysProc_SlamWall_PhysLinear,VehPhysProc_Driving_Audio,VehPhysProc_SlamWall_PhysAngular,VehPhysForce_OnApplyForces,COLL_MOVED_PlayerSearch,VehPhysForce_CollideDrivers,COLL_FIXED_PlayerSearch,VehPhysGeneral_JumpAndFriction,VehPhysForce_TranslateMatrix,VehPhysProc_SlamWall_Animate,VehEmitter_DriverMain},
 {NULL,VehPhysProc_SpinFirst_PhysLinear,VehPhysProc_Driving_Audio,VehPhysProc_SpinFirst_PhysAngular,VehPhysForce_OnApplyForces,COLL_MOVED_PlayerSearch,VehPhysForce_CollideDrivers,COLL_FIXED_PlayerSearch,VehPhysGeneral_JumpAndFriction,VehPhysForce_TranslateMatrix,VehFrameProc_Spinning,VehEmitter_DriverMain},
 {VehPhysProc_SpinFirst_Update,VehPhysProc_SpinFirst_PhysLinear,VehPhysProc_Driving_Audio,VehPhysProc_SpinFirst_PhysAngular,VehPhysForce_OnApplyForces,COLL_MOVED_PlayerSearch,VehPhysForce_CollideDrivers,COLL_FIXED_PlayerSearch,VehPhysGeneral_JumpAndFriction,VehPhysForce_TranslateMatrix,VehFrameProc_Spinning,VehEmitter_DriverMain},
 {VehPhysProc_SpinLast_Update,VehPhysProc_SpinLast_PhysLinear,VehPhysProc_Driving_Audio,VehPhysProc_SpinLast_PhysAngular,VehPhysForce_OnApplyForces,COLL_MOVED_PlayerSearch,VehPhysForce_CollideDrivers,COLL_FIXED_PlayerSearch,VehPhysGeneral_JumpAndFriction,VehPhysForce_TranslateMatrix,VehFrameProc_LastSpin,VehEmitter_DriverMain},
 {VehPhysProc_SpinStop_Update,VehPhysProc_SpinStop_PhysLinear,VehPhysProc_Driving_Audio,VehPhysProc_SpinStop_PhysAngular,VehPhysForce_OnApplyForces,COLL_MOVED_PlayerSearch,VehPhysForce_CollideDrivers,COLL_FIXED_PlayerSearch,VehPhysGeneral_JumpAndFriction,VehPhysForce_TranslateMatrix,VehPhysProc_SpinStop_Animate,VehEmitter_DriverMain},
 {VehStuckProc_MaskGrab_Update,VehStuckProc_MaskGrab_PhysLinear,VehPhysProc_Driving_Audio,VehPhysGeneral_PhysAngular,VehPhysForce_OnApplyForces,COLL_MOVED_PlayerSearch,VehPhysForce_CollideDrivers,COLL_FIXED_PlayerSearch,VehPhysGeneral_JumpAndFriction,VehPhysForce_TranslateMatrix,VehStuckProc_MaskGrab_Animate,VehEmitter_DriverMain},
 {VehStuckProc_PlantEaten_Update,VehStuckProc_PlantEaten_PhysLinear,VehPhysProc_Driving_Audio,NULL,NULL,NULL,NULL,NULL,NULL,NULL,VehStuckProc_PlantEaten_Animate,NULL},
 {NULL,VehStuckProc_PlantEaten_PhysLinear,VehPhysProc_Driving_Audio,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL},
 {VehStuckProc_RevEngine_Update,VehStuckProc_RevEngine_PhysLinear,VehPhysProc_Driving_Audio,NULL,NULL,NULL,NULL,NULL,NULL,VehPhysForce_TranslateMatrix,VehStuckProc_RevEngine_Animate,VehEmitter_DriverMain},
 {VehStuckProc_Tumble_Update,VehStuckProc_Tumble_PhysLinear,VehPhysProc_Driving_Audio,VehStuckProc_Tumble_PhysAngular,VehPhysForce_OnApplyForces,COLL_MOVED_PlayerSearch,VehPhysForce_CollideDrivers,COLL_FIXED_PlayerSearch,VehPhysGeneral_JumpAndFriction,VehPhysForce_TranslateMatrix,VehStuckProc_Tumble_Animate,VehEmitter_DriverMain},
 {NULL,NULL,VehPhysProc_Driving_Audio,VehStuckProc_Warp_PhysAngular,NULL,NULL,NULL,NULL,NULL,VehPhysForce_TranslateMatrix,VehFrameProc_Driving,VehEmitter_DriverMain}
};
static const struct NativeCanonicalDriverBehaviorRegistry productionRegistry={
 {Z,T(driving_init),T(rev_init),T(freeze_init),T(warp_init),T(rip_init),T(tumble_init),T(plant_init),T(spin_init),T(drift_set_init),T(spin_set_init)},
 {
  {Z,Z,Z,Z,Z,Z,Z,Z,Z,Z,Z,Z},
  {T(driving_update),T(driving_linear),T(driving_audio),T(general_angular),T(apply_forces),T(moved_search),T(collide_drivers),T(fixed_search),T(jump_friction),T(translate),T(frame_driving),T(emitter)},
  {Z,T(freeze_linear),T(driving_audio),T(general_angular),T(apply_forces),T(moved_search),T(collide_drivers),T(fixed_search),T(jump_friction),T(translate),T(frame_driving),T(emitter)},
  {T(freeze_update),T(driving_linear),T(driving_audio),T(general_angular),T(apply_forces),T(moved_search),T(collide_drivers),T(fixed_search),T(freeze_reverse),T(translate),T(frame_driving),T(emitter)},
  {Z,T(drift_linear),T(driving_audio),T(drift_angular),T(apply_forces),T(moved_search),T(collide_drivers),T(fixed_search),T(jump_friction),T(translate),T(frame_driving),T(emitter)},
  {T(drift_update),T(drift_linear),T(driving_audio),T(drift_angular),T(apply_forces),T(moved_search),T(collide_drivers),T(fixed_search),T(jump_friction),T(translate),T(frame_driving),T(emitter)},
  {T(slam_update),T(slam_linear),T(driving_audio),T(slam_angular),T(apply_forces),T(moved_search),T(collide_drivers),T(fixed_search),T(jump_friction),T(translate),T(slam_animate),T(emitter)},
  {Z,T(spin_linear),T(driving_audio),T(spin_angular),T(apply_forces),T(moved_search),T(collide_drivers),T(fixed_search),T(jump_friction),T(translate),T(frame_spinning),T(emitter)},
  {T(spin_update),T(spin_linear),T(driving_audio),T(spin_angular),T(apply_forces),T(moved_search),T(collide_drivers),T(fixed_search),T(jump_friction),T(translate),T(frame_spinning),T(emitter)},
  {T(last_update),T(last_linear),T(driving_audio),T(last_angular),T(apply_forces),T(moved_search),T(collide_drivers),T(fixed_search),T(jump_friction),T(translate),T(frame_last),T(emitter)},
  {T(stop_update),T(stop_linear),T(driving_audio),T(stop_angular),T(apply_forces),T(moved_search),T(collide_drivers),T(fixed_search),T(jump_friction),T(translate),T(stop_animate),T(emitter)},
  {T(mask_update),T(mask_linear),T(driving_audio),T(general_angular),T(apply_forces),T(moved_search),T(collide_drivers),T(fixed_search),T(jump_friction),T(translate),T(mask_animate),T(emitter)},
  {T(plant_update),T(plant_linear),T(driving_audio),Z,Z,Z,Z,Z,Z,Z,T(plant_animate),Z},
  {Z,T(plant_linear),T(driving_audio),Z,Z,Z,Z,Z,Z,Z,Z,Z},
  {T(rev_update),T(rev_linear),T(driving_audio),Z,Z,Z,Z,Z,Z,T(translate),T(rev_animate),T(emitter)},
  {T(tumble_update),T(tumble_linear),T(driving_audio),T(tumble_angular),T(apply_forces),T(moved_search),T(collide_drivers),T(fixed_search),T(jump_friction),T(translate),T(tumble_animate),T(emitter)},
  {Z,Z,T(driving_audio),T(warp_angular),Z,Z,Z,Z,Z,T(translate),T(frame_driving),T(emitter)}
 }};

static const void *TokenFor(DriverFunc f){
 if(!f)return T(none);
#define MATCH(fn,name) if(f==fn)return T(name)
 MATCH(VehPhysProc_Driving_Init,driving_init);MATCH(VehStuckProc_RevEngine_Init,rev_init);MATCH(VehPhysProc_FreezeEndEvent_Init,freeze_init);MATCH(VehStuckProc_Warp_Init,warp_init);MATCH(VehStuckProc_RIP_Init,rip_init);MATCH(VehStuckProc_Tumble_Init,tumble_init);MATCH(VehStuckProc_PlantEaten_Init,plant_init);MATCH(VehPhysProc_SpinFirst_Init,spin_init);MATCH(VehPhysProc_PowerSlide_InitSetUpdate,drift_set_init);MATCH(VehPhysProc_SpinFirst_InitSetUpdate,spin_set_init);
 MATCH(VehPhysProc_Driving_Update,driving_update);MATCH(VehPhysProc_Driving_PhysLinear,driving_linear);MATCH(VehPhysProc_Driving_Audio,driving_audio);MATCH(VehPhysGeneral_PhysAngular,general_angular);MATCH(VehPhysForce_OnApplyForces,apply_forces);MATCH(COLL_MOVED_PlayerSearch,moved_search);MATCH(VehPhysForce_CollideDrivers,collide_drivers);MATCH(COLL_FIXED_PlayerSearch,fixed_search);MATCH(VehPhysGeneral_JumpAndFriction,jump_friction);MATCH(VehPhysForce_TranslateMatrix,translate);MATCH(VehFrameProc_Driving,frame_driving);MATCH(VehEmitter_DriverMain,emitter);
 MATCH(VehPhysProc_FreezeEndEvent_PhysLinear,freeze_linear);MATCH(VehPhysProc_FreezeVShift_Update,freeze_update);MATCH(VehPhysProc_FreezeVShift_ReverseOneFrame,freeze_reverse);MATCH(VehPhysProc_PowerSlide_PhysLinear,drift_linear);MATCH(VehPhysProc_PowerSlide_Update,drift_update);MATCH(VehPhysProc_PowerSlide_PhysAngular,drift_angular);MATCH(VehPhysProc_SlamWall_Update,slam_update);MATCH(VehPhysProc_SlamWall_PhysLinear,slam_linear);MATCH(VehPhysProc_SlamWall_PhysAngular,slam_angular);MATCH(VehPhysProc_SlamWall_Animate,slam_animate);MATCH(VehPhysProc_SpinFirst_PhysLinear,spin_linear);MATCH(VehPhysProc_SpinFirst_PhysAngular,spin_angular);MATCH(VehFrameProc_Spinning,frame_spinning);MATCH(VehPhysProc_SpinFirst_Update,spin_update);MATCH(VehPhysProc_SpinLast_Update,last_update);MATCH(VehPhysProc_SpinLast_PhysLinear,last_linear);MATCH(VehPhysProc_SpinLast_PhysAngular,last_angular);MATCH(VehFrameProc_LastSpin,frame_last);MATCH(VehPhysProc_SpinStop_Update,stop_update);MATCH(VehPhysProc_SpinStop_PhysLinear,stop_linear);MATCH(VehPhysProc_SpinStop_PhysAngular,stop_angular);MATCH(VehPhysProc_SpinStop_Animate,stop_animate);
 MATCH(VehStuckProc_MaskGrab_Update,mask_update);MATCH(VehStuckProc_MaskGrab_PhysLinear,mask_linear);MATCH(VehStuckProc_MaskGrab_Animate,mask_animate);MATCH(VehStuckProc_PlantEaten_Update,plant_update);MATCH(VehStuckProc_PlantEaten_PhysLinear,plant_linear);MATCH(VehStuckProc_PlantEaten_Animate,plant_animate);MATCH(VehStuckProc_RevEngine_Update,rev_update);MATCH(VehStuckProc_RevEngine_PhysLinear,rev_linear);MATCH(VehStuckProc_RevEngine_Animate,rev_animate);MATCH(VehStuckProc_Tumble_Update,tumble_update);MATCH(VehStuckProc_Tumble_PhysLinear,tumble_linear);MATCH(VehStuckProc_Tumble_PhysAngular,tumble_angular);MATCH(VehStuckProc_Tumble_Animate,tumble_animate);MATCH(VehStuckProc_Warp_PhysAngular,warp_angular);
 return NULL;
}
static int RegistrySelfTest(void){uint8_t id;for(uint8_t init=0;init<11;init++)for(uint8_t suffix=0;suffix<17;suffix++){const void*t[13];t[0]=TokenFor(initFunctions[init]);for(uint8_t n=0;n<12;n++)t[n+1]=TokenFor(suffixFunctions[suffix][n]);if(!NativeCanonicalDriverBehavior_Resolve(&productionRegistry,t,&id)||id!=(uint8_t)(suffix+17*init))return (int)(suffix+17*init+1);}return 0;}
int MainCanonicalDrivers_ValidateProductionBinding(void){int failure;if(!NativeCanonicalDriverBehaviorRegistry_Validate(&productionRegistry))return -1;failure=RegistrySelfTest();return failure?-(failure+2):1;}
const struct NativeCanonicalDriverBehaviorRegistry *MainCanonicalDrivers_ProductionRegistry(void){return MainCanonicalDrivers_ValidateProductionBinding()==1?&productionRegistry:NULL;}
int MainCanonicalDrivers_ResolveBehavior(const DriverFunc table[13],uint8_t*out){const struct NativeCanonicalDriverBehaviorRegistry*r;const void*tokens[13];uint8_t id;if(!table||!out||(r=MainCanonicalDrivers_ProductionRegistry())==NULL)return MAIN_CANONICAL_DRIVERS_FAILURE;for(uint8_t n=0;n<13;n++)if((tokens[n]=TokenFor(table[n]))==NULL)return MAIN_CANONICAL_DRIVERS_FAILURE;if(!NativeCanonicalDriverBehavior_Resolve(r,tokens,&id))return MAIN_CANONICAL_DRIVERS_FAILURE;*out=id;return MAIN_CANONICAL_DRIVERS_OK;}
int MainCanonicalDrivers_ResolveThread(void (*thread)(struct Thread *),uint8_t*out){uint8_t id;if(!out)return 0;if(!thread)id=0;else if(thread==VehBirth_NullThread)id=1;else if(thread==BOTS_ThTick_Drive)id=2;else if(thread==BOTS_ThTick_RevEngine)id=3;else return 0;*out=id;return 1;}
int MainCanonicalDrivers_ProjectPrelude(const struct NativeCanonicalDriversRosterInput*input,const DriverFunc tables[8][13],void (*const threads[8])(struct Thread *),struct NativeCanonicalDriversRosterCandidate*out){struct NativeCanonicalDriversRosterInput local;struct NativeCanonicalDriversRosterCandidate candidate;if(!input||!tables||!threads||!out)return 0;local=*input;for(uint8_t n=0;n<8;n++)if(local.slots[n].present==1){if(!MainCanonicalDrivers_ResolveBehavior(tables[n],&local.slots[n].behaviorID)||!MainCanonicalDrivers_ResolveThread(threads[n],&local.slots[n].threadBehaviorID))return 0;}if(!NativeCanonicalDriversRoster_Normalize(&local,&candidate))return 0;*out=candidate;return 1;}

static int DriverSlot(const struct Driver *const drivers[8],const struct Driver *driver,uint8_t *slot)
{
	if(!driver||!slot)return 0;
	for(uint8_t n=0;n<8;n++)if(drivers[n]==driver){*slot=n;return 1;}
	return 0;
}

/* A nav cursor is untrusted until it exactly matches the embedded Item of an
 * already validated stable driver root. Never dereference a list pointer
 * before this equality scan succeeds. */
static int NavItemSlot(const struct Driver *const drivers[8],const struct Item *item,uint8_t *slot)
{
	if(!item||!slot)return 0;
	for(uint8_t n=0;n<8;n++)if(drivers[n]&&item==&drivers[n]->botData.item){*slot=n;return 1;}
	return 0;
}

static int NavLists(const struct Driver *const drivers[8],const struct sData *sourceData,
	struct NativeCanonicalDriversRosterInput *input)
{
	uint32_t seen=0,botMask=0;
	for(uint8_t n=0;n<8;n++)if(drivers[n]&&(drivers[n]->actionsFlagSet&ACTION_BOT))botMask|=1u<<n;
	for(uint8_t path=0;path<3;path++)
	{
		const struct LinkedList *list=&sourceData->navBotList[path];
		const struct Item *item,*previous=NULL;
		uint8_t endpointSlot;
		if(list->count<0||list->count>8)return 0;
		if(list->count==0){if(list->first||list->last)return 0;continue;}
		if(!list->first||!list->last||!NavItemSlot(drivers,list->first,&endpointSlot)||!NavItemSlot(drivers,list->last,&endpointSlot))return 0;
		item=list->first;
		for(int count=0;count<list->count;count++)
		{
			uint8_t slot;
			if(!NavItemSlot(drivers,item,&slot))return 0;
			if(item->prev!=previous||(seen&(1u<<slot))||(drivers[slot]->actionsFlagSet&ACTION_BOT)==0||drivers[slot]->botData.botPath!=path)return 0;
			seen|=1u<<slot;input->navOrder[path][count]=slot;previous=item;item=item->next;
		}
		if(item||previous!=list->last)return 0;
		input->navCount[path]=(uint8_t)list->count;
	}
	return seen==botMask;
}

static struct NativeCanonicalPoolInput PoolInput(const struct JitPool *pool)
{
	struct NativeCanonicalPoolInput input;
	input.base=pool->ptrPoolData;
	input.maxItems=pool->maxItems;
	input.itemSize=pool->itemSize;
	input.poolSize=pool->poolSize;
	return input;
}

static struct NativeCanonicalPoolList PoolList(const struct LinkedList *list)
{
	struct NativeCanonicalPoolList result;
	result.first=(const struct NativeCanonicalPoolItem *)list->first;
	result.last=(const struct NativeCanonicalPoolItem *)list->last;
	result.count=list->count;
	return result;
}

/* The native large-stack item begins with the JitPool Item header.  The
 * Driver payload starts exactly eight bytes later; validate that relation
 * before reading a single Driver field, then derive the proven slot address
 * with integer arithmetic for the free-list ownership query. */
static int PoolPayloadAllocatedFromFree(const struct NativeCanonicalPoolGeometry *geometry,
	const struct NativeCanonicalPoolList *freeList,const void *payload,size_t offset,size_t size)
{
	uint32_t index,ownedIndex;
	uintptr_t slot;
	if(!NativeCanonicalPool_PayloadStartIndex(geometry,payload,offset,size,&index))return 0;
	slot=geometry->base+(uintptr_t)index*geometry->stride;
	return NativeCanonicalPool_AllocatedFromFree(geometry,freeList,(const void *)slot,&ownedIndex)&&ownedIndex==index;
}

static int SnapshotRootPools(const struct GameTracker *gGT,
	struct NativeCanonicalPoolGeometry *large,struct NativeCanonicalPoolGeometry *thread,
	struct NativeCanonicalPoolGeometry *instance,
	struct NativeCanonicalPoolList *largeFree,struct NativeCanonicalPoolList *threadFree,
	struct NativeCanonicalPoolList *instanceFree,struct NativeCanonicalPoolList *instanceTaken)
{
	const struct JitPool *largePool=&gGT->JitPools.largeStack;
	const struct JitPool *threadPool=&gGT->JitPools.thread;
	const struct JitPool *instancePool=&gGT->JitPools.instance;
	struct NativeCanonicalPoolInput largeInput,threadInput,instanceInput;
	size_t expectedInstanceSize;
	if((size_t)gGT->numPlyrCurrGame>(UINT32_MAX-sizeof(struct Instance))/sizeof(struct InstDrawPerPlayer))return 0;
	expectedInstanceSize=sizeof(struct Instance)+sizeof(struct InstDrawPerPlayer)*(size_t)gGT->numPlyrCurrGame;
	if(largePool->itemSize!=0x670u||threadPool->itemSize!=sizeof(struct Thread)||
		instancePool->itemSize!=expectedInstanceSize)return 0;
	largeInput=PoolInput(largePool);threadInput=PoolInput(threadPool);instanceInput=PoolInput(instancePool);
	if(!NativeCanonicalPool_GeometrySnapshot(&largeInput,large)||
		!NativeCanonicalPool_GeometrySnapshot(&threadInput,thread)||
		!NativeCanonicalPool_GeometrySnapshot(&instanceInput,instance))return 0;
	if(large->stride!=0x670u||thread->stride!=sizeof(struct Thread)||instance->stride!=expectedInstanceSize)return 0;
	*largeFree=PoolList(&largePool->free);
	*threadFree=PoolList(&threadPool->free);
	*instanceFree=PoolList(&instancePool->free);
	*instanceTaken=PoolList(&instancePool->taken);
	return 1;
}

static int ValidateDriverRootOwnership(const struct Driver *driver,
	const struct NativeCanonicalPoolGeometry *large,const struct NativeCanonicalPoolList *largeFree,
	const struct NativeCanonicalPoolGeometry *thread,const struct NativeCanonicalPoolList *threadFree,
	const struct NativeCanonicalPoolGeometry *instance,const struct NativeCanonicalPoolList *instanceFree,
	const struct NativeCanonicalPoolList *instanceTaken)
{
	const struct Instance *inst;
	const struct Thread *nativeThread;
	uint32_t ignored;
	if(!PoolPayloadAllocatedFromFree(large,largeFree,driver,sizeof(struct Item),DRIVER_NTSC_RETAIL_SIZE))return 0;
	/* driver is now a proven large-stack payload; only now read instSelf. */
	inst=driver->instSelf;
	if(!inst||!NativeCanonicalPool_AllocatedInTaken(instance,instanceTaken,instanceFree,
		NATIVE_CANONICAL_POOL_FREE_LIST_REQUIRED,inst,&ignored))return 0;
	/* inst is now a proven instance slot; only now read thread. */
	nativeThread=inst->thread;
	if(!nativeThread||!NativeCanonicalPool_AllocatedFromFree(thread,threadFree,nativeThread,&ignored))return 0;
	return nativeThread->object==driver&&nativeThread->inst==inst&&
		(nativeThread->flags&THREAD_FLAG_DEAD)==0;
}

static int SnapshotMetaPools(const struct GameTracker *gGT,
	struct NativeCanonicalPoolGeometry *thread,struct NativeCanonicalPoolGeometry *instance,
	struct NativeCanonicalPoolGeometry *small,struct NativeCanonicalPoolList *threadFree,
	struct NativeCanonicalPoolList *instanceFree,struct NativeCanonicalPoolList *instanceTaken,
	struct NativeCanonicalPoolList *smallFree)
{
	const struct JitPool *threadPool=&gGT->JitPools.thread;
	const struct JitPool *instancePool=&gGT->JitPools.instance;
	const struct JitPool *smallPool=&gGT->JitPools.smallStack;
	struct NativeCanonicalPoolInput threadInput,instanceInput,smallInput;
	size_t expectedInstanceSize;
	if((size_t)gGT->numPlyrCurrGame>(UINT32_MAX-sizeof(struct Instance))/sizeof(struct InstDrawPerPlayer))return 0;
	expectedInstanceSize=sizeof(struct Instance)+sizeof(struct InstDrawPerPlayer)*(size_t)gGT->numPlyrCurrGame;
	if(threadPool->itemSize!=sizeof(struct Thread)||instancePool->itemSize!=expectedInstanceSize||smallPool->itemSize!=0x48u)return 0;
	threadInput=PoolInput(threadPool);instanceInput=PoolInput(instancePool);smallInput=PoolInput(smallPool);
	if(!NativeCanonicalPool_GeometrySnapshot(&threadInput,thread)||
		!NativeCanonicalPool_GeometrySnapshot(&instanceInput,instance)||
		!NativeCanonicalPool_GeometrySnapshot(&smallInput,small))return 0;
	if(thread->stride!=sizeof(struct Thread)||instance->stride!=expectedInstanceSize||small->stride!=0x48u)return 0;
	*threadFree=PoolList(&threadPool->free);
	*instanceFree=PoolList(&instancePool->free);
	*instanceTaken=PoolList(&instancePool->taken);
	*smallFree=PoolList(&smallPool->free);
	return 1;
}

static int ValidateMetaThread(const struct Thread *thread,const struct Thread *root,
	const struct NativeCanonicalPoolGeometry *threadGeometry,const struct NativeCanonicalPoolList *threadFree,
	const struct NativeCanonicalPoolGeometry *instanceGeometry,const struct NativeCanonicalPoolList *instanceFree,
	const struct NativeCanonicalPoolList *instanceTaken,uint32_t *threadIndex)
{
	const struct Instance *instance;
	uint32_t ignored;
	if(!threadIndex||!NativeCanonicalPool_AllocatedFromFree(threadGeometry,threadFree,thread,threadIndex))return 0;
	if(thread->parentThread!=root||(thread->flags&THREAD_FLAG_DEAD)!=0)return 0;
	instance=thread->inst;
	if(!instance||!NativeCanonicalPool_AllocatedInTaken(instanceGeometry,instanceTaken,instanceFree,
		NATIVE_CANONICAL_POOL_FREE_LIST_REQUIRED,instance,&ignored))return 0;
	return instance->thread==thread;
}

/* One ownership walk for external game attachments.  Callers choose which
 * optional owned objects are semantically relevant; the walk itself always
 * proves every child before dereferencing it. */
static int MainCanonicalDrivers_ResolveAttachmentFlags(const struct GameTracker *gGT,
	const struct Driver *driver,const struct Thread *cloudThread,
	const struct MaskHeadWeapon *maskObject,int emitMaskFlag,struct MainCanonicalDriversMetaFlags *out)
{
	struct NativeCanonicalPoolGeometry threadGeometry,instanceGeometry,smallGeometry;
	struct NativeCanonicalPoolList threadFree,instanceFree,instanceTaken,smallFree;
	struct MainCanonicalDriversMetaFlags candidate={0};
	const struct Instance *rootInstance;
	const struct Thread *rootThread,*child;
	uint32_t ignored,rootIndex,cloudMatches=0,maskMatches=0;
	uint8_t *seenChildren;
	int success=0;
	if(!gGT||!driver||!out||
		!SnapshotMetaPools(gGT,&threadGeometry,&instanceGeometry,&smallGeometry,&threadFree,&instanceFree,&instanceTaken,&smallFree))return 0;
	/* The Driver's own large-stack root is an explicit precondition from the
	 * roster gate. Revalidate the linked instance/thread before touching child
	 * pointers, so no unowned child graph can be dereferenced. */
	rootInstance=driver->instSelf;
	if(!rootInstance||!NativeCanonicalPool_AllocatedInTaken(&instanceGeometry,&instanceTaken,&instanceFree,
		NATIVE_CANONICAL_POOL_FREE_LIST_REQUIRED,rootInstance,&ignored))return 0;
	rootThread=rootInstance->thread;
	if(!rootThread||!NativeCanonicalPool_AllocatedFromFree(&threadGeometry,&threadFree,rootThread,&rootIndex)||
		rootThread->object!=driver||rootThread->inst!=rootInstance||(rootThread->flags&THREAD_FLAG_DEAD)!=0||
		rootThread->parentThread==rootThread)return 0;
	if((rootThread->flags&THREAD_FLAG_DISABLE_COLLISION)!=0)
		candidate.driverThreadSimFlags=NATIVE_CANONICAL_DRIVER_THREAD_SIM_COLLISION_DISABLED;
	seenChildren=(uint8_t *)calloc(threadGeometry.maxItems,sizeof(*seenChildren));
	if(!seenChildren)return 0;
	child=rootThread->childThread;
	for(uint32_t count=0;child!=NULL&&count<threadGeometry.maxItems;count++)
	{
		const struct Thread *next;
		uint32_t childIndex;
		/* Root is never an immediate child.  Every other cursor must prove its
		 * exact pool slot before its fields are read; seen-by-index prevents a
		 * repeat/cycle from being mistaken for a merely long valid list. */
		if(child==rootThread||!ValidateMetaThread(child,rootThread,&threadGeometry,&threadFree,
			&instanceGeometry,&instanceFree,&instanceTaken,&childIndex)||childIndex==rootIndex||
			childIndex>=threadGeometry.maxItems||seenChildren[childIndex]!=0)goto done;
		seenChildren[childIndex]=1;
		next=child->siblingThread;
		if(cloudThread&&child==cloudThread)
		{
			if(++cloudMatches!=1||child->funcThTick!=RB_RainCloud_ThTick||child->modelIndex!=STATIC_CLOUD||
				(child->flags&0x300u)!=SMALL||!PoolPayloadAllocatedFromFree(&smallGeometry,&smallFree,child->object,
				sizeof(struct Item),sizeof(struct RainCloud)))goto done;
			candidate.externalPresenceFlags|=NATIVE_CANONICAL_DRIVER_EXTERNAL_RAIN_CLOUD;
		}
		if(maskObject&&child->object==maskObject)
		{
			if(++maskMatches!=1||child->funcThTick!=RB_MaskWeapon_ThTick||
				(child->modelIndex!=STATIC_AKUAKU&&child->modelIndex!=STATIC_UKAUKA)||(child->flags&0x300u)!=SMALL||
				!PoolPayloadAllocatedFromFree(&smallGeometry,&smallFree,child->object,sizeof(struct Item),sizeof(struct MaskHeadWeapon)))goto done;
			if(emitMaskFlag)candidate.externalPresenceFlags|=NATIVE_CANONICAL_DRIVER_EXTERNAL_ACTIVE_MASK_GRAB_OBJECT;
		}
		child=next;
	}
	if(child!=NULL||(cloudThread&&cloudMatches!=1)||(maskObject&&maskMatches!=1))goto done;
	success=1;
done:
	free(seenChildren);
	if(success)*out=candidate;
	return success;
}

int MainCanonicalDrivers_ResolveMetaFlags(const struct GameTracker *gGT,
	const struct Driver *driver,uint8_t kind,uint8_t behaviorID,uint8_t kartState,
	struct MainCanonicalDriversMetaFlags *out)
{
	int wantsMask;
	if(!gGT||!driver||!out||driver->kartState!=kartState||
		!NativeCanonicalDriverBehavior_IsMaskGrabActive(kind,behaviorID,kartState,&wantsMask))return 0;
	return MainCanonicalDrivers_ResolveAttachmentFlags(gGT,driver,driver->thCloud,
		wantsMask?driver->KartStates.MaskGrab.maskObj:NULL,wantsMask,out);
}

int MainCanonicalDrivers_ExtractRosterPrelude(const struct GameTracker *gGT,const struct sData *sourceData,struct NativeCanonicalDriversRosterCandidate *out)
{
	struct NativeCanonicalDriversRosterInput input;
	DriverFunc tables[8][13]={{0}};
	void(*threads[8])(struct Thread *)={0};
	const struct Driver *drivers[8];
	struct NativeCanonicalPoolGeometry largeGeometry,threadGeometry,instanceGeometry;
	struct NativeCanonicalPoolList largeFree,threadFree,instanceFree,instanceTaken;
	int anyRoot=0;
	if(!gGT||!sourceData||!out||sourceData->gGT!=gGT||gGT->numLaps<0)return 0;
	memset(&input,0,sizeof(input));
	memset(input.raceOrder,0xff,sizeof(input.raceOrder));
	memset(input.winnerDriverIDs,0xff,sizeof(input.winnerDriverIDs));
	memset(input.ranks,0xff,sizeof(input.ranks));
	memset(input.navOrder,0xff,sizeof(input.navOrder));
	input.numLaps=gGT->numLaps;
	/* Root addresses are compared but never dereferenced until all three pool
	 * snapshots prove ownership.  With no roots, menu/reset phases deliberately
	 * accept uninitialized pools: there is no object to inspect. */
	for(uint8_t slot=0;slot<8;slot++)
	{
		drivers[slot]=gGT->drivers[slot];
		if(!drivers[slot])continue;
		anyRoot=1;
		for(uint8_t prior=0;prior<slot;prior++)if(drivers[prior]==drivers[slot])return 0;
	}
	if(anyRoot&&!SnapshotRootPools(gGT,&largeGeometry,&threadGeometry,&instanceGeometry,
		&largeFree,&threadFree,&instanceFree,&instanceTaken))return 0;
	for(uint8_t slot=0;slot<8;slot++)
	{
		const struct Driver *driver=drivers[slot];
		if(!driver)continue;
		if(!ValidateDriverRootOwnership(driver,&largeGeometry,&largeFree,&threadGeometry,&threadFree,
			&instanceGeometry,&instanceFree,&instanceTaken)||driver->driverID!=slot)return 0;
		input.slots[slot].present=1;input.slots[slot].driverID=slot;
		input.slots[slot].kind=(driver->actionsFlagSet&ACTION_BOT)?NATIVE_CANONICAL_DRIVER_KIND_BOT:NATIVE_CANONICAL_DRIVER_KIND_HUMAN;
		memcpy(tables[slot],driver->funcPtrs,sizeof(tables[slot]));threads[slot]=driver->instSelf->thread->funcThTick;
	}
	for(uint8_t n=0,tail=0;n<8;n++)
	{
		uint8_t slot;const struct Driver *driver=gGT->driversInRaceOrder[n];
		if(!driver){tail=1;continue;}
		if(tail||!DriverSlot(drivers,driver,&slot))return 0;
		for(uint8_t prior=0;prior<n;prior++)if(input.raceOrder[prior]==slot)return 0;
		input.raceOrder[input.raceOrderCount++]=slot;
	}
	if(gGT->numWinners>4)return 0;
	input.winnerCount=(uint8_t)gGT->numWinners;
	for(uint8_t n=0;n<input.winnerCount;n++)
	{
		int id=gGT->winnerIndex[n];uint8_t slot;
		if(id<0||id>7)return 0;
		for(slot=0;slot<8;slot++)if(drivers[slot]&&drivers[slot]->driverID==(uint8_t)id)break;
		if(slot==8)return 0;input.winnerDriverIDs[n]=(uint8_t)id;
	}
	for(uint8_t slot=0;slot<8;slot++)if(drivers[slot]&&(drivers[slot]->actionsFlagSet&ACTION_BOT)==0)
	{
		if(gGT->humanPlayerPositions[slot]>7)return 0;
		input.ranks[input.playerCount++]=gGT->humanPlayerPositions[slot];
	}
	if(!NavLists(drivers,sourceData,&input))return 0;
	return MainCanonicalDrivers_ProjectPrelude(&input,tables,threads,out);
}

static void MainCanonicalDrivers_CopyRace(const struct Driver *driver, struct NativeCanonicalDriverRaceV1 *race)
{
	race->clockReceive = driver->clockReceive;
	race->hazardTimer = driver->hazardTimer;
	race->superEngineTimer = driver->superEngineTimer;
	race->itemRollTimer = driver->itemRollTimer;
	race->noItemTimer = driver->noItemTimer;
	race->jumpMeter = driver->jumpMeter;
	race->jumpMeterTimer = driver->jumpMeterTimer;
	race->numTurbos = driver->numTurbos;
	race->invincibleTimer = (int32_t)driver->invincibleTimer;
	race->invisibleTimer = (int32_t)driver->invisibleTimer;
	race->lapTime = (int32_t)driver->lapTime;
	race->timeElapsedInRace = (int32_t)driver->timeElapsedInRace;
	race->driverRank = driver->driverRank;
	race->checkpointBranchChoiceIndex = driver->checkpoint.branchChoiceIndex;
	race->checkpointCurrentIndex = driver->checkpoint.currentIndex;
	race->distanceToFinishCurr = driver->distanceToFinish_curr;
	race->distanceToFinishCheckpoint = driver->distanceToFinish_checkpoint;
	race->distanceDrivenBackwards = driver->distanceDrivenBackwards;
	race->battleNumLives = (int32_t)driver->BattleHUD.numLives;
	race->battleTeamID = (int32_t)driver->BattleHUD.teamID;
	race->pickupLetterCount = (int32_t)driver->PickupLetterHUD.numCollected;
}

int MainCanonicalDrivers_ExtractRosterRace(const struct GameTracker *gGT,const struct sData *sourceData,
	struct MainCanonicalDriversRosterRaceCandidate *out)
{
	struct MainCanonicalDriversRosterRaceCandidate candidate;

	if(!out || !MainCanonicalDrivers_ExtractRosterPrelude(gGT,sourceData,&candidate.roster))return 0;
	memset(candidate.race,0,sizeof(candidate.race));
	for(uint8_t slot=0;slot<8;slot++)
	{
		if((candidate.roster.prelude.presenceMask&(UINT32_C(1)<<slot))==0)continue;
		/* The prelude call validated this root, its identity, ownership,
		 * behavior, and nav membership before any candidate is published. */
		if(gGT->drivers[slot]==NULL)return 0;
		MainCanonicalDrivers_CopyRace(gGT->drivers[slot],&candidate.race[slot]);
	}
	*out=candidate;
	return 1;
}

static void MainCanonicalDrivers_CopyDynamics(const struct Driver *driver,
	struct NativeCanonicalDriverDynamicsV1 *dynamics)
{
	int16_t *field=dynamics->field;
	field[NATIVE_CANONICAL_DRIVER_DYN_AMP_TURN_STATE]=driver->ampTurnState;
	field[NATIVE_CANONICAL_DRIVER_DYN_BUTTON_USED_TO_START_DRIFT]=driver->buttonUsedToStartDrift;
	field[NATIVE_CANONICAL_DRIVER_DYN_WALL_RUB_SPEED_LIMIT]=driver->wallRubSpeedLimit;
	field[NATIVE_CANONICAL_DRIVER_DYN_WHEEL_ROTATION]=driver->wheelRotation;
	field[NATIVE_CANONICAL_DRIVER_DYN_SPEED]=driver->speed;
	field[NATIVE_CANONICAL_DRIVER_DYN_SPEED_APPROX]=driver->speedApprox;
	field[NATIVE_CANONICAL_DRIVER_DYN_JUMP_HEIGHT_CURR]=driver->jumpHeightCurr;
	field[NATIVE_CANONICAL_DRIVER_DYN_JUMP_HEIGHT_PREV]=driver->jumpHeightPrev;
	field[NATIVE_CANONICAL_DRIVER_DYN_AXIS_ROTATION_Y]=driver->axisRotationY;
	field[NATIVE_CANONICAL_DRIVER_DYN_AXIS_ROTATION_X]=driver->axisRotationX;
	field[NATIVE_CANONICAL_DRIVER_DYN_ANGLE]=driver->angle;
	field[NATIVE_CANONICAL_DRIVER_DYN_BASE_SPEED]=driver->baseSpeed;
	field[NATIVE_CANONICAL_DRIVER_DYN_FIRE_SPEED]=driver->fireSpeed;
	field[NATIVE_CANONICAL_DRIVER_DYN_FORWARD_ACCEL_IMPULSE]=driver->forwardAccelImpulse;
	field[NATIVE_CANONICAL_DRIVER_DYN_ROTATION_SPIN_RATE]=driver->rotationSpinRate;
	field[NATIVE_CANONICAL_DRIVER_DYN_ACCEL_TAP_WINDOW_TIMER]=driver->accelTapWindowTimer;
	field[NATIVE_CANONICAL_DRIVER_DYN_ACCEL_TAP_COUNT]=driver->accelTapCount;
	field[NATIVE_CANONICAL_DRIVER_DYN_TERRAIN_SCALED_BASE_SPEED]=driver->terrainScaledBaseSpeed;
	field[NATIVE_CANONICAL_DRIVER_DYN_TURN_ANGLE_CURR]=driver->turnAngleCurr;
	field[NATIVE_CANONICAL_DRIVER_DYN_TURN_ANGLE_PREV]=driver->turnAnglePrev;
	field[NATIVE_CANONICAL_DRIVER_DYN_TURN_ANGLE_LERP_TARGET]=driver->turnAngleLerpTarget;
	field[NATIVE_CANONICAL_DRIVER_DYN_TURN_ANGLE_LERP_VEL]=driver->turnAngleLerpVel;
	field[NATIVE_CANONICAL_DRIVER_DYN_TURN_WOBBLE_ANGLE]=driver->turnWobbleAngle;
	field[NATIVE_CANONICAL_DRIVER_DYN_TURN_WOBBLE_VELOCITY]=driver->turnWobbleVelocity;
	field[NATIVE_CANONICAL_DRIVER_DYN_TURN_WOBBLE_TIMER]=driver->turnWobbleTimer;
	field[NATIVE_CANONICAL_DRIVER_DYN_MULT_DRIFT]=driver->multDrift;
	field[NATIVE_CANONICAL_DRIVER_DYN_TURBO_METER_ROOM_LEFT]=driver->turbo_MeterRoomLeft;
	field[NATIVE_CANONICAL_DRIVER_DYN_TURBO_OUTSIDE_TIMER]=driver->turbo_outsideTimer;
	field[NATIVE_CANONICAL_DRIVER_DYN_RESERVES]=driver->reserves;
	field[NATIVE_CANONICAL_DRIVER_DYN_FIRE_SPEED_CAP]=driver->fireSpeedCap;
	field[NATIVE_CANONICAL_DRIVER_DYN_NUM_FRAMES_SPENT_STEERING]=driver->numFramesSpentSteering;
	field[NATIVE_CANONICAL_DRIVER_DYN_FORWARD_DIR]=driver->forwardDir;
	field[NATIVE_CANONICAL_DRIVER_DYN_PREVIOUS_FRAME_MULT_DRIFT]=driver->previousFrameMultDrift;
	field[NATIVE_CANONICAL_DRIVER_DYN_TIME_UNTIL_DRIFT_SPINOUT]=driver->timeUntilDriftSpinout;
	field[NATIVE_CANONICAL_DRIVER_DYN_DISTANCE_FROM_GROUND]=driver->distanceFromGround;
	field[NATIVE_CANONICAL_DRIVER_DYN_JUMP_TEN_BUFFER]=driver->jump_TenBuffer;
	field[NATIVE_CANONICAL_DRIVER_DYN_JUMP_COOLDOWN_MS]=driver->jump_CooldownMS;
	field[NATIVE_CANONICAL_DRIVER_DYN_JUMP_COYOTE_TIMER_MS]=driver->jump_CoyoteTimerMS;
	field[NATIVE_CANONICAL_DRIVER_DYN_JUMP_FORCED_MS]=driver->jump_ForcedMS;
	field[NATIVE_CANONICAL_DRIVER_DYN_JUMP_INITIAL_VEL_Y]=driver->jump_InitialVelY;
	field[NATIVE_CANONICAL_DRIVER_DYN_JUMP_HIGH_JUMP_TIMER_MS]=driver->jump_HighJumpTimerMS;
	field[NATIVE_CANONICAL_DRIVER_DYN_JUMP_LANDING_BOOST]=driver->jump_LandingBoost;
	field[NATIVE_CANONICAL_DRIVER_DYN_WALL_RUB_TIMER]=driver->wallRubTimer;
	field[NATIVE_CANONICAL_DRIVER_DYN_NO_INPUT_TIMER]=driver->NoInputTimer;
	field[NATIVE_CANONICAL_DRIVER_DYN_BURN_TIMER]=driver->burnTimer;
	field[NATIVE_CANONICAL_DRIVER_DYN_SQUISH_TIMER]=driver->squishTimer;
	field[NATIVE_CANONICAL_DRIVER_DYN_VSHIFT_START_GUARD_TIMER]=driver->vShiftStartGuardTimer;
	field[NATIVE_CANONICAL_DRIVER_DYN_VSHIFT_WINDOW_TIMER]=driver->vShiftWindowTimer;
	field[NATIVE_CANONICAL_DRIVER_DYN_VSHIFT_COUNT]=driver->vShiftCount;
	field[NATIVE_CANONICAL_DRIVER_DYN_JUMP_SQUISH_STRETCH]=driver->jumpSquishStretch;
	field[NATIVE_CANONICAL_DRIVER_DYN_JUMP_SQUISH_STRETCH2]=driver->jumpSquishStretch2;
	field[NATIVE_CANONICAL_DRIVER_DYN_TERRAIN_FRICTION_TIMER]=driver->terrainFrictionTimer;
	dynamics->xSpeed=(int32_t)driver->xSpeed;
	dynamics->ySpeed=(int32_t)driver->ySpeed;
	dynamics->zSpeed=(int32_t)driver->zSpeed;
}

int MainCanonicalDrivers_ExtractRosterRaceDynamics(const struct GameTracker *gGT,const struct sData *sourceData,
	struct MainCanonicalDriversRosterRaceDynamicsCandidate *out)
{
	struct MainCanonicalDriversRosterRaceDynamicsCandidate candidate;
	struct MainCanonicalDriversRosterRaceCandidate rosterRace;
	if(!out||!MainCanonicalDrivers_ExtractRosterRace(gGT,sourceData,&rosterRace))return 0;
	candidate.roster=rosterRace.roster;
	memcpy(candidate.race,rosterRace.race,sizeof(candidate.race));
	memset(candidate.dynamics,0,sizeof(candidate.dynamics));
	for(uint8_t slot=0;slot<8;slot++)
	{
		if((candidate.roster.prelude.presenceMask&(UINT32_C(1)<<slot))==0)continue;
		if(!gGT->drivers[slot])return 0;
		MainCanonicalDrivers_CopyDynamics(gGT->drivers[slot],&candidate.dynamics[slot]);
	}
	*out=candidate;
	return 1;
}

static void MainCanonicalDrivers_PutU16LE(uint8_t bytes[20],size_t offset,uint16_t value)
{
	bytes[offset]=(uint8_t)value;
	bytes[offset+1]=(uint8_t)(value>>8);
}
static void MainCanonicalDrivers_PutS16LE(uint8_t bytes[20],size_t offset,int16_t value)
{
	MainCanonicalDrivers_PutU16LE(bytes,offset,(uint16_t)value);
}
static void MainCanonicalDrivers_PutU32LE(uint8_t bytes[20],size_t offset,uint32_t value)
{
	bytes[offset]=(uint8_t)value;
	bytes[offset+1]=(uint8_t)(value>>8);
	bytes[offset+2]=(uint8_t)(value>>16);
	bytes[offset+3]=(uint8_t)(value>>24);
}
static void MainCanonicalDrivers_PutS32LE(uint8_t bytes[20],size_t offset,int32_t value)
{
	MainCanonicalDrivers_PutU32LE(bytes,offset,(uint32_t)value);
}
static int MainCanonicalDrivers_Boolean(uint8_t value) { return value<=1; }

int MainCanonicalDrivers_ExtractDriverActive(const struct Driver *driver,
	uint8_t kind,uint8_t behaviorID,uint8_t kartState,
	struct NativeCanonicalDriverActiveV1 *out)
{
	struct NativeCanonicalDriverActiveV1 candidate;
	uint32_t activeTag;
	if(!driver||!out||driver->kartState!=kartState||
		!NativeCanonicalDriverBehavior_ResolveActualActiveTag(kind,behaviorID,kartState,&activeTag))return 0;
	memset(&candidate,0,sizeof(candidate));
	candidate.unionTag=activeTag;
	/* The source pointers at offset zero of RevEngine/MaskGrab are never
	 * copied or read.  Each selected scalar is placed explicitly in canonical
	 * little-endian branch bytes; inactive storage remains zero. */
	switch(activeTag)
	{
		case NATIVE_CANONICAL_DRIVER_ACTIVE_NONE: break;
		case NATIVE_CANONICAL_DRIVER_ACTIVE_DRIFT:
			MainCanonicalDrivers_PutS16LE(candidate.branchBytes,0,driver->KartStates.Drifting.numFramesDrifting);
			MainCanonicalDrivers_PutS16LE(candidate.branchBytes,2,driver->KartStates.Drifting.driftBoostTimeMS);
			MainCanonicalDrivers_PutS16LE(candidate.branchBytes,4,driver->KartStates.Drifting.driftTotalTimeMS);
			candidate.branchBytes[6]=(uint8_t)driver->KartStates.Drifting.numBoostsAttempted;
			candidate.branchBytes[7]=(uint8_t)driver->KartStates.Drifting.numBoostsSuccess;
			break;
		case NATIVE_CANONICAL_DRIVER_ACTIVE_SPIN:
			MainCanonicalDrivers_PutS16LE(candidate.branchBytes,0,driver->KartStates.Spinning.driftSpinRate);
			MainCanonicalDrivers_PutS16LE(candidate.branchBytes,2,driver->KartStates.Spinning.spinDir);
			break;
		case NATIVE_CANONICAL_DRIVER_ACTIVE_REV_ENGINE:
			if(driver->KartStates.RevEngine.chargeState>REV_ENGINE_CHARGE_ACTIVE||
				(driver->KartStates.RevEngine.lockoutFlags&~REV_ENGINE_LOCKOUT_ALL)!=0||
				!MainCanonicalDrivers_Boolean(driver->KartStates.RevEngine.boolMaskGrab))return 0;
			MainCanonicalDrivers_PutS32LE(candidate.branchBytes,0,(int32_t)driver->KartStates.RevEngine.boostMeter);
			MainCanonicalDrivers_PutS32LE(candidate.branchBytes,4,(int32_t)driver->KartStates.RevEngine.fireLevel);
			MainCanonicalDrivers_PutS16LE(candidate.branchBytes,8,driver->KartStates.RevEngine.overRevTimerMS);
			MainCanonicalDrivers_PutS16LE(candidate.branchBytes,10,driver->KartStates.RevEngine.releaseCooldownTimerMS);
			MainCanonicalDrivers_PutS16LE(candidate.branchBytes,12,driver->KartStates.RevEngine.emptyCooldownTimerMS);
			candidate.branchBytes[14]=driver->KartStates.RevEngine.chargeState;
			candidate.branchBytes[15]=driver->KartStates.RevEngine.lockoutFlags;
			candidate.branchBytes[16]=driver->KartStates.RevEngine.boolMaskGrab;
			break;
		case NATIVE_CANONICAL_DRIVER_ACTIVE_MASK_GRAB:
			if(!MainCanonicalDrivers_Boolean(driver->KartStates.MaskGrab.boolParticlesSpawned)||
				!MainCanonicalDrivers_Boolean(driver->KartStates.MaskGrab.boolStillFalling)||
				!MainCanonicalDrivers_Boolean(driver->KartStates.MaskGrab.boolLiftingPlayer)||
				!MainCanonicalDrivers_Boolean(driver->KartStates.MaskGrab.boolWhistle))return 0;
			MainCanonicalDrivers_PutS16LE(candidate.branchBytes,0,driver->KartStates.MaskGrab.AngleAxis_NormalVec.x);
			MainCanonicalDrivers_PutS16LE(candidate.branchBytes,2,driver->KartStates.MaskGrab.AngleAxis_NormalVec.y);
			MainCanonicalDrivers_PutS16LE(candidate.branchBytes,4,driver->KartStates.MaskGrab.AngleAxis_NormalVec.z);
			MainCanonicalDrivers_PutS16LE(candidate.branchBytes,6,driver->KartStates.MaskGrab.animFrame);
			candidate.branchBytes[8]=driver->KartStates.MaskGrab.boolParticlesSpawned;
			candidate.branchBytes[9]=driver->KartStates.MaskGrab.boolStillFalling;
			candidate.branchBytes[10]=driver->KartStates.MaskGrab.boolLiftingPlayer;
			candidate.branchBytes[11]=driver->KartStates.MaskGrab.boolWhistle;
			break;
		case NATIVE_CANONICAL_DRIVER_ACTIVE_PLANT_EATEN:
			if(!MainCanonicalDrivers_Boolean(driver->KartStates.EatenByPlant.boolInited))return 0;
			candidate.branchBytes[0]=driver->KartStates.EatenByPlant.boolInited;
			break;
		case NATIVE_CANONICAL_DRIVER_ACTIVE_BLASTED:
			if(driver->KartStates.Blasted.boolPlayBackwards!=0&&driver->KartStates.Blasted.boolPlayBackwards!=4)return 0;
			candidate.branchBytes[0]=driver->KartStates.Blasted.boolPlayBackwards;
			break;
		case NATIVE_CANONICAL_DRIVER_ACTIVE_WARP:
			MainCanonicalDrivers_PutS32LE(candidate.branchBytes,0,driver->KartStates.Warp.timer);
			MainCanonicalDrivers_PutS32LE(candidate.branchBytes,4,driver->KartStates.Warp.heightOffset);
			MainCanonicalDrivers_PutS32LE(candidate.branchBytes,8,driver->KartStates.Warp.quadHeight);
			MainCanonicalDrivers_PutS32LE(candidate.branchBytes,12,driver->KartStates.Warp.dustAngle);
			MainCanonicalDrivers_PutS32LE(candidate.branchBytes,16,driver->KartStates.Warp.beamHeight);
			break;
		default:return 0;
	}
	*out=candidate;
	return 1;
}

int MainCanonicalDrivers_ExtractRosterRaceDynamicsActive(const struct GameTracker *gGT,const struct sData *sourceData,
	struct MainCanonicalDriversRosterRaceDynamicsActiveCandidate *out)
{
	struct MainCanonicalDriversRosterRaceDynamicsCandidate rosterRaceDynamics;
	struct MainCanonicalDriversRosterRaceDynamicsActiveCandidate candidate;
	if(!out||!MainCanonicalDrivers_ExtractRosterRaceDynamics(gGT,sourceData,&rosterRaceDynamics))return 0;
	candidate.roster=rosterRaceDynamics.roster;
	memcpy(candidate.race,rosterRaceDynamics.race,sizeof(candidate.race));
	memcpy(candidate.dynamics,rosterRaceDynamics.dynamics,sizeof(candidate.dynamics));
	memset(candidate.active,0,sizeof(candidate.active));
	for(uint8_t slot=0;slot<8;slot++)
	{
		if((candidate.roster.prelude.presenceMask&(UINT32_C(1)<<slot))==0)continue;
		if(!gGT->drivers[slot]||!MainCanonicalDrivers_ExtractDriverActive(gGT->drivers[slot],candidate.roster.kind[slot],
			candidate.roster.behaviorID[slot],gGT->drivers[slot]->kartState,&candidate.active[slot]))return 0;
	}
	*out=candidate;
	return 1;
}

static int MainCanonicalDrivers_ExtractPendingDamage(const struct GameTracker *gGT,const struct Driver *victim,
	struct NativeCanonicalDriverPendingDamageV1 *out)
{
	struct NativeCanonicalDriverPendingDamageV1 candidate={0};
	uint8_t attacker;
	if(!gGT||!victim||!out)return 0;
	/* Type zero deliberately ignores stale attacker/reason bytes. */
	if(victim->pendingDamageType==0){*out=candidate;return 1;}
	if(!((victim->pendingDamageType==2&&(victim->pendingDamageReasonByte==0||victim->pendingDamageReasonByte==6))||
		(victim->pendingDamageType==3&&victim->pendingDamageReasonByte==5)))return 0;
	for(attacker=0;attacker<8;attacker++)if(gGT->drivers[attacker]==victim->pendingDamageAttacker)break;
	if(attacker==8||gGT->drivers[attacker]==NULL||gGT->drivers[attacker]==victim)return 0;
	candidate.type=victim->pendingDamageType;
	candidate.attackerSlotPlusOne=(uint8_t)(attacker+1);
	candidate.reason=victim->pendingDamageReasonByte;
	*out=candidate;
	return 1;
}
int MainCanonicalDrivers_ExtractRosterRaceDynamicsActivePending(const struct GameTracker *gGT,const struct sData *sourceData,
	struct MainCanonicalDriversRosterRaceDynamicsActivePendingCandidate *out)
{
	struct MainCanonicalDriversRosterRaceDynamicsActiveCandidate prior;
	struct MainCanonicalDriversRosterRaceDynamicsActivePendingCandidate candidate;
	if(!out||!MainCanonicalDrivers_ExtractRosterRaceDynamicsActive(gGT,sourceData,&prior))return 0;
	candidate.roster=prior.roster;memcpy(candidate.race,prior.race,sizeof(candidate.race));
	memcpy(candidate.dynamics,prior.dynamics,sizeof(candidate.dynamics));memcpy(candidate.active,prior.active,sizeof(candidate.active));
	memset(candidate.pendingDamage,0,sizeof(candidate.pendingDamage));
	for(uint8_t slot=0;slot<8;slot++)if((candidate.roster.prelude.presenceMask&(UINT32_C(1)<<slot))!=0&&
		(!gGT->drivers[slot]||!MainCanonicalDrivers_ExtractPendingDamage(gGT,gGT->drivers[slot],&candidate.pendingDamage[slot])))return 0;
	*out=candidate;return 1;
}

static int MainCanonicalDrivers_BotNavIndex(const struct GameTracker *gGT,const struct sData *sourceData,
	const struct Driver *driver,uint16_t *out)
{
	const struct NavHeader *header;const struct NavFrame *base;uintptr_t baseAddress,endAddress,frameAddress;size_t span;int path;
	if(!gGT||!sourceData||!driver||!out||(path=driver->botData.botPath)<0||path>2||!gGT->level1||!gGT->level1->LevNavTable)return 0;
	header=sourceData->NavPath_ptrHeader[path];
	if(!header||header!=gGT->level1->LevNavTable[path]||header->magicNumber!=-0x1303||header->numPoints<=1||header->numPoints>32766)return 0;
	baseAddress=(uintptr_t)header+sizeof(struct NavHeader);if(baseAddress<(uintptr_t)header)return 0;
	base=sourceData->NavPath_ptrNavFrameArray[path];if(!base||baseAddress!=(uintptr_t)base)return 0;
	if((size_t)header->numPoints>SIZE_MAX/sizeof(struct NavFrame))return 0;span=(size_t)header->numPoints*sizeof(struct NavFrame);if(UINTPTR_MAX-baseAddress<span)return 0;endAddress=baseAddress+(uintptr_t)span;
	if((uintptr_t)header->last!=endAddress||!driver->botData.botNavFrame)return 0;frameAddress=(uintptr_t)driver->botData.botNavFrame;
	if(frameAddress<baseAddress||frameAddress>=endAddress||(frameAddress-baseAddress)%sizeof(struct NavFrame)!=0)return 0;
	*out=(uint16_t)((frameAddress-baseAddress)/sizeof(struct NavFrame));return 1;
}

static int MainCanonicalDrivers_BotMaskPresent(const struct GameTracker *gGT,const struct Driver *driver,uint8_t threadBehaviorID,uint8_t *out)
{
	struct MainCanonicalDriversMetaFlags flags;
	const struct MaskHeadWeapon *maskObject;
	if(!gGT||!driver||!out)return 0;
	maskObject=driver->botData.maskObj;
	if(maskObject&&(threadBehaviorID!=3||driver->kartState!=KS_MASK_GRABBED))return 0;
	/* Null does not mean the child graph is safe to skip: all Bot extraction
	 * uses the same complete immediate-child ownership walk as Meta. */
	if(!MainCanonicalDrivers_ResolveAttachmentFlags(gGT,driver,NULL,maskObject,0,&flags))return 0;
	if(maskObject==NULL)
	{
		if((flags.externalPresenceFlags&NATIVE_CANONICAL_DRIVER_EXTERNAL_ACTIVE_MASK_GRAB_OBJECT)!=0)return 0;
		*out=0;return 1;
	}
	*out=1;return 1;
}

static int MainCanonicalDrivers_ExtractBot(const struct GameTracker *gGT,const struct sData *sourceData,const struct Driver *driver,uint8_t kind,uint8_t threadBehaviorID,struct NativeCanonicalDriverBotV1 *out)
{
	struct NativeCanonicalDriverBotV1 c={0};uint16_t index;uint8_t mask;uint32_t flags;
	if(!gGT||!sourceData||!driver||!out)return 0;if(kind==NATIVE_CANONICAL_DRIVER_KIND_HUMAN){*out=c;return 1;}if(kind!=NATIVE_CANONICAL_DRIVER_KIND_BOT)return 0;
	/* Reject all cheap scalar/callback-state inconsistencies before following
	 * nav or attachment pointers. */
	flags=driver->botData.botFlags;
	if((flags&~NATIVE_CANONICAL_DRIVER_BOT_FLAGS_KNOWN_MASK)!=0||driver->botData.aiDamageState<0||driver->botData.aiDamageState==4||driver->botData.aiDamageState>5||
		((flags&NATIVE_CANONICAL_DRIVER_BOT_FLAG_DAMAGE_ACTIVE)!=0&&driver->botData.aiDamageState==0)||
		((flags&NATIVE_CANONICAL_DRIVER_BOT_FLAG_DAMAGE_SUPPRESS)!=0&&(flags&NATIVE_CANONICAL_DRIVER_BOT_FLAG_DAMAGE_ACTIVE)==0)||driver->botData.desiredPath_BossOnly>2||
		(threadBehaviorID==3&&(driver->kartState!=KS_MASK_GRABBED||(flags&(NATIVE_CANONICAL_DRIVER_BOT_FLAG_DAMAGE_ACTIVE|NATIVE_CANONICAL_DRIVER_BOT_FLAG_DAMAGE_SUPPRESS))!=0))||
		(driver->botData.maskObj!=NULL&&threadBehaviorID!=3))return 0;
	if(!MainCanonicalDrivers_BotNavIndex(gGT,sourceData,driver,&index)||!MainCanonicalDrivers_BotMaskPresent(gGT,driver,threadBehaviorID,&mask))return 0;
	c.botPath=driver->botData.botPath;c.botNavFrameIndex=index;c.navProgressRemainder=(int32_t)driver->botData.navProgressRemainder;c.botFlags=flags;c.botAccel=(int32_t)driver->botData.botAccel;c.aiDamageState=driver->botData.aiDamageState;
	c.rotXZ=driver->botData.aiPhysics.rotXZ;c.driftTarget=driver->botData.aiPhysics.driftTarget;c.mulDrift=driver->botData.aiPhysics.mulDrift;c.simpTurnState=driver->botData.aiPhysics.simpTurnState;c.turboMeter=driver->botData.aiPhysics.turboMeter;c.fireLevel=driver->botData.aiPhysics.fireLevel;c.squishCooldown=(int32_t)driver->botData.aiPhysics.squishCooldown;c.speedY=(int32_t)driver->botData.aiPhysics.speedY;c.speedLinear=(int32_t)driver->botData.aiPhysics.speedLinear;
	c.accel[0]=(int32_t)driver->botData.aiPhysics.accel.x;c.accel[1]=(int32_t)driver->botData.aiPhysics.accel.y;c.accel[2]=(int32_t)driver->botData.aiPhysics.accel.z;c.velocity[0]=(int32_t)driver->botData.aiPhysics.velocity.x;c.velocity[1]=(int32_t)driver->botData.aiPhysics.velocity.y;c.velocity[2]=(int32_t)driver->botData.aiPhysics.velocity.z;c.positionBackup[0]=(int32_t)driver->botData.positionBackup.x;c.positionBackup[1]=(int32_t)driver->botData.positionBackup.y;c.positionBackup[2]=(int32_t)driver->botData.positionBackup.z;c.aiRot[0]=driver->botData.aiRot.x;c.aiRot[1]=driver->botData.aiRot.y;c.aiRot[2]=driver->botData.aiRot.z;
	c.estimatePos[0]=driver->botData.estimateNavFrame.pos.x;c.estimatePos[1]=driver->botData.estimateNavFrame.pos.y;c.estimatePos[2]=driver->botData.estimateNavFrame.pos.z;for(uint8_t n=0;n<4;n++)c.estimateRot[n]=driver->botData.estimateNavFrame.rot[n];c.aiProgressCooldown=(int32_t)driver->botData.ai_progress_cooldown;c.aiRotY=driver->botData.ai_rotY_608;c.aiQuadblockCheckpointIndex=driver->botData.ai_quadblock_checkpointIndex;c.estimateDistXYZ=driver->botData.estimateNavFrame.distToNextNavXYZ;c.estimateDistXZ=driver->botData.estimateNavFrame.distToNextNavXZ;c.estimateFlags=driver->botData.estimateNavFrame.flags;c.estimatePathChangeOpcode=driver->botData.estimateNavFrame.pathChangeOpcode;c.estimateGoBackCount=driver->botData.estimateNavFrame.goBackCount;c.estimateSpecialBits=driver->botData.estimateNavFrame.specialBits;c.maskObjPresent=mask;c.weaponCooldown=driver->botData.weaponCooldown;c.blastBounceCount=driver->botData.blastBounceCount;c.desiredPathBossOnly=driver->botData.desiredPath_BossOnly;*out=c;return 1;
}

static int MainCanonicalDrivers_ValidMetaScalars(const struct Driver *driver,uint8_t slot)
{
	int characterID;
	if(!driver||slot>=8)return 0;
	characterID=data.characterIDs[slot];
	return characterID>=0&&characterID<=15&&driver->boolFirstFrameSinceRevEngine<=1&&
		((int)driver->heldItemID<=13||driver->heldItemID==15||driver->heldItemID==16)&&
		driver->currentTerrain<=20&&driver->forcedJumpType<=2&&(int8_t)driver->revEngineState>=0&&
		(int8_t)driver->revEngineState<=2&&((uint16_t)driver->collisionFlags&~UINT16_C(0xf))==0&&
		(int)driver->rainCloudEffect>=0&&(int)driver->rainCloudEffect<=6;
}

static int MainCanonicalDrivers_SelectAttachmentFacts(const struct Driver *driver,uint8_t kind,
	uint8_t behaviorID,uint8_t kartState,const struct Thread **cloudOut,
	const struct MaskHeadWeapon **maskOut,int *emitMaskOut)
{
	uint32_t tag;
	if(!driver||!cloudOut||!maskOut||!emitMaskOut||driver->kartState!=kartState)return 0;
	*cloudOut=driver->thCloud;*maskOut=NULL;*emitMaskOut=0;
	if(kind==NATIVE_CANONICAL_DRIVER_KIND_BOT)
	{
		*maskOut=driver->botData.maskObj;
		return 1;
	}
	if(kind!=NATIVE_CANONICAL_DRIVER_KIND_HUMAN||
		!NativeCanonicalDriverBehavior_ResolveActualActiveTag(kind,behaviorID,kartState,&tag))return 0;
	if(tag==NATIVE_CANONICAL_DRIVER_ACTIVE_MASK_GRAB)
	{
		*maskOut=driver->KartStates.MaskGrab.maskObj;*emitMaskOut=1;
	}
	else if(tag==NATIVE_CANONICAL_DRIVER_ACTIVE_REV_ENGINE)
	{
		if(driver->KartStates.RevEngine.boolMaskGrab>1)return 0;
		if(driver->KartStates.RevEngine.boolMaskGrab)
		{
			if(!driver->KartStates.RevEngine.maskObj)return 0;
			*maskOut=driver->KartStates.RevEngine.maskObj;
		}
	}
	return 1;
}

static int MainCanonicalDrivers_ExtractMetaAndBot(const struct GameTracker *gGT,const struct sData *sourceData,
	const struct Driver *driver,uint8_t slot,const struct NativeCanonicalDriversRosterCandidate *roster,
	struct NativeCanonicalDriverMetaV1 *metaOut,struct NativeCanonicalDriverBotV1 *botOut)
{
	struct NativeCanonicalDriverMetaV1 meta={0};struct NativeCanonicalDriverBotV1 bot={0};
	struct MainCanonicalDriversMetaFlags facts;const struct Thread *cloud;const struct MaskHeadWeapon *mask;
	uint8_t kind,threadBehaviorID;int emitMask;uint16_t navIndex;
	if(!gGT||!sourceData||!driver||!roster||!metaOut||!botOut||slot>=8||!MainCanonicalDrivers_ValidMetaScalars(driver,slot))return 0;
	kind=roster->kind[slot];threadBehaviorID=roster->threadBehaviorID[slot];
	/* Bot pointer-state gates are intentionally checked before traversal. */
	if(kind==NATIVE_CANONICAL_DRIVER_KIND_BOT&&
		((driver->botData.botFlags&~NATIVE_CANONICAL_DRIVER_BOT_FLAGS_KNOWN_MASK)!=0||driver->botData.aiDamageState<0||driver->botData.aiDamageState==4||driver->botData.aiDamageState>5||
		 ((driver->botData.botFlags&NATIVE_CANONICAL_DRIVER_BOT_FLAG_DAMAGE_ACTIVE)!=0&&driver->botData.aiDamageState==0)||
		 ((driver->botData.botFlags&NATIVE_CANONICAL_DRIVER_BOT_FLAG_DAMAGE_SUPPRESS)!=0&&(driver->botData.botFlags&NATIVE_CANONICAL_DRIVER_BOT_FLAG_DAMAGE_ACTIVE)==0)||driver->botData.desiredPath_BossOnly>2||
		 (threadBehaviorID==3&&(driver->kartState!=KS_MASK_GRABBED||(driver->botData.botFlags&(NATIVE_CANONICAL_DRIVER_BOT_FLAG_DAMAGE_ACTIVE|NATIVE_CANONICAL_DRIVER_BOT_FLAG_DAMAGE_SUPPRESS))!=0))||
		 (driver->botData.maskObj!=NULL&&threadBehaviorID!=3)))return 0;
	if(!MainCanonicalDrivers_SelectAttachmentFacts(driver,kind,roster->behaviorID[slot],driver->kartState,&cloud,&mask,&emitMask)||
		!MainCanonicalDrivers_ResolveAttachmentFlags(gGT,driver,cloud,mask,emitMask,&facts))return 0;
	meta.present=1;meta.slotIndex=slot;meta.driverID=slot;meta.characterID=(uint8_t)data.characterIDs[slot];meta.driverKind=kind;
	meta.behaviorID=roster->behaviorID[slot];meta.threadBehaviorID=threadBehaviorID;meta.kartState=driver->kartState;
	meta.actionsFlagSet=(uint32_t)driver->actionsFlagSet&~UINT32_C(0x04000000);meta.actionsFlagSetPrevFrame=(uint32_t)driver->actionsFlagSetPrevFrame&~UINT32_C(0x04000000);
	meta.heldItemID=(uint8_t)driver->heldItemID;meta.numHeldItems=driver->numHeldItems;meta.numWumpas=(int8_t)driver->numWumpas;meta.numCrystals=(int8_t)driver->numCrystals;meta.numTimeCrates=(int8_t)driver->numTimeCrates;meta.accelConst=(int8_t)driver->accelConst;meta.turnConst=(int8_t)driver->turnConst;meta.turboConst=(int8_t)driver->turboConst;meta.lapIndex=driver->lapIndex;meta.simpTurnState=(int8_t)driver->simpTurnState;meta.currentTerrain=driver->currentTerrain;meta.forcedJumpType=(uint8_t)driver->forcedJumpType;meta.normalVecID=(int8_t)driver->normalVecID;meta.boolFirstFrameSinceRevEngine=driver->boolFirstFrameSinceRevEngine;meta.clockSend=driver->clockSend;meta.revEngineState=(int8_t)driver->revEngineState;meta.externalPresenceFlags=facts.externalPresenceFlags;meta.driverThreadSimFlags=facts.driverThreadSimFlags;meta.collisionFlags=(int16_t)driver->collisionFlags;meta.rainCloudEffect=(int16_t)driver->rainCloudEffect;
	if(kind==NATIVE_CANONICAL_DRIVER_KIND_BOT)
	{
		if(!MainCanonicalDrivers_BotNavIndex(gGT,sourceData,driver,&navIndex))return 0;
		/* Reuse the proven attachment fact; pointer presence has already passed
		 * the one walk above, so no second child traversal is needed. */
		bot.botPath=driver->botData.botPath;bot.botNavFrameIndex=navIndex;bot.navProgressRemainder=(int32_t)driver->botData.navProgressRemainder;bot.botFlags=driver->botData.botFlags;bot.botAccel=(int32_t)driver->botData.botAccel;bot.aiDamageState=driver->botData.aiDamageState;bot.maskObjPresent=mask?1:0;
		bot.rotXZ=driver->botData.aiPhysics.rotXZ;bot.driftTarget=driver->botData.aiPhysics.driftTarget;bot.mulDrift=driver->botData.aiPhysics.mulDrift;bot.simpTurnState=driver->botData.aiPhysics.simpTurnState;bot.turboMeter=driver->botData.aiPhysics.turboMeter;bot.fireLevel=driver->botData.aiPhysics.fireLevel;bot.squishCooldown=(int32_t)driver->botData.aiPhysics.squishCooldown;bot.speedY=(int32_t)driver->botData.aiPhysics.speedY;bot.speedLinear=(int32_t)driver->botData.aiPhysics.speedLinear;
		bot.accel[0]=(int32_t)driver->botData.aiPhysics.accel.x;bot.accel[1]=(int32_t)driver->botData.aiPhysics.accel.y;bot.accel[2]=(int32_t)driver->botData.aiPhysics.accel.z;bot.velocity[0]=(int32_t)driver->botData.aiPhysics.velocity.x;bot.velocity[1]=(int32_t)driver->botData.aiPhysics.velocity.y;bot.velocity[2]=(int32_t)driver->botData.aiPhysics.velocity.z;bot.positionBackup[0]=(int32_t)driver->botData.positionBackup.x;bot.positionBackup[1]=(int32_t)driver->botData.positionBackup.y;bot.positionBackup[2]=(int32_t)driver->botData.positionBackup.z;bot.aiRot[0]=driver->botData.aiRot.x;bot.aiRot[1]=driver->botData.aiRot.y;bot.aiRot[2]=driver->botData.aiRot.z;
		bot.estimatePos[0]=driver->botData.estimateNavFrame.pos.x;bot.estimatePos[1]=driver->botData.estimateNavFrame.pos.y;bot.estimatePos[2]=driver->botData.estimateNavFrame.pos.z;for(uint8_t n=0;n<4;n++)bot.estimateRot[n]=driver->botData.estimateNavFrame.rot[n];bot.aiProgressCooldown=(int32_t)driver->botData.ai_progress_cooldown;bot.aiRotY=driver->botData.ai_rotY_608;bot.aiQuadblockCheckpointIndex=driver->botData.ai_quadblock_checkpointIndex;bot.estimateDistXYZ=driver->botData.estimateNavFrame.distToNextNavXYZ;bot.estimateDistXZ=driver->botData.estimateNavFrame.distToNextNavXZ;bot.estimateFlags=driver->botData.estimateNavFrame.flags;bot.estimatePathChangeOpcode=driver->botData.estimateNavFrame.pathChangeOpcode;bot.estimateGoBackCount=driver->botData.estimateNavFrame.goBackCount;bot.estimateSpecialBits=driver->botData.estimateNavFrame.specialBits;bot.weaponCooldown=driver->botData.weaponCooldown;bot.blastBounceCount=driver->botData.blastBounceCount;bot.desiredPathBossOnly=driver->botData.desiredPath_BossOnly;
	}
	*metaOut=meta;*botOut=bot;return 1;
}

int MainCanonicalDrivers_ExtractRosterRaceDynamicsActivePendingBot(const struct GameTracker *gGT,const struct sData *sourceData,struct MainCanonicalDriversRosterRaceDynamicsActivePendingBotCandidate *out)
{
	struct MainCanonicalDriversRosterRaceDynamicsActivePendingCandidate prior;struct MainCanonicalDriversRosterRaceDynamicsActivePendingBotCandidate candidate;
	if(!out||!MainCanonicalDrivers_ExtractRosterRaceDynamicsActivePending(gGT,sourceData,&prior))return 0;candidate.roster=prior.roster;memcpy(candidate.race,prior.race,sizeof(candidate.race));memcpy(candidate.dynamics,prior.dynamics,sizeof(candidate.dynamics));memcpy(candidate.active,prior.active,sizeof(candidate.active));memcpy(candidate.pendingDamage,prior.pendingDamage,sizeof(candidate.pendingDamage));memset(candidate.bot,0,sizeof(candidate.bot));
	for(uint8_t slot=0;slot<8;slot++)if((candidate.roster.prelude.presenceMask&(UINT32_C(1)<<slot))!=0&&(!gGT->drivers[slot]||!MainCanonicalDrivers_ExtractBot(gGT,sourceData,gGT->drivers[slot],candidate.roster.kind[slot],candidate.roster.threadBehaviorID[slot],&candidate.bot[slot])))return 0;*out=candidate;return 1;
}

int MainCanonicalDrivers_ExtractRosterRaceDynamicsActivePendingBotMeta(const struct GameTracker *gGT,const struct sData *sourceData,
	struct MainCanonicalDriversRosterRaceDynamicsActivePendingBotMetaCandidate *out)
{
	struct MainCanonicalDriversRosterRaceDynamicsActivePendingCandidate prior;
	struct MainCanonicalDriversRosterRaceDynamicsActivePendingBotMetaCandidate candidate;
	if(!out||!MainCanonicalDrivers_ExtractRosterRaceDynamicsActivePending(gGT,sourceData,&prior))return 0;
	candidate.roster=prior.roster;memcpy(candidate.race,prior.race,sizeof(candidate.race));memcpy(candidate.dynamics,prior.dynamics,sizeof(candidate.dynamics));memcpy(candidate.active,prior.active,sizeof(candidate.active));memcpy(candidate.pendingDamage,prior.pendingDamage,sizeof(candidate.pendingDamage));memset(candidate.bot,0,sizeof(candidate.bot));memset(candidate.meta,0,sizeof(candidate.meta));
	for(uint8_t slot=0;slot<8;slot++)if((candidate.roster.prelude.presenceMask&(UINT32_C(1)<<slot))!=0&&
		(!gGT->drivers[slot]||!MainCanonicalDrivers_ExtractMetaAndBot(gGT,sourceData,gGT->drivers[slot],slot,&candidate.roster,&candidate.meta[slot],&candidate.bot[slot])))return 0;
	*out=candidate;return 1;
}

static int MainCanonicalDrivers_TerrainIndex(const struct Terrain *terrain,uint8_t *out)
{
	uint8_t candidate;
	if(!terrain||!out)return 0;
	for(candidate=0;candidate<=20;candidate++)if(terrain==&data.MetaDataTerrain[candidate]){*out=candidate;return 1;}
	return 0;
}
static void MainCanonicalDrivers_CopyS32Vec(int32_t out[3],const Vec3 *in)
{
	out[0]=(int32_t)in->x;out[1]=(int32_t)in->y;out[2]=(int32_t)in->z;
}
static void MainCanonicalDrivers_CopyS16Vec(int16_t out[3],const SVec3 *in)
{
	out[0]=in->x;out[1]=in->y;out[2]=in->z;
}
static void MainCanonicalDrivers_CopyS16Vec4(int16_t out[4],const SVec3Slot *in)
{
	out[0]=in->x;out[1]=in->y;out[2]=in->z;out[3]=in->w;
}
static int MainCanonicalDrivers_ExtractPhysics(const struct MainCanonicalTopologyContext *context,
	const struct MainCanonicalTopologySnapshot *snapshot,const struct GameTracker *gGT,const struct sData *sourceData,
	const struct Driver *driver,struct NativeCanonicalDriverPhysicsV1 *out)
{
	struct NativeCanonicalDriverPhysicsV1 candidate;
	if(!context||!snapshot||!gGT||!sourceData||!driver||!out)return 0;
	memset(&candidate,0,sizeof(candidate));
	/* Terrain identity is equality-scanned, never dereferenced. */
	if(!MainCanonicalDrivers_TerrainIndex(driver->terrainMeta1,&candidate.terrainMeta1Index)||
		!MainCanonicalDrivers_TerrainIndex(driver->terrainMeta2,&candidate.terrainMeta2Index)||
		!MainCanonicalTopology_NullableQuadBlockIndex(context,snapshot,gGT,sourceData,driver->currBlockTouching,&candidate.currQuadIndex)||
		!MainCanonicalTopology_NullableQuadBlockIndex(context,snapshot,gGT,sourceData,driver->underDriver,&candidate.underDriverQuadIndex)||
		!MainCanonicalTopology_NullableQuadBlockIndex(context,snapshot,gGT,sourceData,driver->lastValid,&candidate.lastValidQuadIndex))return 0;
	candidate.stepFlagSet=(uint32_t)driver->stepFlagSet;
	candidate.quadBlockHeight=(int32_t)driver->quadBlockHeight;
	MainCanonicalDrivers_CopyS32Vec(candidate.velocity,&driver->velocity);
	MainCanonicalDrivers_CopyS32Vec(candidate.originToCenter,&driver->originToCenter);
	MainCanonicalDrivers_CopyS32Vec(candidate.posCurr,&driver->posCurr);
	MainCanonicalDrivers_CopyS32Vec(candidate.posPrev,&driver->posPrev);
	MainCanonicalDrivers_CopyS16Vec(candidate.normalVecUP,&driver->normalVecUP);
	MainCanonicalDrivers_CopyS16Vec(candidate.spsHitPos,&driver->spsHitPos);
	MainCanonicalDrivers_CopyS16Vec(candidate.spsNormalVec,&driver->spsNormalVec);
	MainCanonicalDrivers_CopyS16Vec(candidate.axisAngle1,&driver->AxisAngle1_normalVec);
	MainCanonicalDrivers_CopyS16Vec(candidate.axisAngle2,&driver->AxisAngle2_normalVec);
	MainCanonicalDrivers_CopyS16Vec(candidate.axisAngle3,&driver->AxisAngle3_normalVec);
	MainCanonicalDrivers_CopyS16Vec(candidate.axisAngle4,&driver->AxisAngle4_normalVec);
	MainCanonicalDrivers_CopyS16Vec4(candidate.rotCurr,&driver->rotCurr);
	MainCanonicalDrivers_CopyS16Vec4(candidate.rotPrev,&driver->rotPrev);
	MainCanonicalDrivers_CopyS16Vec(candidate.posWallColl,&driver->posWallColl);
	MainCanonicalDrivers_CopyS16Vec(candidate.forwardAccelVector,&driver->forwardAccelVector);
	MainCanonicalDrivers_CopyS16Vec(candidate.accel,&driver->accel);
	if(!NativeCanonicalDriverPhysicsV1_Validate(&candidate))return 0;
	*out=candidate;return 1;
}
int MainCanonicalDrivers_ExtractRosterRaceDynamicsActivePendingBotMetaPhysics(const struct GameTracker *gGT,const struct sData *sourceData,
	const struct MainCanonicalTopologyContext *topologyContext,const struct MainCanonicalTopologySnapshot *topologySnapshot,
	struct MainCanonicalDriversRosterRaceDynamicsActivePendingBotMetaPhysicsCandidate *out)
{
	struct MainCanonicalDriversRosterRaceDynamicsActivePendingBotMetaCandidate prior;
	struct MainCanonicalDriversRosterRaceDynamicsActivePendingBotMetaPhysicsCandidate candidate;
	if(!out||!MainCanonicalDrivers_ExtractRosterRaceDynamicsActivePendingBotMeta(gGT,sourceData,&prior))return 0;
	memset(&candidate,0,sizeof(candidate));candidate.roster=prior.roster;
	memcpy(candidate.race,prior.race,sizeof(candidate.race));memcpy(candidate.dynamics,prior.dynamics,sizeof(candidate.dynamics));
	memcpy(candidate.active,prior.active,sizeof(candidate.active));memcpy(candidate.pendingDamage,prior.pendingDamage,sizeof(candidate.pendingDamage));
	memcpy(candidate.bot,prior.bot,sizeof(candidate.bot));memcpy(candidate.meta,prior.meta,sizeof(candidate.meta));
	if(candidate.roster.prelude.presenceMask!=0&&!MainCanonicalTopology_Validate(topologyContext,topologySnapshot,gGT,sourceData))return 0;
	for(uint8_t slot=0;slot<8;slot++)if((candidate.roster.prelude.presenceMask&(UINT32_C(1)<<slot))!=0&&
		(!gGT->drivers[slot]||!MainCanonicalDrivers_ExtractPhysics(topologyContext,topologySnapshot,gGT,sourceData,gGT->drivers[slot],&candidate.physics[slot])))return 0;
	*out=candidate;return 1;
}
