/*
 * Opt-in presentation-only trace for finding safe texture/text replacement
 * candidates.  It observes PSX VRAM reads and writes; it never changes the
 * renderer's source selection, batching, or GPU command ordering.
 */
#ifndef NATIVE_TEXTURE_TRACE_H
#define NATIVE_TEXTURE_TRACE_H

enum NativeTextureTraceHazard
{
	NATIVE_TEXTURE_TRACE_HAZARD_TEXTURE_READ = 1u << 0,
	NATIVE_TEXTURE_TRACE_HAZARD_CLUT_READ = 1u << 1,
	NATIVE_TEXTURE_TRACE_HAZARD_DRAW_PAGE_WRITE = 1u << 2,
	NATIVE_TEXTURE_TRACE_HAZARD_FRAMEBUFFER_OVERLAP = 1u << 3,
	NATIVE_TEXTURE_TRACE_HAZARD_FRAMEBUFFER_FEEDBACK = 1u << 4,
	NATIVE_TEXTURE_TRACE_HAZARD_MOVE_IMAGE = 1u << 5,
	NATIVE_TEXTURE_TRACE_HAZARD_STORE_IMAGE = 1u << 6,
	NATIVE_TEXTURE_TRACE_HAZARD_LOAD_IMAGE = 1u << 7,
	NATIVE_TEXTURE_TRACE_HAZARD_CLEAR_IMAGE = 1u << 8,
	NATIVE_TEXTURE_TRACE_HAZARD_FRAMEBUFFER_STORE = 1u << 9,
};

/*
 * Enable with CTR_NATIVE_TEXTURE_TRACE=1.  CTR_NATIVE_TEXTURE_TRACE_PATH can
 * select an output path; the default is texture-trace.csv in the process CWD.
 */
void NativeTextureTrace_BeginFrame(void);
void NativeTextureTrace_Shutdown(void);
void NativeTextureTrace_TexturePrimitive(int tpage, int clut, int texFormat, int uvMinU, int uvMinV, int uvMaxU, int uvMaxV, int drawX, int drawY,
                                         int drawW, int drawH, int framebufferOverlap);
void NativeTextureTrace_VramCopy(int sourceX, int sourceY, int width, int height, int destinationX, int destinationY, int isMoveImage);
void NativeTextureTrace_VramRead(int sourceX, int sourceY, int width, int height);
void NativeTextureTrace_VramClear(int x, int y, int width, int height);
void NativeTextureTrace_FramebufferStore(int x, int y, int width, int height);
void NativeTextureTrace_FramebufferFeedbackPrepared(int x, int y, int width, int height);

#endif
