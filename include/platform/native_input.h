#ifndef PLATFORM_NATIVE_INPUT_H
#define PLATFORM_NATIVE_INPUT_H

#include <macros.h>

#define PLATFORM_INPUT_PAD_COUNT 4

struct PlatformInputPadSnapshot
{
	u8 status;
	u8 id;
	u8 buttons[2];
	u8 analog[4];
	u8 connected;
	u8 reserved[3];
};

int Platform_InputInit(void);
void Platform_InputShutdown(void);
void Platform_InputUpdate(void);
void Platform_InputControllerAdded(int deviceIndex);
void Platform_InputControllerRemoved(int instanceId);
int Platform_InputCycleKeyboardController(void);
int Platform_InputCycleGamepadController(void);

/* The cabinet's own pad (docs/LOCKSTEP_RACE_MILESTONE.md LR-4): host
 * controller slot 0 (its gamepad, its G29, and the keyboard when mapped to
 * slot 0) as Platform_InputUpdate would build it, whether or not installed
 * pads are active. Writes only *dst: never the slot snapshots, the pad bus,
 * or any state Platform_InputCaptureState saves. Returns 1 with *dst
 * written; 0 for a NULL dst (nothing written), and 0 with the neutral
 * slot-0 snapshot in *dst while input is not initialized. The bytes are not
 * normalized; the race drive core does that. */
int Platform_InputSampleLocalPad(struct PlatformInputPadSnapshot *dst);

void Platform_InputPadInit(int slot, unsigned char *padData);
int Platform_InputPadGetState(int port);
void Platform_InputPadVibrate(int port, unsigned char *table, int len);
int Platform_InputCapturePadSnapshots(struct PlatformInputPadSnapshot *dst, int count);
int Platform_InputInstallPadSnapshots(const struct PlatformInputPadSnapshot *src, int count);
void Platform_InputClearInstalledPadSnapshots(void);
int Platform_InputGetStateSize(void);
int Platform_InputCaptureState(void *dst, int dstSize);
int Platform_InputRestoreState(const void *src, int srcSize);

#endif
