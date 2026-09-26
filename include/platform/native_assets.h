#ifndef NATIVE_ASSETS_H
#define NATIVE_ASSETS_H

#include <stddef.h>
#include <stdio.h>

#include "platform/native_str8.h"

enum NativeAssetReadMode
{
	NATIVE_ASSET_READ_DATA_FILE,
	NATIVE_ASSET_READ_RAW_CD_SECTORS,
};

struct NativeAssetsByteBuffer
{
	u8 *data;
	int size;
};

int NativeAssets_Init(const char *executableBasePath);
/*
 * Explicit data directory (docs/PACKAGING.md PK-6): assetDir names the folder
 * that holds ctr-u.bin or BIGFILE.BIG; a relative assetDir is joined to the
 * exe directory. The base directory stays the exe directory and no parent
 * search runs. Returns 0, leaving the asset paths unchanged, when the folder
 * holds neither file or when assetDir is drive- or root-relative (see
 * below). resolvedAssetDir (optional) receives the resolved path, or "" when
 * it could not be resolved or was rejected.
 */
int NativeAssets_InitWithAssetDir(const char *executableBasePath, const char *assetDir, char *resolvedAssetDir, size_t resolvedAssetDirSize);
/*
 * 1 when path is drive-relative ("C:" or "C:dir") or, on Windows,
 * root-relative ("\dir" or "/dir"; a UNC "\\server\share" is not). Such a
 * data directory would resolve against a current directory that changes when
 * startup enters the base directory, so it is rejected. 0 for NULL.
 */
int NativeAssets_IsDriveOrRootRelativePath(const char *path);
const char *NativeAssets_GetBaseDir(void);
const char *NativeAssets_GetAssetDir(void);
int NativeAssets_BuildPathStr8(NativeStr8 relativePath, char *dst, size_t dstSize);
int NativeAssets_BuildPath(const char *relativePath, char *dst, size_t dstSize);
int NativeAssets_ResolvePathStr8(NativeStr8 relativePath, char *dst, size_t dstSize);
int NativeAssets_ResolvePath(const char *relativePath, char *dst, size_t dstSize);
FILE *NativeAssets_OpenHostStr8(NativeStr8 relativePath, const char *mode);
FILE *NativeAssets_OpenHost(const char *relativePath, const char *mode);
FILE *NativeAssets_OpenHostBigfile(const char *mode);
int NativeAssets_ReadBytes(const char *path, int readMode, struct NativeAssetsByteBuffer *bytes);
void NativeAssets_FreeBytes(struct NativeAssetsByteBuffer *bytes);
int NativeAssets_Validate(void);

#endif
