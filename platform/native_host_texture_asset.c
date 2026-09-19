#include <platform/native_host_texture_asset.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void NativeHostTextureAsset_Reset(struct NativeHostTextureAsset *asset,
	                                        enum NativeHostTextureAssetError error)
{
	if (asset == NULL)
	{
		return;
	}
	memset(asset, 0, sizeof(*asset));
	asset->lastError = error;
}

static void NativeHostTextureAsset_Clear(struct NativeHostTextureAsset *asset,
	                                        enum NativeHostTextureAssetError error)
{
	if (asset == NULL)
	{
		return;
	}
	free(asset->rgbaBytes);
	NativeHostTextureAsset_Reset(asset, error);
}

void NativeHostTextureAsset_Init(struct NativeHostTextureAsset *asset)
{
	/* Init is valid for uninitialized automatic storage, so it must not free. */
	NativeHostTextureAsset_Reset(asset, NATIVE_HOST_TEXTURE_ASSET_ERROR_NONE);
}

void NativeHostTextureAsset_Free(struct NativeHostTextureAsset *asset)
{
	NativeHostTextureAsset_Clear(asset, NATIVE_HOST_TEXTURE_ASSET_ERROR_NONE);
}

static uint32_t NativeHostTextureAsset_ReadU32LE(const uint8_t *bytes)
{
	return ((uint32_t)bytes[0]) |
	       ((uint32_t)bytes[1] << 8) |
	       ((uint32_t)bytes[2] << 16) |
	       ((uint32_t)bytes[3] << 24);
}

static int NativeHostTextureAsset_ExpectedSize(uint32_t width, uint32_t height,
	                                               size_t *rgbaByteCount, size_t *fileByteCount)
{
	size_t pixelCount;
	size_t rgbaSize;

	if ((rgbaByteCount == NULL) || (fileByteCount == NULL) ||
	    (width == 0u) || (height == 0u) ||
	    (width > NATIVE_HOST_TEXTURE_ASSET_MAX_WIDTH) ||
	    (height > NATIVE_HOST_TEXTURE_ASSET_MAX_HEIGHT))
	{
		return 0;
	}
	if ((size_t)width > (SIZE_MAX / (size_t)height))
	{
		return 0;
	}
	pixelCount = (size_t)width * (size_t)height;
	if (pixelCount > (SIZE_MAX / 4u))
	{
		return 0;
	}
	rgbaSize = pixelCount * 4u;
	if ((rgbaSize > NATIVE_HOST_TEXTURE_ASSET_MAX_RGBA_BYTES) ||
	    (rgbaSize > (SIZE_MAX - NATIVE_HOST_TEXTURE_ASSET_HEADER_SIZE)))
	{
		return 0;
	}
	*rgbaByteCount = rgbaSize;
	*fileByteCount = NATIVE_HOST_TEXTURE_ASSET_HEADER_SIZE + rgbaSize;
	return 1;
}

static int NativeHostTextureAsset_Open(FILE **file, const char *path)
{
#if defined(_WIN32)
	return fopen_s(file, path, "rb") == 0;
#else
	*file = fopen(path, "rb");
	return *file != NULL;
#endif
}

int NativeHostTextureAsset_LoadFile(struct NativeHostTextureAsset *asset, const char *path)
{
	FILE *file = NULL;
	uint8_t header[NATIVE_HOST_TEXTURE_ASSET_HEADER_SIZE];
	uint8_t *pixels = NULL;
	uint32_t width;
	uint32_t height;
	size_t rgbaByteCount;
	size_t expectedFileByteCount;
	long fileLength;

	if (asset == NULL)
	{
		return 0;
	}
	NativeHostTextureAsset_Clear(asset, NATIVE_HOST_TEXTURE_ASSET_ERROR_ARGUMENT);
	if ((path == NULL) || (path[0] == '\0'))
	{
		return 0;
	}
	if (!NativeHostTextureAsset_Open(&file, path) || (file == NULL))
	{
		asset->lastError = NATIVE_HOST_TEXTURE_ASSET_ERROR_OPEN;
		return 0;
	}
	if ((fseek(file, 0, SEEK_END) != 0) || ((fileLength = ftell(file)) < 0) ||
	    (fseek(file, 0, SEEK_SET) != 0))
	{
		fclose(file);
		asset->lastError = NATIVE_HOST_TEXTURE_ASSET_ERROR_SEEK;
		return 0;
	}
	if ((size_t)fileLength < NATIVE_HOST_TEXTURE_ASSET_HEADER_SIZE)
	{
		fclose(file);
		asset->lastError = NATIVE_HOST_TEXTURE_ASSET_ERROR_SIZE;
		return 0;
	}
	if (fread(header, 1u, sizeof(header), file) != sizeof(header))
	{
		fclose(file);
		asset->lastError = NATIVE_HOST_TEXTURE_ASSET_ERROR_READ;
		return 0;
	}
	if (memcmp(header, "CTRH", 4u) != 0)
	{
		fclose(file);
		asset->lastError = NATIVE_HOST_TEXTURE_ASSET_ERROR_MAGIC;
		return 0;
	}
	if (NativeHostTextureAsset_ReadU32LE(header + 4u) != NATIVE_HOST_TEXTURE_ASSET_VERSION)
	{
		fclose(file);
		asset->lastError = NATIVE_HOST_TEXTURE_ASSET_ERROR_VERSION;
		return 0;
	}
	width = NativeHostTextureAsset_ReadU32LE(header + 8u);
	height = NativeHostTextureAsset_ReadU32LE(header + 12u);
	if (!NativeHostTextureAsset_ExpectedSize(width, height, &rgbaByteCount, &expectedFileByteCount))
	{
		fclose(file);
		asset->lastError = NATIVE_HOST_TEXTURE_ASSET_ERROR_DIMENSIONS;
		return 0;
	}
	if ((unsigned long long)fileLength != (unsigned long long)expectedFileByteCount)
	{
		fclose(file);
		asset->lastError = NATIVE_HOST_TEXTURE_ASSET_ERROR_SIZE;
		return 0;
	}
	pixels = (uint8_t *)malloc(rgbaByteCount);
	if (pixels == NULL)
	{
		fclose(file);
		asset->lastError = NATIVE_HOST_TEXTURE_ASSET_ERROR_ALLOCATION;
		return 0;
	}
	if (fread(pixels, 1u, rgbaByteCount, file) != rgbaByteCount)
	{
		free(pixels);
		fclose(file);
		asset->lastError = NATIVE_HOST_TEXTURE_ASSET_ERROR_READ;
		return 0;
	}
	fclose(file);
	asset->width = width;
	asset->height = height;
	asset->rgbaByteCount = rgbaByteCount;
	asset->rgbaBytes = pixels;
	asset->lastError = NATIVE_HOST_TEXTURE_ASSET_ERROR_NONE;
	return 1;
}

const char *NativeHostTextureAsset_ErrorString(enum NativeHostTextureAssetError error)
{
	switch (error)
	{
	case NATIVE_HOST_TEXTURE_ASSET_ERROR_NONE: return "none";
	case NATIVE_HOST_TEXTURE_ASSET_ERROR_ARGUMENT: return "invalid argument";
	case NATIVE_HOST_TEXTURE_ASSET_ERROR_OPEN: return "file open failed";
	case NATIVE_HOST_TEXTURE_ASSET_ERROR_SEEK: return "file seek failed";
	case NATIVE_HOST_TEXTURE_ASSET_ERROR_READ: return "file read failed";
	case NATIVE_HOST_TEXTURE_ASSET_ERROR_MAGIC: return "invalid CTRH magic";
	case NATIVE_HOST_TEXTURE_ASSET_ERROR_VERSION: return "unsupported CTRH version";
	case NATIVE_HOST_TEXTURE_ASSET_ERROR_DIMENSIONS: return "invalid CTRH dimensions";
	case NATIVE_HOST_TEXTURE_ASSET_ERROR_SIZE: return "invalid CTRH exact size";
	case NATIVE_HOST_TEXTURE_ASSET_ERROR_ALLOCATION: return "pixel allocation failed";
	default: return "unknown error";
	}
}
