#ifndef PLATFORM_NATIVE_HOST_TEXTURE_ASSET_H
#define PLATFORM_NATIVE_HOST_TEXTURE_ASSET_H

/*
 * Bounded, local-only raw RGBA asset loader for a future host-texture upload
 * path.  It has no renderer, GL, replay, checkpoint, match, or simulation
 * dependency.  Nothing loads an asset until a later local presentation
 * caller explicitly invokes this module.
 *
 * A registry entry's validated asset path is expected to name a CTRH file.
 * The caller resolves that path below the registry's local pack root before
 * calling LoadFile; this module deliberately does not parse manifests or
 * grant an asset any eligibility to replace a retail texture.
 *
 * CTRH version 1 is exactly:
 *   byte  0..3: ASCII magic "CTRH"
 *   byte  4..7: little-endian u32 version (1)
 *   byte  8..11: little-endian u32 width in pixels
 *   byte 12..15: little-endian u32 height in pixels
 *   byte 16.. : width * height pixels, four bytes each in R,G,B,A order
 *
 * No padding, metadata, compression, palette, mipmaps, or trailing bytes
 * are allowed.  A malformed file fails closed and leaves `asset` empty.
 */

#include <stddef.h>
#include <stdint.h>

#define NATIVE_HOST_TEXTURE_ASSET_HEADER_SIZE 16u
#define NATIVE_HOST_TEXTURE_ASSET_VERSION 1u
#define NATIVE_HOST_TEXTURE_ASSET_MAX_WIDTH 4096u
#define NATIVE_HOST_TEXTURE_ASSET_MAX_HEIGHT 4096u
#define NATIVE_HOST_TEXTURE_ASSET_MAX_RGBA_BYTES (64u * 1024u * 1024u)

enum NativeHostTextureAssetError
{
	NATIVE_HOST_TEXTURE_ASSET_ERROR_NONE = 0,
	NATIVE_HOST_TEXTURE_ASSET_ERROR_ARGUMENT,
	NATIVE_HOST_TEXTURE_ASSET_ERROR_OPEN,
	NATIVE_HOST_TEXTURE_ASSET_ERROR_SEEK,
	NATIVE_HOST_TEXTURE_ASSET_ERROR_READ,
	NATIVE_HOST_TEXTURE_ASSET_ERROR_MAGIC,
	NATIVE_HOST_TEXTURE_ASSET_ERROR_VERSION,
	NATIVE_HOST_TEXTURE_ASSET_ERROR_DIMENSIONS,
	NATIVE_HOST_TEXTURE_ASSET_ERROR_SIZE,
	NATIVE_HOST_TEXTURE_ASSET_ERROR_ALLOCATION,
};

struct NativeHostTextureAsset
{
	uint32_t width;
	uint32_t height;
	size_t rgbaByteCount;
	uint8_t *rgbaBytes;
	enum NativeHostTextureAssetError lastError;
};

void NativeHostTextureAsset_Init(struct NativeHostTextureAsset *asset);
void NativeHostTextureAsset_Free(struct NativeHostTextureAsset *asset);

/*
 * Loads one complete CTRH v1 file from an already-authorized local path.
 * This does not perform manifest lookup or path authorization.  On failure it
 * returns zero, frees any old pixels, and records a stable error code.
 */
int NativeHostTextureAsset_LoadFile(struct NativeHostTextureAsset *asset, const char *path);

const char *NativeHostTextureAsset_ErrorString(enum NativeHostTextureAssetError error);

#endif
