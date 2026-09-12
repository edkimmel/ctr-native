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
int main(void)
{
	struct NativeCanonicalDriverBehaviorRegistry registry;uint8_t init[11],suffix[17][12],other=0;const void *table[13];struct CallbackContext context={{0},{0}};uint8_t out,before;
	Registry(&registry,init,suffix);CHECK(NativeCanonicalDriverBehaviorRegistry_Validate(&registry));
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
	puts("native_canonical_driver_behavior_test: passed");return 0;
}
