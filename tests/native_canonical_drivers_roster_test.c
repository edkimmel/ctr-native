#include "platform/native_canonical_drivers_roster.h"
#include <stdio.h>
#include <string.h>
#define C(x) do{if(!(x)){fprintf(stderr,"fail %d\n",__LINE__);return 1;}}while(0)
static void Init(struct NativeCanonicalDriversRosterInput*i)
{
	memset(i,0,sizeof(*i));memset(i->raceOrder,0xff,sizeof(i->raceOrder));memset(i->winnerDriverIDs,0xff,sizeof(i->winnerDriverIDs));
	memset(i->ranks,0xff,sizeof(i->ranks));memset(i->navOrder,0xff,sizeof(i->navOrder));
}
static void Slot(struct NativeCanonicalDriversRosterInput*i,uint8_t n,uint8_t kind)
{
	i->slots[n]=(struct NativeCanonicalDriversRosterSlot){1,n,kind,kind==NATIVE_CANONICAL_DRIVER_KIND_BOT?1:0,
		kind==NATIVE_CANONICAL_DRIVER_KIND_BOT?NATIVE_CANONICAL_DRIVER_THREAD_BOTS_DRIVE:NATIVE_CANONICAL_DRIVER_THREAD_NULL};
}
static int FullRoster(void)
{
	struct NativeCanonicalDriversRosterInput i;struct NativeCanonicalDriversRosterCandidate o;
	Init(&i);for(uint8_t n=0;n<8;n++){Slot(&i,n,n<4?NATIVE_CANONICAL_DRIVER_KIND_HUMAN:NATIVE_CANONICAL_DRIVER_KIND_BOT);i.raceOrder[n]=(uint8_t)(7-n);i.navOrder[0][n]=n;if(n<4){i.ranks[n]=(uint8_t)n;i.winnerDriverIDs[n]=n;}}
	i.playerCount=4;i.raceOrderCount=8;i.winnerCount=4;i.navCount[0]=8;
	if(!NativeCanonicalDriversRoster_Normalize(&i,&o)||o.prelude.presenceMask!=0xff||o.prelude.playerCount!=4||o.prelude.activeBotCount!=4||o.prelude.winnerSlots[3]!=3)return 0;
	/* Each malformed foreign/duplicate/tail value must fail without output mutation. */
	{struct NativeCanonicalDriversRosterCandidate before=o;i.winnerDriverIDs[3]=2;if(NativeCanonicalDriversRoster_Normalize(&i,&o)||memcmp(&o,&before,sizeof(o))!=0)return 0;i.winnerDriverIDs[3]=3;i.raceOrder[7]=i.raceOrder[6];if(NativeCanonicalDriversRoster_Normalize(&i,&o)||memcmp(&o,&before,sizeof(o))!=0)return 0;i.raceOrder[7]=0;i.navOrder[1][0]=0;i.navCount[1]=1;if(NativeCanonicalDriversRoster_Normalize(&i,&o)||memcmp(&o,&before,sizeof(o))!=0)return 0;i.navOrder[1][0]=0xff;i.navCount[1]=0;i.winnerDriverIDs[3]=0xff;if(NativeCanonicalDriversRoster_Normalize(&i,&o)||memcmp(&o,&before,sizeof(o))!=0)return 0;}
	return 1;
}
int main(void)
{
	struct NativeCanonicalDriversRosterInput i;struct NativeCanonicalDriversRosterCandidate o,b;
	/* Empty menus are canonical and have no phantom tail values. */
	Init(&i);C(NativeCanonicalDriversRoster_Normalize(&i,&o)&&o.prelude.presenceMask==0&&o.prelude.raceOrderCount==0);
	/* Sparse handles remain slots; ranks are independent rank ordinals. */
	Slot(&i,3,NATIVE_CANONICAL_DRIVER_KIND_HUMAN);i.playerCount=1;i.ranks[0]=7;i.raceOrderCount=1;i.raceOrder[0]=3;i.winnerCount=1;i.winnerDriverIDs[0]=3;
	C(NativeCanonicalDriversRoster_Normalize(&i,&o)&&o.prelude.presenceMask==8&&o.prelude.raceOrder[0]==3&&o.prelude.winnerSlots[0]==3&&o.prelude.humanPlayerPositions[0]==7);
	i.navCount[0]=1;i.navOrder[0][0]=3;C(NativeCanonicalDriversRoster_Normalize(&i,&o));
	i.navOrder[1][0]=3;i.navCount[1]=1;C(!NativeCanonicalDriversRoster_Normalize(&i,&o));i.navOrder[1][0]=0xff;i.navCount[1]=0;
	i.raceOrder[1]=3;C(!NativeCanonicalDriversRoster_Normalize(&i,&o));i.raceOrder[1]=0xff;
	i.winnerDriverIDs[0]=2;C(!NativeCanonicalDriversRoster_Normalize(&i,&o));i.winnerDriverIDs[0]=3;
	Slot(&i,4,NATIVE_CANONICAL_DRIVER_KIND_BOT);i.raceOrderCount=2;i.raceOrder[1]=4;C(NativeCanonicalDriversRoster_Normalize(&i,&o)&&o.prelude.activeBotCount==1);
	i.slots[3].driverID=2;b=o;C(!NativeCanonicalDriversRoster_Normalize(&i,&o)&&memcmp(&o,&b,sizeof(o))==0);
	C(FullRoster());puts("native_canonical_drivers_roster_test: passed");return 0;
}
