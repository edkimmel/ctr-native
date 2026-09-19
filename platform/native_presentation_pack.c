#include <platform/native_presentation_pack.h>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4005) /* legacy macros.h owns offsetof in unity builds */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <platform/native_win32.h>
#else
#include <sys/stat.h>
#endif

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#define NATIVE_PRESENTATION_PACK_MANIFEST_PATH_BYTES \
	(NATIVE_PRESENTATION_REGISTRY_MAX_ASSET_ROOT + sizeof(NATIVE_PRESENTATION_PACK_MANIFEST_FILE_NAME) + 2u)

static void NativePresentationPack_Fail(struct NativePresentationPack *pack,
	enum NativePresentationPackError error)
{
	NativePresentationRegistry_Init(&pack->registry);
	pack->lastError = error;
}

void NativePresentationPack_Init(struct NativePresentationPack *pack)
{
	if (pack != NULL)
	{
		NativePresentationRegistry_Init(&pack->registry);
		pack->lastError = NATIVE_PRESENTATION_PACK_ERROR_NONE;
	}
}

static int NativePresentationPack_BuildManifestPath(char *destination, size_t destinationSize,
	const char *root)
{
	const size_t rootLength = root != NULL ? strlen(root) : 0u;
	size_t writeOffset;
	const int needsSeparator = rootLength > 0u && root[rootLength - 1u] != '/' && root[rootLength - 1u] != '\\';
	const size_t suffixLength = sizeof(NATIVE_PRESENTATION_PACK_MANIFEST_FILE_NAME) - 1u;
	const size_t required = rootLength + (needsSeparator ? 1u : 0u) + suffixLength + 1u;

	if ((rootLength == 0u) || (destination == NULL) || (required > destinationSize))
	{
		return 0;
	}

	memcpy(destination, root, rootLength);
	writeOffset = rootLength;
	if (needsSeparator)
	{
		destination[writeOffset++] = '/';
	}
	memcpy(destination + writeOffset, NATIVE_PRESENTATION_PACK_MANIFEST_FILE_NAME, suffixLength + 1u);
	return 1;
}

static int NativePresentationPack_IsManifestReparsePoint(const char *path)
{
#if defined(_WIN32)
	const DWORD attributes = GetFileAttributesA(path);
	return (attributes != INVALID_FILE_ATTRIBUTES) && ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0);
#else
	struct stat status;
	return lstat(path, &status) == 0 && S_ISLNK(status.st_mode);
#endif
}

static int NativePresentationPack_ReadManifest(const char *path, char **dataOut, size_t *sizeOut,
	enum NativePresentationPackError *errorOut)
{
	FILE *file;
	long fileLength;
	char *manifestData;

	*dataOut = NULL;
	*sizeOut = 0u;
	if (NativePresentationPack_IsManifestReparsePoint(path))
	{
		*errorOut = NATIVE_PRESENTATION_PACK_ERROR_MANIFEST_REPARSE_POINT;
		return 0;
	}
	file = fopen(path, "rb");
	if (file == NULL)
	{
		*errorOut = NATIVE_PRESENTATION_PACK_ERROR_MANIFEST_MISSING;
		return 0;
	}
	if (fseek(file, 0, SEEK_END) != 0)
	{
		(void)fclose(file);
		*errorOut = NATIVE_PRESENTATION_PACK_ERROR_MANIFEST_READ;
		return 0;
	}
	fileLength = ftell(file);
	if (fileLength <= 0 || (size_t)fileLength > NATIVE_PRESENTATION_REGISTRY_MAX_MANIFEST_BYTES || fseek(file, 0, SEEK_SET) != 0)
	{
		(void)fclose(file);
		*errorOut = (fileLength > 0 && (size_t)fileLength > NATIVE_PRESENTATION_REGISTRY_MAX_MANIFEST_BYTES)
			? NATIVE_PRESENTATION_PACK_ERROR_MANIFEST_TOO_LARGE : NATIVE_PRESENTATION_PACK_ERROR_MANIFEST_READ;
		return 0;
	}
	manifestData = (char *)malloc((size_t)fileLength);
	if (manifestData == NULL)
	{
		(void)fclose(file);
		*errorOut = NATIVE_PRESENTATION_PACK_ERROR_MANIFEST_READ;
		return 0;
	}
	if (fread(manifestData, 1u, (size_t)fileLength, file) != (size_t)fileLength)
	{
		(void)fclose(file);
		free(manifestData);
		*errorOut = NATIVE_PRESENTATION_PACK_ERROR_MANIFEST_READ;
		return 0;
	}
	if (fclose(file) != 0)
	{
		free(manifestData);
		*errorOut = NATIVE_PRESENTATION_PACK_ERROR_MANIFEST_READ;
		return 0;
	}
	*dataOut = manifestData;
	*sizeOut = (size_t)fileLength;
	return 1;
}

int NativePresentationPack_Load(const struct NativePresentationOverrideConfig *config,
	struct NativePresentationPack *pack)
{
	char manifestPath[NATIVE_PRESENTATION_PACK_MANIFEST_PATH_BYTES];
	char *manifest = NULL;
	size_t manifestSize = 0u;
	enum NativePresentationPackError error;

	if ((config == NULL) || (pack == NULL) || ((config->enabled != 0) && (config->enabled != 1)) ||
	    (config->enabled != 0 && (config->packDirectory == NULL || config->packDirectory[0] == '\0')))
	{
		if (pack != NULL)
		{
			NativePresentationPack_Fail(pack, NATIVE_PRESENTATION_PACK_ERROR_ARGUMENT);
		}
		return 0;
	}

	NativePresentationPack_Init(pack);
	if (config->enabled == 0)
	{
		return 1;
	}
	if (!NativePresentationPack_BuildManifestPath(manifestPath, sizeof(manifestPath), config->packDirectory))
	{
		NativePresentationPack_Fail(pack, NATIVE_PRESENTATION_PACK_ERROR_MANIFEST_PATH);
		return 0;
	}
	if (!NativePresentationPack_ReadManifest(manifestPath, &manifest, &manifestSize, &error))
	{
		NativePresentationPack_Fail(pack, error);
		return 0;
	}
	if (!NativePresentationRegistry_LoadManifest(&pack->registry, config->packDirectory, manifest, manifestSize))
	{
		free(manifest);
		pack->lastError = NATIVE_PRESENTATION_PACK_ERROR_REGISTRY;
		return 0;
	}
	free(manifest);
	NativePresentationRegistry_SetEnabled(&pack->registry, 1);
	pack->lastError = NATIVE_PRESENTATION_PACK_ERROR_NONE;
	return 1;
}

enum NativePresentationPackError NativePresentationPack_GetLastError(const struct NativePresentationPack *pack)
{
	return pack != NULL ? pack->lastError : NATIVE_PRESENTATION_PACK_ERROR_ARGUMENT;
}

const char *NativePresentationPack_ErrorString(enum NativePresentationPackError error)
{
	switch (error)
	{
	case NATIVE_PRESENTATION_PACK_ERROR_NONE: return "none";
	case NATIVE_PRESENTATION_PACK_ERROR_ARGUMENT: return "invalid local presentation configuration";
	case NATIVE_PRESENTATION_PACK_ERROR_MANIFEST_PATH: return "presentation manifest path is too long";
	case NATIVE_PRESENTATION_PACK_ERROR_MANIFEST_MISSING: return "presentation.manifest is missing";
	case NATIVE_PRESENTATION_PACK_ERROR_MANIFEST_REPARSE_POINT: return "presentation.manifest is a reparse point";
	case NATIVE_PRESENTATION_PACK_ERROR_MANIFEST_READ: return "presentation.manifest could not be read";
	case NATIVE_PRESENTATION_PACK_ERROR_MANIFEST_TOO_LARGE: return "presentation.manifest is too large";
	case NATIVE_PRESENTATION_PACK_ERROR_REGISTRY: return "presentation manifest or asset validation failed";
	default: return "unknown presentation pack error";
	}
}
