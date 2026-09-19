#ifndef PLATFORM_NATIVE_PRESENTATION_REGISTRY_H
#define PLATFORM_NATIVE_PRESENTATION_REGISTRY_H

/*
 * Local-only presentation replacement registry.
 *
 * This module deliberately knows nothing about replay, checkpoints, match
 * configuration, canonical state, or content identity.  A caller may use an
 * exact match as a host-renderer hint only; failure or an absent pack must
 * retain the retail/native texture path.
 */

#include <macros.h>

#define NATIVE_PRESENTATION_REGISTRY_MAX_ENTRIES        64u
#define NATIVE_PRESENTATION_REGISTRY_MAX_ASSET_PATH     260u
#define NATIVE_PRESENTATION_REGISTRY_MAX_ASSET_ROOT     512u
/* Includes the optional separator between root and relative asset path. */
#define NATIVE_PRESENTATION_REGISTRY_MAX_RESOLVED_ASSET_PATH \
	(NATIVE_PRESENTATION_REGISTRY_MAX_ASSET_ROOT + NATIVE_PRESENTATION_REGISTRY_MAX_ASSET_PATH)
#define NATIVE_PRESENTATION_REGISTRY_MAX_MANIFEST_BYTES (64u * 1024u)
#define NATIVE_PRESENTATION_REGISTRY_FINGERPRINT_BYTES  17u

enum NativePresentationTextureMode
{
	NATIVE_PRESENTATION_TEXTURE_MODE_4_BIT = 4,
	NATIVE_PRESENTATION_TEXTURE_MODE_8_BIT = 8,
	NATIVE_PRESENTATION_TEXTURE_MODE_16_BIT = 16,
};

enum NativePresentationAssetClass
{
	NATIVE_PRESENTATION_ASSET_CLASS_FONT = 1,
	NATIVE_PRESENTATION_ASSET_CLASS_UI_ICON,
	NATIVE_PRESENTATION_ASSET_CLASS_UI_STATIC,
	/* Character art is deliberately distinct from generic/static UI. */
	NATIVE_PRESENTATION_ASSET_CLASS_CHARACTER_SPRITE,
};

enum NativePresentationRegistryError
{
	NATIVE_PRESENTATION_REGISTRY_ERROR_NONE = 0,
	NATIVE_PRESENTATION_REGISTRY_ERROR_ARGUMENT,
	NATIVE_PRESENTATION_REGISTRY_ERROR_ROOT_MISSING,
	NATIVE_PRESENTATION_REGISTRY_ERROR_ROOT_REPARSE_POINT,
	NATIVE_PRESENTATION_REGISTRY_ERROR_MANIFEST_TOO_LARGE,
	NATIVE_PRESENTATION_REGISTRY_ERROR_MANIFEST_FORMAT,
	NATIVE_PRESENTATION_REGISTRY_ERROR_ENTRY_LIMIT,
	NATIVE_PRESENTATION_REGISTRY_ERROR_DUPLICATE_KEY,
	NATIVE_PRESENTATION_REGISTRY_ERROR_ASSET_PATH,
	NATIVE_PRESENTATION_REGISTRY_ERROR_ASSET_REPARSE_POINT,
	NATIVE_PRESENTATION_REGISTRY_ERROR_ASSET_MISSING,
};

/* Every field is part of a match.  There are no page-, CLUT-, or rectangle-wide
 * wildcards; callers must construct this from the source primitive. */
struct NativePresentationSourceKey
{
	u8 textureMode;
	u16 tpage;
	u16 clut;
	u16 x;
	u16 y;
	u16 width;
	u16 height;
	u8 assetClass;
};

struct NativePresentationRegistryEntry
{
	struct NativePresentationSourceKey source;
	char assetPath[NATIVE_PRESENTATION_REGISTRY_MAX_ASSET_PATH];
};

struct NativePresentationRegistry
{
	/* Explicitly off by default, even after a manifest successfully loads. */
	int presentationOverrideEnabled;
	enum NativePresentationRegistryError lastError;
	unsigned int entryCount;
	char assetRoot[NATIVE_PRESENTATION_REGISTRY_MAX_ASSET_ROOT];
	char manifestFingerprint[NATIVE_PRESENTATION_REGISTRY_FINGERPRINT_BYTES];
	struct NativePresentationRegistryEntry entries[NATIVE_PRESENTATION_REGISTRY_MAX_ENTRIES];
};

void NativePresentationRegistry_Init(struct NativePresentationRegistry *registry);
void NativePresentationRegistry_SetEnabled(struct NativePresentationRegistry *registry, int enabled);
int NativePresentationRegistry_IsEnabled(const struct NativePresentationRegistry *registry);

/*
 * Parses the bounded, versioned text format:
 *   ctr-native-presentation-manifest<TAB>1
 *   entry<TAB>mode<TAB>tpage<TAB>clut<TAB>x<TAB>y<TAB>w<TAB>h<TAB>class<TAB>relative-asset
 *
 * All asset paths must resolve below assetRoot, name a regular existing file,
 * and traverse no symbolic link/reparse point. Loading is transactional and fail-closed: on any error `registry`
 * becomes empty and disabled.  A successful load remains disabled until an
 * explicit NativePresentationRegistry_SetEnabled(..., 1).
 */
int NativePresentationRegistry_LoadManifest(struct NativePresentationRegistry *registry, const char *assetRoot,
	                                         const char *manifest, size_t manifestSize);

/* Ignores enabled state, for diagnostics and selection-harness assertions. */
const struct NativePresentationRegistryEntry *NativePresentationRegistry_FindExact(
	const struct NativePresentationRegistry *registry, const struct NativePresentationSourceKey *source);

/* Returns NULL unless presentation overrides have explicitly been enabled. */
const struct NativePresentationRegistryEntry *NativePresentationRegistry_Lookup(
	const struct NativePresentationRegistry *registry, const struct NativePresentationSourceKey *source);

/*
 * Resolves one already-registered asset for a local host-presentation caller.
 * This is deliberately stricter than FindExact: it accepts only the exact
 * entry object owned by an explicitly enabled registry, then repeats the
 * regular-file, no-reparse-point, and canonical-below-root checks at use
 * time.  This closes the time-of-check/time-of-use window between manifest
 * parsing and a future host asset load.  It never changes the registry.
 *
 * `destination` is cleared on failure.  Its capacity must be at least
 * NATIVE_PRESENTATION_REGISTRY_MAX_RESOLVED_ASSET_PATH.
 */
int NativePresentationRegistry_ResolveEnabledAssetPath(
	const struct NativePresentationRegistry *registry,
	const struct NativePresentationRegistryEntry *entry,
	char *destination, size_t destinationSize);

enum NativePresentationRegistryError NativePresentationRegistry_GetLastError(const struct NativePresentationRegistry *registry);
const char *NativePresentationRegistry_ErrorString(enum NativePresentationRegistryError error);
const char *NativePresentationRegistry_GetManifestFingerprint(const struct NativePresentationRegistry *registry);

#endif
