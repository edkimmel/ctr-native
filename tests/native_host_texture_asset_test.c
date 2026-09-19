#include <platform/native_host_texture_asset.h>

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #expression); return 1; } } while (0)

static const char *const s_path = "native_host_texture_asset_test.ctrh";

static void WriteU32LE(FILE *file, unsigned int value)
{
	unsigned char bytes[4];
	bytes[0] = (unsigned char)value;
	bytes[1] = (unsigned char)(value >> 8);
	bytes[2] = (unsigned char)(value >> 16);
	bytes[3] = (unsigned char)(value >> 24);
	(void)fwrite(bytes, 1u, sizeof(bytes), file);
}

static int WriteFile(const char magic[4], unsigned int version, unsigned int width,
	unsigned int height, const unsigned char *pixels, size_t pixelBytes, int addTrailingByte)
{
	FILE *file;

#if defined(_WIN32)
	if (fopen_s(&file, s_path, "wb") != 0)
		file = NULL;
#else
	file = fopen(s_path, "wb");
#endif
	if (file == NULL)
		return 0;
	(void)fwrite(magic, 1u, 4u, file);
	WriteU32LE(file, version);
	WriteU32LE(file, width);
	WriteU32LE(file, height);
	if ((pixelBytes != 0u) && (fwrite(pixels, 1u, pixelBytes, file) != pixelBytes))
	{
		fclose(file);
		return 0;
	}
	if (addTrailingByte != 0)
		(void)fputc(0, file);
	return fclose(file) == 0;
}

static int ExpectLoadFailure(struct NativeHostTextureAsset *asset,
	enum NativeHostTextureAssetError expected)
{
	CHECK(!NativeHostTextureAsset_LoadFile(asset, s_path));
	CHECK(asset->lastError == expected);
	CHECK(asset->rgbaBytes == NULL);
	CHECK(asset->rgbaByteCount == 0u);
	CHECK(asset->width == 0u && asset->height == 0u);
	return 0;
}

int main(void)
{
	static const unsigned char pixels[] = {
		0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
		0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
		0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
	};
	struct NativeHostTextureAsset asset;

	(void)remove(s_path);
	NativeHostTextureAsset_Init(&asset);
	CHECK(WriteFile("CTRH", 1u, 2u, 3u, pixels, sizeof(pixels), 0));
	CHECK(NativeHostTextureAsset_LoadFile(&asset, s_path));
	CHECK(asset.width == 2u && asset.height == 3u);
	CHECK(asset.rgbaByteCount == sizeof(pixels));
	CHECK(memcmp(asset.rgbaBytes, pixels, sizeof(pixels)) == 0);
	CHECK(asset.lastError == NATIVE_HOST_TEXTURE_ASSET_ERROR_NONE);

	CHECK(WriteFile("NOPE", 1u, 2u, 3u, pixels, sizeof(pixels), 0));
	CHECK(ExpectLoadFailure(&asset, NATIVE_HOST_TEXTURE_ASSET_ERROR_MAGIC) == 0);
	CHECK(WriteFile("CTRH", 2u, 2u, 3u, pixels, sizeof(pixels), 0));
	CHECK(ExpectLoadFailure(&asset, NATIVE_HOST_TEXTURE_ASSET_ERROR_VERSION) == 0);
	CHECK(WriteFile("CTRH", 1u, 0u, 3u, pixels, sizeof(pixels), 0));
	CHECK(ExpectLoadFailure(&asset, NATIVE_HOST_TEXTURE_ASSET_ERROR_DIMENSIONS) == 0);
	CHECK(WriteFile("CTRH", 1u, NATIVE_HOST_TEXTURE_ASSET_MAX_WIDTH + 1u, 1u, NULL, 0u, 0));
	CHECK(ExpectLoadFailure(&asset, NATIVE_HOST_TEXTURE_ASSET_ERROR_DIMENSIONS) == 0);
	CHECK(WriteFile("CTRH", 1u, 2u, 3u, pixels, sizeof(pixels) - 1u, 0));
	CHECK(ExpectLoadFailure(&asset, NATIVE_HOST_TEXTURE_ASSET_ERROR_SIZE) == 0);
	CHECK(WriteFile("CTRH", 1u, 2u, 3u, pixels, sizeof(pixels), 1));
	CHECK(ExpectLoadFailure(&asset, NATIVE_HOST_TEXTURE_ASSET_ERROR_SIZE) == 0);
	CHECK(!NativeHostTextureAsset_LoadFile(&asset, NULL));
	CHECK(asset.lastError == NATIVE_HOST_TEXTURE_ASSET_ERROR_ARGUMENT);
	CHECK(strcmp(NativeHostTextureAsset_ErrorString(NATIVE_HOST_TEXTURE_ASSET_ERROR_SIZE),
		"invalid CTRH exact size") == 0);

	NativeHostTextureAsset_Free(&asset);
	(void)remove(s_path);
	puts("native_host_texture_asset_test: PASS");
	return 0;
}
