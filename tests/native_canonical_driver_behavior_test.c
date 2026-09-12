#include "platform/native_canonical_driver_behavior.h"
#include "platform/native_canonical_drivers_detailed.h"

#include <stdio.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression); return 1; } } while (0)

struct CallbackContext { const void *table[13]; uint8_t kinds[8]; };
static const void *TokenCallback(void *context,uint8_t field){return ((struct CallbackContext *)context)->table[field];}
static int KindCallback(void *context,uint8_t slot,uint8_t *kindOut){if(slot>=8)return 0;*kindOut=((struct CallbackContext *)context)->kinds[slot];return 1;}
static void Registry(struct NativeCanonicalDriverBehaviorRegistry *registry,uint8_t init[11],uint8_t suffix[17][12])
{
	for(uint8_t i=0;i<11;i++)registry->initTokens[i]=&init[i];
	for(uint8_t s=0;s<17;s++)for(uint8_t f=0;f<12;f++)registry->suffixTemplates[s][f]=&suffix[s][f];
}
static int TestStateRows(void)
{
	static const struct { uint8_t suffix,state; uint32_t tag; } rows[]={{1,0,0},{2,11,0},{3,9,0},{4,2,1},{5,2,1},{6,1,0},{7,3,2},{8,3,2},{9,3,2},{10,3,2},{11,5,4},{12,5,5},{13,5,5},{14,4,3},{15,6,6},{16,10,7}};
	for(size_t i=0;i<sizeof(rows)/sizeof(rows[0]);i++)
	{
		for(uint8_t init=0;init<11;init++)
		{
			uint8_t behavior=(uint8_t)(rows[i].suffix+17*init);uint32_t expected=rows[i].tag,resolved=UINT32_C(0xa5a5a5a5);
			if(init==1&&rows[i].state==4)expected=0;
			CHECK(NativeCanonicalDriverBehavior_ResolveActiveTag(NATIVE_CANONICAL_DRIVER_KIND_HUMAN,behavior,rows[i].state,&resolved));
			CHECK(resolved==expected);
			CHECK(NativeCanonicalDriverBehavior_ValidateState(NATIVE_CANONICAL_DRIVER_KIND_HUMAN,behavior,rows[i].state,expected));
			resolved=UINT32_C(0xa5a5a5a5);
			CHECK(!NativeCanonicalDriverBehavior_ResolveActiveTag(NATIVE_CANONICAL_DRIVER_KIND_HUMAN,behavior,UINT8_MAX,&resolved));
			CHECK(resolved==UINT32_C(0xa5a5a5a5));
			CHECK(!NativeCanonicalDriverBehavior_ValidateState(NATIVE_CANONICAL_DRIVER_KIND_HUMAN,behavior,rows[i].state,expected^1));
		}
	}
	for(uint8_t init=0;init<11;init++){uint32_t resolved=UINT32_MAX;CHECK(NativeCanonicalDriverBehavior_ResolveActiveTag(NATIVE_CANONICAL_DRIVER_KIND_HUMAN,(uint8_t)(17*init),0,&resolved)&&resolved==0);}
	/* Queued damage and podium retain no initialized union.  Repeated freeze
	 * and warp remain their steady suffix/state rows above. */
	for(uint8_t init=6;init<=8;init++){uint32_t resolved=UINT32_MAX;CHECK(NativeCanonicalDriverBehavior_ResolveActiveTag(NATIVE_CANONICAL_DRIVER_KIND_HUMAN,(uint8_t)(1+17*init),0,&resolved)&&resolved==0);}
	{uint32_t resolved=UINT32_MAX;CHECK(NativeCanonicalDriverBehavior_ResolveActiveTag(NATIVE_CANONICAL_DRIVER_KIND_HUMAN,1+17,4,&resolved)&&resolved==0);}
	{uint32_t resolved=UINT32_MAX;CHECK(NativeCanonicalDriverBehavior_ResolveActiveTag(NATIVE_CANONICAL_DRIVER_KIND_HUMAN,2+17*3,11,&resolved)&&resolved==0);}
	{uint32_t resolved=UINT32_MAX;CHECK(NativeCanonicalDriverBehavior_ResolveActiveTag(NATIVE_CANONICAL_DRIVER_KIND_HUMAN,16+17*4,10,&resolved)&&resolved==7);}
	/* Internal post-init setters retain the armed suffix's union contract. */
	{uint32_t resolved=UINT32_MAX;CHECK(NativeCanonicalDriverBehavior_ResolveActiveTag(NATIVE_CANONICAL_DRIVER_KIND_HUMAN,4+17*9,2,&resolved)&&resolved==1);}
	{uint32_t resolved=UINT32_MAX;CHECK(NativeCanonicalDriverBehavior_ResolveActiveTag(NATIVE_CANONICAL_DRIVER_KIND_HUMAN,7+17*10,3,&resolved)&&resolved==2);}
	/* KS_MASK_GRABBED remains suffix-disambiguated. */
	{uint32_t resolved=UINT32_MAX;CHECK(NativeCanonicalDriverBehavior_ResolveActiveTag(NATIVE_CANONICAL_DRIVER_KIND_HUMAN,11,5,&resolved)&&resolved==4);}
	{uint32_t resolved=UINT32_MAX;CHECK(NativeCanonicalDriverBehavior_ResolveActiveTag(NATIVE_CANONICAL_DRIVER_KIND_HUMAN,12,5,&resolved)&&resolved==5);}
	{uint32_t resolved=UINT32_MAX;CHECK(NativeCanonicalDriverBehavior_ResolveActiveTag(NATIVE_CANONICAL_DRIVER_KIND_HUMAN,13,5,&resolved)&&resolved==5);}
	for(uint8_t behavior=0;behavior<=NATIVE_CANONICAL_DRIVER_BEHAVIOR_MAX;behavior++)
	{
		uint32_t resolved=UINT32_MAX;
		CHECK(NativeCanonicalDriverBehavior_ResolveActiveTag(NATIVE_CANONICAL_DRIVER_KIND_BOT,behavior,UINT8_MAX,&resolved)&&resolved==0);
	}
	{uint32_t resolved=UINT32_C(0xa5a5a5a5);CHECK(!NativeCanonicalDriverBehavior_ResolveActiveTag(0,1,0,&resolved)&&resolved==UINT32_C(0xa5a5a5a5));}
	{uint32_t resolved=UINT32_C(0xa5a5a5a5);CHECK(!NativeCanonicalDriverBehavior_ResolveActiveTag(NATIVE_CANONICAL_DRIVER_KIND_HUMAN,187,0,&resolved)&&resolved==UINT32_C(0xa5a5a5a5));}
	CHECK(!NativeCanonicalDriverBehavior_ResolveActiveTag(NATIVE_CANONICAL_DRIVER_KIND_HUMAN,1,0,NULL));
	return 0;
}
int main(void)
{
	struct NativeCanonicalDriverBehaviorRegistry registry;uint8_t init[11],suffix[17][12],other=0;const void *table[13];struct CallbackContext context={{0},{0}};uint8_t out,before;
	Registry(&registry,init,suffix);CHECK(NativeCanonicalDriverBehaviorRegistry_Validate(&registry));
	registry.initTokens[1]=registry.initTokens[0];CHECK(!NativeCanonicalDriverBehaviorRegistry_Validate(&registry));Registry(&registry,init,suffix);
	for(uint8_t f=0;f<12;f++)registry.suffixTemplates[1][f]=registry.suffixTemplates[0][f];CHECK(!NativeCanonicalDriverBehaviorRegistry_Validate(&registry));Registry(&registry,init,suffix);
	registry.suffixTemplates[1][2]=NULL;CHECK(!NativeCanonicalDriverBehaviorRegistry_Validate(&registry));Registry(&registry,init,suffix);
	for(uint8_t i=0;i<11;i++)for(uint8_t s=0;s<17;s++)
	{
		table[0]=registry.initTokens[i];for(uint8_t f=0;f<12;f++)table[f+1]=registry.suffixTemplates[s][f];out=UINT8_MAX;
		CHECK(NativeCanonicalDriverBehavior_Resolve(&registry,table,&out)&&out==(uint8_t)(s+17*i));
		for(uint8_t f=0;f<12;f++){const void *saved=table[f+1];table[f+1]=registry.suffixTemplates[(uint8_t)((s+1)%17)][f];before=out;CHECK(!NativeCanonicalDriverBehavior_Resolve(&registry,table,&out)&&out==before);table[f+1]=saved;}
		before=out;table[1]=&other;CHECK(!NativeCanonicalDriverBehavior_Resolve(&registry,table,&out)&&out==before);table[1]=registry.suffixTemplates[s][0];
	}
	table[0]=registry.initTokens[10];for(uint8_t f=0;f<12;f++)table[f+1]=registry.suffixTemplates[16][f];for(uint8_t f=0;f<13;f++)context.table[f]=table[f];
	CHECK(NativeCanonicalDriverBehavior_ResolveCallback(&registry,TokenCallback,&context,&out)&&out==186);
	context.table[8]=&other;before=out;CHECK(!NativeCanonicalDriverBehavior_ResolveCallback(&registry,TokenCallback,&context,&out)&&out==before);
	CHECK(NativeCanonicalDriverBehavior_ValidateKind(NATIVE_CANONICAL_DRIVER_KIND_HUMAN,186,NATIVE_CANONICAL_DRIVER_THREAD_NULL));
	CHECK(NativeCanonicalDriverBehavior_ValidateKind(NATIVE_CANONICAL_DRIVER_KIND_HUMAN,0,NATIVE_CANONICAL_DRIVER_THREAD_VEH_BIRTH_NULL));
	CHECK(!NativeCanonicalDriverBehavior_ValidateKind(NATIVE_CANONICAL_DRIVER_KIND_HUMAN,0,NATIVE_CANONICAL_DRIVER_THREAD_BOTS_DRIVE));
	CHECK(NativeCanonicalDriverBehavior_ValidateKind(NATIVE_CANONICAL_DRIVER_KIND_BOT,16,NATIVE_CANONICAL_DRIVER_THREAD_BOTS_DRIVE));
	CHECK(NativeCanonicalDriverBehavior_ValidateKind(NATIVE_CANONICAL_DRIVER_KIND_BOT,16,NATIVE_CANONICAL_DRIVER_THREAD_BOTS_REV_ENGINE));
	CHECK(!NativeCanonicalDriverBehavior_ValidateKind(NATIVE_CANONICAL_DRIVER_KIND_BOT,187,NATIVE_CANONICAL_DRIVER_THREAD_BOTS_DRIVE));
	context.kinds[3]=NATIVE_CANONICAL_DRIVER_KIND_BOT;CHECK(NativeCanonicalDriverBehavior_ValidateKindCallback(KindCallback,&context,3,16,NATIVE_CANONICAL_DRIVER_THREAD_BOTS_DRIVE));
	context.kinds[3]=NATIVE_CANONICAL_DRIVER_KIND_HUMAN;CHECK(!NativeCanonicalDriverBehavior_ValidateKindCallback(KindCallback,&context,3,16,NATIVE_CANONICAL_DRIVER_THREAD_BOTS_DRIVE));
	if(TestStateRows()!=0)return 1;
	puts("native_canonical_driver_behavior_test: passed");return 0;
}
