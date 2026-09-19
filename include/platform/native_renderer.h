#ifndef NATIVE_RENDERER_H
#define NATIVE_RENDERER_H

#include <platform/native_host_texture_asset.h>
#include <platform/native_renderer_types.h>

/*
 * Renderer-owned texture and UV state for one already-authorized host
 * presentation asset.  sourceU/sourceV and their extents are retail UV
 * texels, not normalized coordinates and not the expanded CTRH dimensions.
 * The dedicated host shader maps them to the uploaded asset at texel centres.
 */
struct NativeRendererHostTextureBinding
{
	TextureID texture;
	unsigned int sourceU;
	unsigned int sourceV;
	unsigned int sourceUExtent;
	unsigned int sourceVExtent;
};

int NativeRenderer_InitialiseRender(char *windowName, int width, int height, int fullscreen);
int NativeRenderer_InitialisePSX(void);
void NativeRenderer_Shutdown(void);
void NativeRenderer_ResetDevice(void);
void NativeRenderer_BeginScene(void);
void NativeRenderer_EndScene(void);
void NativeRenderer_EndGpuFrame(void);
void NativeRenderer_FinishGpuMeasurements(void);
void NativeRenderer_UpdateSwapIntervalState(int swapInterval);
void NativeRenderer_SwapWindow(void);
void NativeRenderer_SetRenderScale(int scale);
int NativeRenderer_GetRenderScale(void);
void NativeRenderer_StoreFrameBuffer(int x, int y, int w, int h);
void NativeRenderer_PresentVRAMDisplay(void);
void NativeRenderer_PresentVRAMRect(int x, int y, int w, int h);
void NativeRenderer_PresentMainRenderTarget(void);
void NativeRenderer_SaveVRAM(const char *outputFileName, int x, int y, int width, int height, int readFromFramebuffer);
void NativeRenderer_Clear(int x, int y, int w, int h, u8 r, u8 g, u8 b);
void NativeRenderer_ClearVRAM(int x, int y, int w, int h, u8 r, u8 g, u8 b);
void NativeRenderer_CopyVRAM(u16 *src, int x, int y, int w, int h, int dstX, int dstY);
void NativeRenderer_ReadVRAM(u16 *dst, int x, int y, int dstW, int dstH);
void NativeRenderer_UpdateVRAM(void);
int NativeRenderer_GetVRAMStateSize(void);
int NativeRenderer_CaptureVRAMState(void *dst, int dstSize);
int NativeRenderer_RestoreVRAMState(const void *src, int srcSize);
TextureID NativeRenderer_GetVRAMTexture(void);
TextureID NativeRenderer_GetWhiteTexture(void);
void NativeRenderer_SetBlendMode(BlendMode blendMode);
void NativeRenderer_SetStencilMode(int drawPrim);
void NativeRenderer_SetOffscreenState(const RECT16 *offscreenRect, int enable);
void NativeRenderer_SetProjection(const RECT16 *drawRect, const DISPENV *displayEnv, int offscreen);
void NativeRenderer_SetupClipMode(const RECT16 *clipRect, const DISPENV *displayEnv, int enable);
void NativeRenderer_SetTexture(TextureID texture, TexFormat texFormat);
/*
 * Uploads a decoder-validated RGBA asset once.  The returned texture is owned
 * by the caller and must be released with NativeRenderer_DestroyHostTexture.
 * This API performs no filesystem access and never changes texture unit 1,
 * which is reserved for the PSX RG lookup table.
 */
int NativeRenderer_UploadHostTexture(const struct NativeHostTextureAsset *asset, TextureID *textureOut);
void NativeRenderer_DestroyHostTexture(TextureID *texture);

/*
 * Selects the dedicated presentation shader and binds one preuploaded host
 * texture for a split.  Returns zero without changing renderer state when the
 * binding is incomplete or the shader is unavailable, allowing a native
 * fallback.  The caller still supplies projection and PSX draw-mask state via
 * the normal renderer APIs.
 */
int NativeRenderer_BindHostTexture(const struct NativeRendererHostTextureBinding *binding);
void NativeRenderer_SetOverrideTextureSize(int width, int height);
void NativeRenderer_SetPSXTextureSemiTransPass(int pass);
void NativeRenderer_SetPSXTextureOutputSTP(int enabled);
void NativeRenderer_SetPSXDrawMaskSet(int maskSet);
void NativeRenderer_UpdateVertexBuffer(const GrVertex *vertices, int count);
void NativeRenderer_DrawTriangles(int startVertex, int triangles);
void NativeRenderer_PushDebugLabel(const char *label);
void NativeRenderer_PopDebugLabel(void);

#endif
