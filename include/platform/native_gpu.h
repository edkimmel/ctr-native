/*
 * Derived from REDRIVER2/PsyCross MIT source:
 * externals/PsyCross/src/gpu/PsyX_GPU.h
 * See THIRD_PARTY_NOTICES.md for copyright and license details.
 */

#ifndef NATIVE_GPU_H
#define NATIVE_GPU_H

#include <macros.h>
#include <psx/libgte.h>
#include <psx/libgpu.h>
#include <platform/native_presentation_registry.h>

extern DISPENV activeDispEnv;
extern DRAWENV activeDrawEnv;
extern int g_GPUDisabledState;

int NativeGpu_HasPendingSplits(void);
void ClearSplits(void);
void DrawAllSplits(void);
void ParsePrimitivesLinkedList(u32 *p, int singlePrimitive);
int NativeGpu_GetStateSize(void);
int NativeGpu_CaptureState(void *dst, int dstSize);
int NativeGpu_RestoreState(const void *src, int srcSize);

/* Cabinet-local presentation overrides are configured only after Platform_Init
 * creates the GL context. A failed preload/upload leaves the retail renderer
 * active. These APIs do not touch replay or canonical state. */
int NativeGpu_ConfigurePresentationOverrides(const struct NativePresentationRegistry *registry);
void NativeGpu_ShutdownPresentationOverrides(void);

/* Retail-shaped VRAM operations notify the local replacement safety gate. */
void NativeGpu_NotifyPresentationVramWrite(int x, int y, int width, int height);
void NativeGpu_NotifyPresentationVramCopy(int sourceX, int sourceY, int width, int height,
	int destinationX, int destinationY);
void NativeGpu_NotifyPresentationVramReadback(int x, int y, int width, int height);

#endif
