#if !defined(_WIN32)
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include <platform/native_presentation_registry.h>

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <platform/native_win32.h>
#else
#include <stdlib.h>
#endif

#define NATIVE_PRESENTATION_MANIFEST_HEADER "ctr-native-presentation-manifest\t1"

#if defined(_WIN32)
#define NATIVE_PRESENTATION_IS_DIRECTORY(mode) (((mode) & _S_IFDIR) != 0)
#define NATIVE_PRESENTATION_IS_REGULAR_FILE(mode) (((mode) & _S_IFREG) != 0)
#define NATIVE_PRESENTATION_CANONICAL_PATH_MAX 4096u
#else
#define NATIVE_PRESENTATION_IS_DIRECTORY(mode) S_ISDIR(mode)
#define NATIVE_PRESENTATION_IS_REGULAR_FILE(mode) S_ISREG(mode)
#define NATIVE_PRESENTATION_CANONICAL_PATH_MAX 4096u
#endif

static void NativePresentationRegistry_Clear(struct NativePresentationRegistry *registry,
	                                            enum NativePresentationRegistryError error)
{
	if (registry == NULL)
	{
		return;
	}

	memset(registry, 0, sizeof(*registry));
	registry->lastError = error;
	memcpy(registry->manifestFingerprint, "0000000000000000", NATIVE_PRESENTATION_REGISTRY_FINGERPRINT_BYTES);
}

void NativePresentationRegistry_Init(struct NativePresentationRegistry *registry)
{
	NativePresentationRegistry_Clear(registry, NATIVE_PRESENTATION_REGISTRY_ERROR_NONE);
}

void NativePresentationRegistry_SetEnabled(struct NativePresentationRegistry *registry, int enabled)
{
	if (registry != NULL)
	{
		registry->presentationOverrideEnabled = (enabled != 0) && (registry->entryCount != 0);
	}
}

int NativePresentationRegistry_IsEnabled(const struct NativePresentationRegistry *registry)
{
	return (registry != NULL) && (registry->presentationOverrideEnabled != 0);
}

static int NativePresentationRegistry_IsDirectory(const char *path)
{
	struct stat info;
	return (path != NULL) && (stat(path, &info) == 0) && NATIVE_PRESENTATION_IS_DIRECTORY(info.st_mode);
}

static int NativePresentationRegistry_IsRegularFile(const char *path)
{
	struct stat info;
	return (path != NULL) && (stat(path, &info) == 0) && NATIVE_PRESENTATION_IS_REGULAR_FILE(info.st_mode);
}

static int NativePresentationRegistry_Copy(char *destination, size_t destinationSize, const char *source, size_t sourceSize);

/* Reject links even when their final target would remain below the pack root:
 * presentation pack files are expected to be a self-contained, ordinary tree. */
static int NativePresentationRegistry_HasReparsePoint(const char *root, const char *relativePath)
{
	char current[NATIVE_PRESENTATION_REGISTRY_MAX_ASSET_ROOT + NATIVE_PRESENTATION_REGISTRY_MAX_ASSET_PATH];
	size_t currentLength;
	size_t segmentStart = 0;
	size_t relativeLength;

	if ((root == NULL) || (relativePath == NULL) || !NativePresentationRegistry_Copy(current, sizeof(current), root, strlen(root)))
	{
		return 1;
	}
	currentLength = strlen(current);
	relativeLength = strlen(relativePath);

#if defined(_WIN32)
	if ((GetFileAttributesA(current) & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
	{
		return 1;
	}
#else
	{
		struct stat info;
		if ((lstat(current, &info) == 0) && S_ISLNK(info.st_mode))
		{
			return 1;
		}
	}
#endif

	for (size_t index = 0; index <= relativeLength; index++)
	{
		if ((index != relativeLength) && (relativePath[index] != '/') && (relativePath[index] != '\\'))
		{
			continue;
		}

		if ((currentLength == 0) || ((current[currentLength - 1u] != '/') && (current[currentLength - 1u] != '\\')))
		{
			if ((currentLength + 1u) >= sizeof(current))
			{
				return 1;
			}
			current[currentLength++] = '/';
		}
		if ((currentLength + (index - segmentStart)) >= sizeof(current))
		{
			return 1;
		}
		for (size_t segmentIndex = segmentStart; segmentIndex < index; segmentIndex++)
		{
			current[currentLength++] = (relativePath[segmentIndex] == '\\') ? '/' : relativePath[segmentIndex];
		}
		current[currentLength] = '\0';

#if defined(_WIN32)
		if ((GetFileAttributesA(current) & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
		{
			return 1;
		}
#else
		{
			struct stat info;
			if ((lstat(current, &info) == 0) && S_ISLNK(info.st_mode))
			{
				return 1;
			}
		}
#endif
		segmentStart = index + 1u;
	}

	return 0;
}

static int NativePresentationRegistry_PathStartsWith(const char *path, const char *prefix)
{
	size_t prefixLength = strlen(prefix);

	while ((prefixLength > 0) && ((prefix[prefixLength - 1u] == '/') || (prefix[prefixLength - 1u] == '\\')))
	{
		prefixLength--;
	}
	if (prefixLength == 0)
	{
		return 0;
	}
	if (strlen(path) < prefixLength)
	{
		return 0;
	}
	for (size_t index = 0; index < prefixLength; index++)
	{
		char pathChar = path[index];
		char prefixChar = prefix[index];
		if ((pathChar >= 'A') && (pathChar <= 'Z')) pathChar = (char)(pathChar + ('a' - 'A'));
		if ((prefixChar >= 'A') && (prefixChar <= 'Z')) prefixChar = (char)(prefixChar + ('a' - 'A'));
		if (pathChar != prefixChar)
		{
			return 0;
		}
	}
	return (path[prefixLength] == '\0') || (path[prefixLength] == '/') || (path[prefixLength] == '\\');
}

static int NativePresentationRegistry_IsResolvedBelowRoot(const char *root, const char *assetPath)
{
#if defined(_WIN32)
	HANDLE rootHandle;
	HANDLE assetHandle;
	char rootFinal[NATIVE_PRESENTATION_CANONICAL_PATH_MAX];
	char assetFinal[NATIVE_PRESENTATION_CANONICAL_PATH_MAX];
	DWORD rootLength;
	DWORD assetLength;

	rootHandle = CreateFileA(root, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
	                         OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (rootHandle == INVALID_HANDLE_VALUE)
	{
		return 0;
	}
	assetHandle = CreateFileA(assetPath, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
	                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (assetHandle == INVALID_HANDLE_VALUE)
	{
		CloseHandle(rootHandle);
		return 0;
	}
	rootLength = GetFinalPathNameByHandleA(rootHandle, rootFinal, (DWORD)sizeof(rootFinal), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
	assetLength = GetFinalPathNameByHandleA(assetHandle, assetFinal, (DWORD)sizeof(assetFinal), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
	CloseHandle(assetHandle);
	CloseHandle(rootHandle);
	if ((rootLength == 0) || (assetLength == 0) || (rootLength >= sizeof(rootFinal)) || (assetLength >= sizeof(assetFinal)))
	{
		return 0;
	}
	return NativePresentationRegistry_PathStartsWith(assetFinal, rootFinal);
#else
	char rootFinal[NATIVE_PRESENTATION_CANONICAL_PATH_MAX];
	char assetFinal[NATIVE_PRESENTATION_CANONICAL_PATH_MAX];
	return (realpath(root, rootFinal) != NULL) && (realpath(assetPath, assetFinal) != NULL) &&
	       NativePresentationRegistry_PathStartsWith(assetFinal, rootFinal);
#endif
}

static int NativePresentationRegistry_Copy(char *destination, size_t destinationSize, const char *source, size_t sourceSize)
{
	if ((destination == NULL) || (source == NULL) || (sourceSize >= destinationSize))
	{
		return 0;
	}

	memcpy(destination, source, sourceSize);
	destination[sourceSize] = '\0';
	return 1;
}

static int NativePresentationRegistry_ParseUnsigned(const char *text, size_t length, unsigned int maximum, unsigned int *valueOut)
{
	unsigned int value = 0;

	if ((text == NULL) || (length == 0) || (valueOut == NULL))
	{
		return 0;
	}

	for (size_t index = 0; index < length; index++)
	{
		unsigned int digit;
		if ((text[index] < '0') || (text[index] > '9'))
		{
			return 0;
		}
		digit = (unsigned int)(text[index] - '0');
		if (digit > maximum)
		{
			return 0;
		}
		if (value > ((maximum - digit) / 10u))
		{
			return 0;
		}
		value = value * 10u + digit;
	}

	*valueOut = value;
	return 1;
}

static int NativePresentationRegistry_ParseAssetClass(const char *text, size_t length, u8 *assetClass)
{
	if ((text == NULL) || (assetClass == NULL))
	{
		return 0;
	}

	if ((length == 4u) && (memcmp(text, "font", 4u) == 0))
	{
		*assetClass = NATIVE_PRESENTATION_ASSET_CLASS_FONT;
		return 1;
	}
	if ((length == 7u) && (memcmp(text, "ui-icon", 7u) == 0))
	{
		*assetClass = NATIVE_PRESENTATION_ASSET_CLASS_UI_ICON;
		return 1;
	}
	if ((length == 9u) && (memcmp(text, "ui-static", 9u) == 0))
	{
		*assetClass = NATIVE_PRESENTATION_ASSET_CLASS_UI_STATIC;
		return 1;
	}
	if ((length == 16u) && (memcmp(text, "character-sprite", 16u) == 0))
	{
		*assetClass = NATIVE_PRESENTATION_ASSET_CLASS_CHARACTER_SPRITE;
		return 1;
	}
	return 0;
}

static int NativePresentationRegistry_IsSafeRelativeAssetPath(const char *path, size_t length)
{
	size_t segmentStart = 0;

	if ((path == NULL) || (length == 0) || (path[0] == '/') || (path[0] == '\\') || ((length >= 2u) && (path[1] == ':')))
	{
		return 0;
	}

	for (size_t index = 0; index <= length; index++)
	{
		if ((index == length) || (path[index] == '/') || (path[index] == '\\'))
		{
			size_t segmentLength = index - segmentStart;
			if ((segmentLength == 0) || ((segmentLength == 1u) && (path[segmentStart] == '.')) ||
			    ((segmentLength == 2u) && (path[segmentStart] == '.') && (path[segmentStart + 1u] == '.')))
			{
				return 0;
			}
			segmentStart = index + 1u;
		}
	}
	return 1;
}

static int NativePresentationRegistry_BuildAssetPath(char *destination, size_t destinationSize, const char *root,
	                                                    const char *relativePath)
{
	size_t rootLength = strlen(root);
	size_t relativeLength = strlen(relativePath);
	int needsSeparator;

	while ((rootLength > 0) && ((root[rootLength - 1u] == '/') || (root[rootLength - 1u] == '\\')))
	{
		rootLength--;
	}
	needsSeparator = (rootLength != 0);
	if ((rootLength + (size_t)needsSeparator + relativeLength) >= destinationSize)
	{
		return 0;
	}

	memcpy(destination, root, rootLength);
	if (needsSeparator != 0)
	{
		destination[rootLength++] = '/';
	}
	memcpy(destination + rootLength, relativePath, relativeLength);
	destination[rootLength + relativeLength] = '\0';
	return 1;
}

static int NativePresentationRegistry_KeysEqual(const struct NativePresentationSourceKey *left,
	                                                const struct NativePresentationSourceKey *right)
{
	return (left->textureMode == right->textureMode) && (left->tpage == right->tpage) && (left->clut == right->clut) &&
	       (left->x == right->x) && (left->y == right->y) && (left->width == right->width) &&
	       (left->height == right->height) && (left->assetClass == right->assetClass);
}

static void NativePresentationRegistry_Fingerprint(const char *manifest, size_t manifestSize, char output[NATIVE_PRESENTATION_REGISTRY_FINGERPRINT_BYTES])
{
	u64 hash = 1469598103934665603ULL;
	static const char hex[] = "0123456789abcdef";

	for (size_t index = 0; index < manifestSize; index++)
	{
		hash ^= (u8)manifest[index];
		hash *= 1099511628211ULL;
	}

	for (size_t index = 0; index < 16u; index++)
	{
		output[15u - index] = hex[hash & 15u];
		hash >>= 4u;
	}
	output[16] = '\0';
}

static enum NativePresentationRegistryError NativePresentationRegistry_ParseEntry(struct NativePresentationRegistry *candidate,
	                                                                                   const char *line, size_t lineLength)
{
	const char *fields[10];
	size_t lengths[10];
	unsigned int values[7];
	struct NativePresentationRegistryEntry *entry;
	char fullAssetPath[NATIVE_PRESENTATION_REGISTRY_MAX_ASSET_ROOT + NATIVE_PRESENTATION_REGISTRY_MAX_ASSET_PATH];
	size_t fieldStart = 0;
	unsigned int fieldCount = 0;

	for (size_t index = 0; index <= lineLength; index++)
	{
		if ((index == lineLength) || (line[index] == '\t'))
		{
			if (fieldCount >= 10u)
			{
				return NATIVE_PRESENTATION_REGISTRY_ERROR_MANIFEST_FORMAT;
			}
			fields[fieldCount] = line + fieldStart;
			lengths[fieldCount++] = index - fieldStart;
			fieldStart = index + 1u;
		}
	}

	if ((fieldCount != 10u) || (lengths[0] != 5u) || (memcmp(fields[0], "entry", 5u) != 0))
	{
		return NATIVE_PRESENTATION_REGISTRY_ERROR_MANIFEST_FORMAT;
	}
	if (candidate->entryCount >= NATIVE_PRESENTATION_REGISTRY_MAX_ENTRIES)
	{
		return NATIVE_PRESENTATION_REGISTRY_ERROR_ENTRY_LIMIT;
	}

	if (!NativePresentationRegistry_ParseUnsigned(fields[1], lengths[1], 16u, &values[0]) ||
	    !NativePresentationRegistry_ParseUnsigned(fields[2], lengths[2], 0x9ffu, &values[1]) ||
	    !NativePresentationRegistry_ParseUnsigned(fields[3], lengths[3], 0xffffu, &values[2]) ||
	    !NativePresentationRegistry_ParseUnsigned(fields[4], lengths[4], 1023u, &values[3]) ||
	    !NativePresentationRegistry_ParseUnsigned(fields[5], lengths[5], 511u, &values[4]) ||
	    !NativePresentationRegistry_ParseUnsigned(fields[6], lengths[6], 1024u, &values[5]) ||
	    !NativePresentationRegistry_ParseUnsigned(fields[7], lengths[7], 512u, &values[6]))
	{
		return NATIVE_PRESENTATION_REGISTRY_ERROR_MANIFEST_FORMAT;
	}
	if ((values[0] != NATIVE_PRESENTATION_TEXTURE_MODE_4_BIT) && (values[0] != NATIVE_PRESENTATION_TEXTURE_MODE_8_BIT) &&
	    (values[0] != NATIVE_PRESENTATION_TEXTURE_MODE_16_BIT))
	{
		return NATIVE_PRESENTATION_REGISTRY_ERROR_MANIFEST_FORMAT;
	}
	if ((values[5] == 0u) || (values[6] == 0u) || (values[3] + values[5] > 1024u) || (values[4] + values[6] > 512u))
	{
		return NATIVE_PRESENTATION_REGISTRY_ERROR_MANIFEST_FORMAT;
	}

	entry = &candidate->entries[candidate->entryCount];
	memset(entry, 0, sizeof(*entry));
	entry->source.textureMode = (u8)values[0];
	entry->source.tpage = (u16)values[1];
	entry->source.clut = (u16)values[2];
	entry->source.x = (u16)values[3];
	entry->source.y = (u16)values[4];
	entry->source.width = (u16)values[5];
	entry->source.height = (u16)values[6];
	if (!NativePresentationRegistry_ParseAssetClass(fields[8], lengths[8], &entry->source.assetClass))
	{
		return NATIVE_PRESENTATION_REGISTRY_ERROR_MANIFEST_FORMAT;
	}
	if (!NativePresentationRegistry_IsSafeRelativeAssetPath(fields[9], lengths[9]) ||
	    !NativePresentationRegistry_Copy(entry->assetPath, sizeof(entry->assetPath), fields[9], lengths[9]))
	{
		return NATIVE_PRESENTATION_REGISTRY_ERROR_ASSET_PATH;
	}
	if (!NativePresentationRegistry_BuildAssetPath(fullAssetPath, sizeof(fullAssetPath), candidate->assetRoot, entry->assetPath) ||
	    !NativePresentationRegistry_IsRegularFile(fullAssetPath))
	{
		return NATIVE_PRESENTATION_REGISTRY_ERROR_ASSET_MISSING;
	}
	if (NativePresentationRegistry_HasReparsePoint(candidate->assetRoot, entry->assetPath) ||
	    !NativePresentationRegistry_IsResolvedBelowRoot(candidate->assetRoot, fullAssetPath))
	{
		return NATIVE_PRESENTATION_REGISTRY_ERROR_ASSET_REPARSE_POINT;
	}

	for (unsigned int index = 0; index < candidate->entryCount; index++)
	{
		if (NativePresentationRegistry_KeysEqual(&candidate->entries[index].source, &entry->source))
		{
			return NATIVE_PRESENTATION_REGISTRY_ERROR_DUPLICATE_KEY;
		}
	}

	candidate->entryCount++;
	return NATIVE_PRESENTATION_REGISTRY_ERROR_NONE;
}

int NativePresentationRegistry_LoadManifest(struct NativePresentationRegistry *registry, const char *assetRoot,
	                                         const char *manifest, size_t manifestSize)
{
	struct NativePresentationRegistry candidate;
	size_t cursor = 0;
	unsigned int lineNumber = 0;
	enum NativePresentationRegistryError error = NATIVE_PRESENTATION_REGISTRY_ERROR_NONE;

	if (registry == NULL)
	{
		return 0;
	}
	NativePresentationRegistry_Init(&candidate);
	if ((assetRoot == NULL) || (assetRoot[0] == '\0') || !NativePresentationRegistry_IsDirectory(assetRoot))
	{
		error = NATIVE_PRESENTATION_REGISTRY_ERROR_ROOT_MISSING;
		goto failed;
	}
	if (NativePresentationRegistry_HasReparsePoint(assetRoot, ""))
	{
		error = NATIVE_PRESENTATION_REGISTRY_ERROR_ROOT_REPARSE_POINT;
		goto failed;
	}
	if (!NativePresentationRegistry_Copy(candidate.assetRoot, sizeof(candidate.assetRoot), assetRoot, strlen(assetRoot)) ||
	    (manifest == NULL) || (manifestSize == 0) || (manifestSize > NATIVE_PRESENTATION_REGISTRY_MAX_MANIFEST_BYTES))
	{
		error = (manifestSize > NATIVE_PRESENTATION_REGISTRY_MAX_MANIFEST_BYTES) ? NATIVE_PRESENTATION_REGISTRY_ERROR_MANIFEST_TOO_LARGE : NATIVE_PRESENTATION_REGISTRY_ERROR_ARGUMENT;
		goto failed;
	}

	while (cursor < manifestSize)
	{
		size_t lineStart = cursor;
		size_t lineLength;
		while ((cursor < manifestSize) && (manifest[cursor] != '\n'))
		{
			cursor++;
		}
		lineLength = cursor - lineStart;
		if ((lineLength != 0) && (manifest[lineStart + lineLength - 1u] == '\r'))
		{
			lineLength--;
		}
		if (cursor < manifestSize)
		{
			cursor++;
		}
		if (lineLength == 0)
		{
			error = NATIVE_PRESENTATION_REGISTRY_ERROR_MANIFEST_FORMAT;
			goto failed;
		}

		if (lineNumber++ == 0u)
		{
			if ((lineLength != sizeof(NATIVE_PRESENTATION_MANIFEST_HEADER) - 1u) ||
			    (memcmp(manifest + lineStart, NATIVE_PRESENTATION_MANIFEST_HEADER, lineLength) != 0))
			{
				error = NATIVE_PRESENTATION_REGISTRY_ERROR_MANIFEST_FORMAT;
				goto failed;
			}
		}
		else if ((error = NativePresentationRegistry_ParseEntry(&candidate, manifest + lineStart, lineLength)) != NATIVE_PRESENTATION_REGISTRY_ERROR_NONE)
		{
			goto failed;
		}
	}

	if ((lineNumber != 0u) && (candidate.entryCount != 0u))
	{
		NativePresentationRegistry_Fingerprint(manifest, manifestSize, candidate.manifestFingerprint);
		candidate.presentationOverrideEnabled = 0;
		candidate.lastError = NATIVE_PRESENTATION_REGISTRY_ERROR_NONE;
		*registry = candidate;
		return 1;
	}
	error = NATIVE_PRESENTATION_REGISTRY_ERROR_MANIFEST_FORMAT;

failed:
	NativePresentationRegistry_Clear(registry, error);
	return 0;
}

const struct NativePresentationRegistryEntry *NativePresentationRegistry_FindExact(
	const struct NativePresentationRegistry *registry, const struct NativePresentationSourceKey *source)
{
	if ((registry == NULL) || (source == NULL))
	{
		return NULL;
	}
	for (unsigned int index = 0; index < registry->entryCount; index++)
	{
		if (NativePresentationRegistry_KeysEqual(&registry->entries[index].source, source))
		{
			return &registry->entries[index];
		}
	}
	return NULL;
}

const struct NativePresentationRegistryEntry *NativePresentationRegistry_Lookup(
	const struct NativePresentationRegistry *registry, const struct NativePresentationSourceKey *source)
{
	return NativePresentationRegistry_IsEnabled(registry) ? NativePresentationRegistry_FindExact(registry, source) : NULL;
}

enum NativePresentationRegistryError NativePresentationRegistry_GetLastError(const struct NativePresentationRegistry *registry)
{
	return (registry != NULL) ? registry->lastError : NATIVE_PRESENTATION_REGISTRY_ERROR_ARGUMENT;
}

const char *NativePresentationRegistry_ErrorString(enum NativePresentationRegistryError error)
{
	switch (error)
	{
	case NATIVE_PRESENTATION_REGISTRY_ERROR_NONE: return "none";
	case NATIVE_PRESENTATION_REGISTRY_ERROR_ARGUMENT: return "invalid argument";
	case NATIVE_PRESENTATION_REGISTRY_ERROR_ROOT_MISSING: return "asset root missing";
	case NATIVE_PRESENTATION_REGISTRY_ERROR_ROOT_REPARSE_POINT: return "asset root contains a reparse point";
	case NATIVE_PRESENTATION_REGISTRY_ERROR_MANIFEST_TOO_LARGE: return "manifest too large";
	case NATIVE_PRESENTATION_REGISTRY_ERROR_MANIFEST_FORMAT: return "manifest format invalid";
	case NATIVE_PRESENTATION_REGISTRY_ERROR_ENTRY_LIMIT: return "manifest entry limit exceeded";
	case NATIVE_PRESENTATION_REGISTRY_ERROR_DUPLICATE_KEY: return "duplicate source key";
	case NATIVE_PRESENTATION_REGISTRY_ERROR_ASSET_PATH: return "asset path invalid";
	case NATIVE_PRESENTATION_REGISTRY_ERROR_ASSET_REPARSE_POINT: return "asset path contains a reparse point";
	case NATIVE_PRESENTATION_REGISTRY_ERROR_ASSET_MISSING: return "asset missing";
	default: return "unknown error";
	}
}

const char *NativePresentationRegistry_GetManifestFingerprint(const struct NativePresentationRegistry *registry)
{
	return (registry != NULL) ? registry->manifestFingerprint : "0000000000000000";
}
