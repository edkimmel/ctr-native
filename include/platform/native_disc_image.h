#ifndef NATIVE_DISC_IMAGE_H
#define NATIVE_DISC_IMAGE_H

#include <macros.h>
#include "platform/native_identity.h"

#include <stddef.h>

struct NativeDiscImageFile
{
	u32 lba;
	u32 size;
};

int NativeDiscImage_Init(const char *assetsDir);
/* Releases the retained raw-image handle and invalidates all derived state.
 * Safe to call repeatedly; callers must reinitialize before later reads. */
void NativeDiscImage_Shutdown(void);
/* Returns nonzero only after the lazy content digest has been cached. */
int NativeDiscImage_ContentIdentityReady(void);
int NativeDiscImage_GetContentIdentity(uint8_t content[NATIVE_IDENTITY_DIGEST_BYTES]);
int NativeDiscImage_FindFile(const char *path, struct NativeDiscImageFile *fileOut);
int NativeDiscImage_ReadDataSectors(const struct NativeDiscImageFile *file, u32 sector, u32 sectorCount, void *dst);
int NativeDiscImage_ReadRawSectors(const struct NativeDiscImageFile *file, u32 sector, u32 sectorCount, void *dst);
int NativeDiscImage_ReadFileBytes(const char *path, int rawSectors, u8 **dataOut, int *sizeOut);

#endif
