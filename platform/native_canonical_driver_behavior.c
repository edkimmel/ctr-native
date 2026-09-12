#include "platform/native_canonical_driver_behavior.h"
#include "platform/native_canonical_drivers_detailed.h"

static int Distinct(const void *const *values, uint8_t count)
{
	for(uint8_t i=0;i<count;i++) { if(values[i]==NULL)return 0; for(uint8_t j=0;j<i;j++)if(values[i]==values[j])return 0; }
	return 1;
}
static int TemplateMatches(const void *const left[12], const void *const right[12])
{
	for(uint8_t field=0;field<12;field++)if(left[field]!=right[field])return 0;
	return 1;
}
int NativeCanonicalDriverBehaviorRegistry_Validate(const struct NativeCanonicalDriverBehaviorRegistry *registry)
{
	if(registry==NULL||!Distinct(registry->initTokens,NATIVE_CANONICAL_DRIVER_BEHAVIOR_INIT_COUNT))return 0;
	for(uint8_t suffix=0;suffix<NATIVE_CANONICAL_DRIVER_BEHAVIOR_SUFFIX_COUNT;suffix++)
	{
		for(uint8_t field=0;field<12;field++)if(registry->suffixTemplates[suffix][field]==NULL)return 0;
		for(uint8_t prior=0;prior<suffix;prior++)if(TemplateMatches(registry->suffixTemplates[suffix],registry->suffixTemplates[prior]))return 0;
	}
	return 1;
}
int NativeCanonicalDriverBehavior_Resolve(const struct NativeCanonicalDriverBehaviorRegistry *registry,
	const void *const table[NATIVE_CANONICAL_DRIVER_BEHAVIOR_TABLE_FIELDS], uint8_t *behaviorIDOut)
{
	uint8_t init=UINT8_MAX,suffix=UINT8_MAX;
	if(!NativeCanonicalDriverBehaviorRegistry_Validate(registry)||table==NULL||behaviorIDOut==NULL)return 0;
	for(uint8_t i=0;i<NATIVE_CANONICAL_DRIVER_BEHAVIOR_INIT_COUNT;i++)if(table[0]==registry->initTokens[i]){init=i;break;}
	if(init==UINT8_MAX)return 0;
	for(uint8_t s=0;s<NATIVE_CANONICAL_DRIVER_BEHAVIOR_SUFFIX_COUNT;s++)
	{
		uint8_t field;for(field=0;field<12;field++)if(table[field+1]!=registry->suffixTemplates[s][field])break;
		if(field==12){suffix=s;break;}
	}
	if(suffix==UINT8_MAX)return 0;
	*behaviorIDOut=(uint8_t)(suffix+NATIVE_CANONICAL_DRIVER_BEHAVIOR_SUFFIX_COUNT*init);
	return 1;
}
int NativeCanonicalDriverBehavior_ResolveCallback(const struct NativeCanonicalDriverBehaviorRegistry *registry,
	NativeCanonicalDriverBehaviorTokenCallback callback, void *context, uint8_t *behaviorIDOut)
{
	const void *table[NATIVE_CANONICAL_DRIVER_BEHAVIOR_TABLE_FIELDS];uint8_t candidate;
	if(callback==NULL||behaviorIDOut==NULL)return 0;
	for(uint8_t i=0;i<NATIVE_CANONICAL_DRIVER_BEHAVIOR_TABLE_FIELDS;i++){table[i]=callback(context,i);if(table[i]==NULL)return 0;}
	if(!NativeCanonicalDriverBehavior_Resolve(registry,table,&candidate))return 0;
	*behaviorIDOut=candidate;return 1;
}
int NativeCanonicalDriverBehavior_ValidateKind(uint8_t kind, uint8_t behaviorID, uint8_t threadBehaviorID)
{
	if(behaviorID>NATIVE_CANONICAL_DRIVER_BEHAVIOR_MAX)return 0;
	if(kind==NATIVE_CANONICAL_DRIVER_KIND_HUMAN)return threadBehaviorID==NATIVE_CANONICAL_DRIVER_THREAD_NULL||threadBehaviorID==NATIVE_CANONICAL_DRIVER_THREAD_VEH_BIRTH_NULL;
	if(kind==NATIVE_CANONICAL_DRIVER_KIND_BOT)return threadBehaviorID==NATIVE_CANONICAL_DRIVER_THREAD_BOTS_DRIVE||threadBehaviorID==NATIVE_CANONICAL_DRIVER_THREAD_BOTS_REV_ENGINE;
	return 0;
}
int NativeCanonicalDriverBehavior_ValidateKindCallback(NativeCanonicalDriverBehaviorKindCallback callback, void *context,
	uint8_t slotIndex, uint8_t behaviorID, uint8_t threadBehaviorID)
{
	uint8_t kind;if(callback==NULL||!callback(context,slotIndex,&kind))return 0;return NativeCanonicalDriverBehavior_ValidateKind(kind,behaviorID,threadBehaviorID);
}
