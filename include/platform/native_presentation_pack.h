#ifndef PLATFORM_NATIVE_PRESENTATION_PACK_H
#define PLATFORM_NATIVE_PRESENTATION_PACK_H

/*
 * Local host-presentation pack lifecycle. This is intentionally a small
 * bridge between command-line configuration and the exact-key registry; it
 * neither loads GL textures nor touches game, replay, checkpoint, match, or
 * canonical state. A failed pack is always an ordinary retail-texture run.
 */

#include <platform/native_presentation_override_config.h>
#include <platform/native_presentation_registry.h>

#define NATIVE_PRESENTATION_PACK_MANIFEST_FILE_NAME "presentation.manifest"

enum NativePresentationPackError
{
	NATIVE_PRESENTATION_PACK_ERROR_NONE = 0,
	NATIVE_PRESENTATION_PACK_ERROR_ARGUMENT,
	NATIVE_PRESENTATION_PACK_ERROR_MANIFEST_PATH,
	NATIVE_PRESENTATION_PACK_ERROR_MANIFEST_MISSING,
	NATIVE_PRESENTATION_PACK_ERROR_MANIFEST_REPARSE_POINT,
	NATIVE_PRESENTATION_PACK_ERROR_MANIFEST_READ,
	NATIVE_PRESENTATION_PACK_ERROR_MANIFEST_TOO_LARGE,
	NATIVE_PRESENTATION_PACK_ERROR_REGISTRY,
};

struct NativePresentationPack
{
	struct NativePresentationRegistry registry;
	enum NativePresentationPackError lastError;
};

void NativePresentationPack_Init(struct NativePresentationPack *pack);

/*
 * With an absent pack or an explicit local `off` this succeeds without file
 * access and leaves the registry disabled. With an enabled pack it loads only
 * PACK_DIRECTORY/presentation.manifest, validates it transactionally through
 * the registry, and enables that registry only after full success.
 */
int NativePresentationPack_Load(const struct NativePresentationOverrideConfig *config,
	struct NativePresentationPack *pack);

enum NativePresentationPackError NativePresentationPack_GetLastError(const struct NativePresentationPack *pack);
const char *NativePresentationPack_ErrorString(enum NativePresentationPackError error);

#endif
