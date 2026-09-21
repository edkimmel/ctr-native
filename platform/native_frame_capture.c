#include <platform/native_frame_capture.h>

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static const char k_captureOption[] = "--capture-frame";
static const char k_captureOptionEquals[] = "--capture-frame=";
static const char k_exitOption[] = "--exit-after-frame";
static const char k_exitOptionEquals[] = "--exit-after-frame=";

void NativeFrameCapture_SetDefaults(struct NativeFrameCaptureConfig *config)
{
	if (config != NULL)
	{
		/* Zeroing the whole table keeps a defaulted config bit-for-bit
		 * reproducible, which the transactional tests rely on. */
		memset(config, 0, sizeof(*config));
	}
}

static int NativeFrameCapture_IsValid(const struct NativeFrameCaptureConfig *config)
{
	if (config == NULL)
	{
		return 0;
	}

	if ((config->requestCount < 0) || (config->requestCount > NATIVE_FRAME_CAPTURE_MAX_REQUESTS))
	{
		return 0;
	}

	if (config->exitAfterFrame < 0)
	{
		return 0;
	}

	for (int index = 0; index < config->requestCount; index++)
	{
		const struct NativeFrameCaptureRequest *request = &config->requests[index];

		if (request->frame < 1)
		{
			return 0;
		}

		if ((request->path[0] == '\0') ||
		    (memchr(request->path, '\0', sizeof(request->path)) == NULL))
		{
			return 0;
		}
	}

	return 1;
}

/*
 * Decimal only: no sign, no whitespace, no trailing garbage, >= 1, fits int.
 */
static int NativeFrameCapture_ParseFrame(const char *text, int *frame)
{
	char *end = NULL;
	long value;

	if ((text == NULL) || (text[0] == '\0') || (frame == NULL))
	{
		return 0;
	}

	for (const char *cursor = text; cursor[0] != '\0'; cursor++)
	{
		if ((cursor[0] < '0') || (cursor[0] > '9'))
		{
			return 0;
		}
	}

	errno = 0;
	value = strtol(text, &end, 10);
	if ((errno == ERANGE) || (end == text) || (end[0] != '\0') || (value < 1) || (value > INT_MAX))
	{
		return 0;
	}

	*frame = (int)value;
	return 1;
}

static int NativeFrameCapture_AddRequest(struct NativeFrameCaptureConfig *candidate, int frame, const char *path)
{
	size_t length;

	if (path == NULL)
	{
		return 0;
	}

	length = strlen(path);
	if ((length == 0) || (length >= NATIVE_FRAME_CAPTURE_MAX_PATH))
	{
		return 0;
	}

	/* Duplicate frame numbers: the last request on the command line wins. */
	for (int index = 0; index < candidate->requestCount; index++)
	{
		if (candidate->requests[index].frame == frame)
		{
			memset(candidate->requests[index].path, 0, sizeof(candidate->requests[index].path));
			memcpy(candidate->requests[index].path, path, length + 1);
			return 1;
		}
	}

	if (candidate->requestCount >= NATIVE_FRAME_CAPTURE_MAX_REQUESTS)
	{
		return 0;
	}

	candidate->requests[candidate->requestCount].frame = frame;
	memset(candidate->requests[candidate->requestCount].path, 0,
	       sizeof(candidate->requests[candidate->requestCount].path));
	memcpy(candidate->requests[candidate->requestCount].path, path, length + 1);
	candidate->requestCount++;
	return 1;
}

/*
 * `value` is the "<frame>=<path>" part of a capture request.  The frame number
 * ends at the first '='; everything after it is the path, so a path may itself
 * contain '='.
 */
static int NativeFrameCapture_ApplyRequest(struct NativeFrameCaptureConfig *candidate, const char *value)
{
	const char *separator;
	char frameText[32];
	size_t frameLength;
	int frame = 0;

	if ((candidate == NULL) || (value == NULL))
	{
		return 0;
	}

	separator = strchr(value, '=');
	if (separator == NULL)
	{
		return 0;
	}

	frameLength = (size_t)(separator - value);
	if ((frameLength == 0) || (frameLength >= sizeof(frameText)))
	{
		return 0;
	}

	memcpy(frameText, value, frameLength);
	frameText[frameLength] = '\0';

	if (!NativeFrameCapture_ParseFrame(frameText, &frame))
	{
		return 0;
	}

	return NativeFrameCapture_AddRequest(candidate, frame, separator + 1);
}

int NativeFrameCapture_ApplyArgs(int argc, char *argv[], struct NativeFrameCaptureConfig *config)
{
	struct NativeFrameCaptureConfig candidate;

	if (config == NULL)
	{
		return 0;
	}

	candidate = *config;
	if (!NativeFrameCapture_IsValid(&candidate))
	{
		return 0;
	}

	for (int index = 1; index < argc; index++)
	{
		const char *arg = argv[index];
		const char *value = NULL;
		int isExitOption = 0;

		if (arg == NULL)
		{
			return 0;
		}

		if (strcmp(arg, k_captureOption) == 0)
		{
			if ((index + 1 >= argc) || (argv[index + 1] == NULL) || (argv[index + 1][0] == '-'))
			{
				return 0;
			}
			value = argv[++index];
		}
		else if (strncmp(arg, k_captureOptionEquals, strlen(k_captureOptionEquals)) == 0)
		{
			value = arg + strlen(k_captureOptionEquals);
		}
		else if (strcmp(arg, k_exitOption) == 0)
		{
			if ((index + 1 >= argc) || (argv[index + 1] == NULL) || (argv[index + 1][0] == '-'))
			{
				return 0;
			}
			value = argv[++index];
			isExitOption = 1;
		}
		else if (strncmp(arg, k_exitOptionEquals, strlen(k_exitOptionEquals)) == 0)
		{
			value = arg + strlen(k_exitOptionEquals);
			isExitOption = 1;
		}
		else
		{
			continue;
		}

		if (isExitOption)
		{
			if (!NativeFrameCapture_ParseFrame(value, &candidate.exitAfterFrame))
			{
				return 0;
			}
		}
		else if (!NativeFrameCapture_ApplyRequest(&candidate, value))
		{
			return 0;
		}
	}

	*config = candidate;
	return 1;
}

const char *NativeFrameCapture_PathForFrame(const struct NativeFrameCaptureConfig *config, int frame)
{
	if ((config == NULL) || (config->requestCount <= 0) || (frame < 1))
	{
		return NULL;
	}

	for (int index = 0; index < config->requestCount; index++)
	{
		if (config->requests[index].frame == frame)
		{
			return config->requests[index].path;
		}
	}

	return NULL;
}

int NativeFrameCapture_ShouldExitAfterFrame(const struct NativeFrameCaptureConfig *config, int frame)
{
	if ((config == NULL) || (config->exitAfterFrame <= 0))
	{
		return 0;
	}

	return (frame >= config->exitAfterFrame) ? 1 : 0;
}
