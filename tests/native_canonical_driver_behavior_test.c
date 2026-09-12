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
/* This is the pre-mask contract, retained independently so the exhaustive
 * rows prove that the set-valued API has not narrowed legacy acceptance. */
static int LegacyValidateState(uint8_t kind,uint8_t behaviorID,uint8_t kartState,uint32_t activeTag)
{
	uint8_t suffix,init,expectedState,expectedTag;
	if(behaviorID>NATIVE_CANONICAL_DRIVER_BEHAVIOR_MAX)return 0;
	if(kind==NATIVE_CANONICAL_DRIVER_KIND_BOT)return activeTag==NATIVE_CANONICAL_DRIVER_ACTIVE_NONE;
	if(kind!=NATIVE_CANONICAL_DRIVER_KIND_HUMAN)return 0;
	suffix=(uint8_t)(behaviorID%NATIVE_CANONICAL_DRIVER_BEHAVIOR_SUFFIX_COUNT);
	init=(uint8_t)(behaviorID/NATIVE_CANONICAL_DRIVER_BEHAVIOR_SUFFIX_COUNT);
	if(suffix==0)return activeTag==NATIVE_CANONICAL_DRIVER_ACTIVE_NONE;
	if((init==6||init==7||init==8)&&kartState==0&&activeTag==NATIVE_CANONICAL_DRIVER_ACTIVE_NONE)return 1;
	if(init==1&&kartState==4&&activeTag==NATIVE_CANONICAL_DRIVER_ACTIVE_NONE)return 1;
	expectedState=0;expectedTag=0;
	switch(suffix)
	{
		case 1: break;
		case 2: expectedState=11;break;
		case 3: expectedState=9;break;
		case 4:case 5: expectedState=2;expectedTag=1;break;
		case 6: expectedState=1;break;
		case 7:case 8:case 9:case 10: expectedState=3;expectedTag=2;break;
		case 11: expectedState=5;expectedTag=4;break;
		case 12:case 13: expectedState=5;expectedTag=5;break;
		case 14: expectedState=4;expectedTag=3;break;
		case 15: expectedState=6;expectedTag=6;break;
		case 16: expectedState=10;expectedTag=7;break;
		default:return 0;
	}
	return kartState==expectedState&&activeTag==expectedTag;
}
static uint32_t LegacyAllowedMask(uint8_t kind,uint8_t behaviorID,uint8_t kartState)
{
	uint32_t mask=0;
	for(uint32_t tag=NATIVE_CANONICAL_DRIVER_ACTIVE_NONE;tag<=NATIVE_CANONICAL_DRIVER_ACTIVE_WARP;tag++)
		if(LegacyValidateState(kind,behaviorID,kartState,tag))mask|=UINT32_C(1)<<tag;
	return mask;
}
static int TestStateRows(void)
{
	for(uint16_t behavior=0;behavior<=NATIVE_CANONICAL_DRIVER_BEHAVIOR_MAX;behavior++)
	{
		for(uint16_t state=0;state<=UINT8_MAX;state++)
		{
			uint32_t expected=LegacyAllowedMask(NATIVE_CANONICAL_DRIVER_KIND_HUMAN,(uint8_t)behavior,(uint8_t)state),mask=UINT32_C(0xa5a5a5a5),resolved=UINT32_C(0xa5a5a5a5);
			CHECK(NativeCanonicalDriverBehavior_AllowedActiveTagMask(NATIVE_CANONICAL_DRIVER_KIND_HUMAN,(uint8_t)behavior,(uint8_t)state,&mask)==(expected!=0));
			if(expected==0)CHECK(mask==UINT32_C(0xa5a5a5a5));else CHECK(mask==expected);
			for(uint32_t tag=NATIVE_CANONICAL_DRIVER_ACTIVE_NONE;tag<=NATIVE_CANONICAL_DRIVER_ACTIVE_WARP;tag++)
				CHECK(NativeCanonicalDriverBehavior_ValidateState(NATIVE_CANONICAL_DRIVER_KIND_HUMAN,(uint8_t)behavior,(uint8_t)state,tag)==((expected&(UINT32_C(1)<<tag))!=0));
			CHECK(!NativeCanonicalDriverBehavior_ValidateState(NATIVE_CANONICAL_DRIVER_KIND_HUMAN,(uint8_t)behavior,(uint8_t)state,UINT32_MAX));
			if(expected!=0&&(expected&(expected-1))==0)
			{
				CHECK(NativeCanonicalDriverBehavior_ResolveActiveTag(NATIVE_CANONICAL_DRIVER_KIND_HUMAN,(uint8_t)behavior,(uint8_t)state,&resolved));
				CHECK((expected&(UINT32_C(1)<<resolved))!=0);
			}
			else CHECK(!NativeCanonicalDriverBehavior_ResolveActiveTag(NATIVE_CANONICAL_DRIVER_KIND_HUMAN,(uint8_t)behavior,(uint8_t)state,&resolved)&&resolved==UINT32_C(0xa5a5a5a5));
		}
	}
	/* Explicit queued-damage and podium coverage, including the genuine
	 * behavior 31/state 4 NONE-or-REV_ENGINE overlap. */
	for(uint8_t init=6;init<=8;init++)for(uint8_t suffix=1;suffix<17;suffix++)
	{
		uint8_t behavior=(uint8_t)(suffix+17*init);uint32_t mask=0;
		CHECK(NativeCanonicalDriverBehavior_AllowedActiveTagMask(NATIVE_CANONICAL_DRIVER_KIND_HUMAN,behavior,0,&mask));
		CHECK(mask==(UINT32_C(1)<<NATIVE_CANONICAL_DRIVER_ACTIVE_NONE));
	}
	for(uint8_t suffix=1;suffix<17;suffix++)
	{
		uint8_t behavior=(uint8_t)(suffix+17);uint32_t expected=LegacyAllowedMask(NATIVE_CANONICAL_DRIVER_KIND_HUMAN,behavior,4),mask=0;
		CHECK(NativeCanonicalDriverBehavior_AllowedActiveTagMask(NATIVE_CANONICAL_DRIVER_KIND_HUMAN,behavior,4,&mask)&&mask==expected);
		CHECK((mask&(UINT32_C(1)<<NATIVE_CANONICAL_DRIVER_ACTIVE_NONE))!=0);
		if(suffix==14)
		{
			uint32_t resolved=UINT32_C(0xa5a5a5a5);
			CHECK(mask==((UINT32_C(1)<<NATIVE_CANONICAL_DRIVER_ACTIVE_NONE)|(UINT32_C(1)<<NATIVE_CANONICAL_DRIVER_ACTIVE_REV_ENGINE)));
			CHECK(NativeCanonicalDriverBehavior_ValidateState(NATIVE_CANONICAL_DRIVER_KIND_HUMAN,behavior,4,NATIVE_CANONICAL_DRIVER_ACTIVE_NONE));
			CHECK(NativeCanonicalDriverBehavior_ValidateState(NATIVE_CANONICAL_DRIVER_KIND_HUMAN,behavior,4,NATIVE_CANONICAL_DRIVER_ACTIVE_REV_ENGINE));
			CHECK(!NativeCanonicalDriverBehavior_ResolveActiveTag(NATIVE_CANONICAL_DRIVER_KIND_HUMAN,behavior,4,&resolved)&&resolved==UINT32_C(0xa5a5a5a5));
		}
		else CHECK(mask==(UINT32_C(1)<<NATIVE_CANONICAL_DRIVER_ACTIVE_NONE));
	}
	for(uint16_t behavior=0;behavior<=NATIVE_CANONICAL_DRIVER_BEHAVIOR_MAX;behavior++)for(uint16_t state=0;state<=UINT8_MAX;state++)
	{
		uint32_t mask=UINT32_MAX,resolved=UINT32_MAX;
		CHECK(NativeCanonicalDriverBehavior_AllowedActiveTagMask(NATIVE_CANONICAL_DRIVER_KIND_BOT,(uint8_t)behavior,(uint8_t)state,&mask)&&mask==(UINT32_C(1)<<NATIVE_CANONICAL_DRIVER_ACTIVE_NONE));
		CHECK(NativeCanonicalDriverBehavior_ResolveActiveTag(NATIVE_CANONICAL_DRIVER_KIND_BOT,(uint8_t)behavior,(uint8_t)state,&resolved)&&resolved==NATIVE_CANONICAL_DRIVER_ACTIVE_NONE);
		for(uint32_t tag=NATIVE_CANONICAL_DRIVER_ACTIVE_NONE;tag<=NATIVE_CANONICAL_DRIVER_ACTIVE_WARP;tag++)
			CHECK(NativeCanonicalDriverBehavior_ValidateState(NATIVE_CANONICAL_DRIVER_KIND_BOT,(uint8_t)behavior,(uint8_t)state,tag)==(tag==NATIVE_CANONICAL_DRIVER_ACTIVE_NONE));
	}
	{uint32_t mask=UINT32_C(0xa5a5a5a5);CHECK(!NativeCanonicalDriverBehavior_AllowedActiveTagMask(0,1,0,&mask)&&mask==UINT32_C(0xa5a5a5a5));}
	{uint32_t mask=UINT32_C(0xa5a5a5a5);CHECK(!NativeCanonicalDriverBehavior_AllowedActiveTagMask(NATIVE_CANONICAL_DRIVER_KIND_HUMAN,187,0,&mask)&&mask==UINT32_C(0xa5a5a5a5));}
	{uint32_t resolved=UINT32_C(0xa5a5a5a5);CHECK(!NativeCanonicalDriverBehavior_ResolveActiveTag(0,1,0,&resolved)&&resolved==UINT32_C(0xa5a5a5a5));}
	{uint32_t resolved=UINT32_C(0xa5a5a5a5);CHECK(!NativeCanonicalDriverBehavior_ResolveActiveTag(NATIVE_CANONICAL_DRIVER_KIND_HUMAN,187,0,&resolved)&&resolved==UINT32_C(0xa5a5a5a5));}
	CHECK(!NativeCanonicalDriverBehavior_AllowedActiveTagMask(NATIVE_CANONICAL_DRIVER_KIND_HUMAN,1,0,NULL));
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
