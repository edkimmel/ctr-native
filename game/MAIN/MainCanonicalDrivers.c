#include "common.h"
#include "MainCanonicalDrivers.h"
#include "functions.h"
#include "platform/native_canonical_pool.h"

#include <limits.h>

CTR_STATIC_ASSERT(NATIVE_CANONICAL_DRIVERS_RACE_BYTES == 60u);
CTR_STATIC_ASSERT(sizeof(struct NativeCanonicalDriverRaceV1) == NATIVE_CANONICAL_DRIVERS_RACE_BYTES);
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
