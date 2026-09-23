#include "platform/native_vblank_pacing.h"

#include <stdint.h>

uint32_t NativeVBlankPacing_Plan(int fixedPacing, uint64_t now, uint64_t nextVBlank, uint64_t step, uint32_t catchUpMax)
{
	if (fixedPacing != 0)
	{
		return (now >= nextVBlank) ? (uint32_t)NATIVE_VBLANK_PACING_REANCHOR : (uint32_t)NATIVE_VBLANK_PACING_ON_TIME;
	}
	if (now >= nextVBlank)
	{
		/* The approximate count of VBlanks due, the slot at nextVBlank included. */
		if ((step == 0u) || ((((now - nextVBlank) / step) + 1u) > (uint64_t)catchUpMax))
		{
			return (uint32_t)NATIVE_VBLANK_PACING_REBASE;
		}
	}
	return (uint32_t)NATIVE_VBLANK_PACING_CATCH_UP;
}
