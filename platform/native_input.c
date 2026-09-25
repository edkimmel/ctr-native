#include <platform/native_input.h>
#include <platform/native_g29_input.h>

#include <macros.h>
#include "psx/libpad.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NATIVE_INPUT_MAX_CONTROLLERS       PLATFORM_INPUT_PAD_COUNT
#define NATIVE_INPUT_PHYSICAL_SLOT_COUNT   2
#define NATIVE_INPUT_PAD_PACKET_BYTES      8
#define NATIVE_INPUT_MULTITAP_HEADER       2
#define NATIVE_INPUT_PAD_DIGITAL           0x41
#define NATIVE_INPUT_PAD_ANALOG            0x73
#define NATIVE_INPUT_PAD_MULTITAP          0x80
#define NATIVE_INPUT_PAD_DISCONNECT        0xff
#define NATIVE_INPUT_AXIS_DEADZONE         500
#define NATIVE_INPUT_MAP_FLAG_AXIS         0x4000
#define NATIVE_INPUT_MAP_FLAG_INVERSE      0x8000
#define NATIVE_INPUT_DEFAULT_KEYBOARD_SLOT 0
#define NATIVE_INPUT_G29_DIAGNOSTIC_ENV     "CTR_NATIVE_G29_DIAGNOSTICS"
#define NATIVE_INPUT_G29_DIAGNOSTIC_AXES    16
#define NATIVE_INPUT_G29_DIAGNOSTIC_DELTA   2048
// NOTE(aalhendi): Little-endian tag `CTRI` = CTR native Input snapshot.
#define NATIVE_INPUT_STATE_MAGIC           0x49525443
#define NATIVE_INPUT_STATE_VERSION         2

// NOTE(aalhendi): Native input preserves behavior from PsyCross's
// MIT-licensed pad implementation while moving host ownership into ctr-native.
// See THIRD_PARTY_NOTICES.md.

struct NativeInputKeyboardMapping
{
	s32 id;

	s32 kc_square, kc_circle, kc_triangle, kc_cross;

	s32 kc_l1, kc_l2, kc_l3;
	s32 kc_r1, kc_r2, kc_r3;

	s32 kc_start, kc_select;

	s32 kc_dpad_left, kc_dpad_right, kc_dpad_up, kc_dpad_down;
};

struct NativeInputControllerMapping
{
	s32 id;

	s32 gc_square, gc_circle, gc_triangle, gc_cross;

	s32 gc_l1, gc_l2, gc_l3;
	s32 gc_r1, gc_r2, gc_r3;

	s32 gc_start, gc_select;

	s32 gc_dpad_left, gc_dpad_right, gc_dpad_up, gc_dpad_down;

	s32 gc_axis_left_x, gc_axis_left_y;
	s32 gc_axis_right_x, gc_axis_right_y;
};

struct NativeInputController
{
	SDL_JoystickID instanceId;
	SDL_Gamepad *controller;
	SDL_Joystick *joystick;
	struct NativeG29MappingState g29State;
	s32 analogEnabled;
	s32 switchingAnalog;
	struct PlatformInputPadSnapshot snapshot;
};

/* This is deliberately outside NativeInputStateSnapshot: it is host-only
 * observation state, not input state that can affect gameplay or replay. */
struct NativeInputG29Diagnostic
{
	s32 valid;
	s32 axisCount;
	int16_t axes[NATIVE_INPUT_G29_DIAGNOSTIC_AXES];
	struct NativeG29MappingState mappingState;
	uint16_t buttons;
};

struct NativeInputControllerStateSnapshot
{
	struct PlatformInputPadSnapshot snapshot;
	struct NativeG29MappingState g29State;
	s32 analogEnabled;
	s32 switchingAnalog;
	s32 controllerToSlotMapping;
};

struct NativeInputStateSnapshot
{
	u32 magic;
	u32 version;
	u32 size;
	s32 keyboardControllerSlot;
	s32 lastActiveControllerSlot;
	s32 installedSnapshotsActive;
	struct PlatformInputPadSnapshot installedSnapshots[NATIVE_INPUT_MAX_CONTROLLERS];
	struct NativeInputControllerStateSnapshot controllers[NATIVE_INPUT_MAX_CONTROLLERS];
};

global_variable struct NativeInputControllerMapping s_controllerMapping;
global_variable struct NativeInputKeyboardMapping s_keyboardMapping;
global_variable s32 s_controllerToSlotMapping[NATIVE_INPUT_MAX_CONTROLLERS] = {-1, -1, -1, -1};

global_variable struct NativeInputController s_controllers[NATIVE_INPUT_MAX_CONTROLLERS];
global_variable struct PlatformInputPadSnapshot s_installedSnapshots[NATIVE_INPUT_MAX_CONTROLLERS];
global_variable u8 *s_padSlotData[NATIVE_INPUT_PHYSICAL_SLOT_COUNT];
global_variable const bool *s_keyboardState;
global_variable s32 s_inputInitialized;
global_variable s32 s_installedSnapshotsActive;
global_variable s32 s_keyboardControllerSlot = NATIVE_INPUT_DEFAULT_KEYBOARD_SLOT;
global_variable s32 s_lastActiveControllerSlot = -1;
global_variable SDL_JoystickID s_directG29InstanceId = -1;
global_variable struct NativeInputG29Diagnostic s_g29Diagnostic;

/* The local sample's own G29 pedal hysteresis (LR-37). Like s_g29Diagnostic
 * it is deliberately outside NativeInputStateSnapshot: host-local and
 * sample-only, never saved, restored, or written by Platform_InputUpdate.
 * Re-arm rule: a (non-repeat) Platform_InputInit, Platform_InputShutdown,
 * an install of pad snapshots that turns installed pads on (inactive to
 * active), Platform_InputClearInstalledPadSnapshots, a successful
 * Platform_InputRestoreState, and every change of slot 0's device (closing
 * or opening slot 0, or a swap involving slot 0) set s_sampleG29Armed. The
 * first sample after that seeds the state from slot 0's live g29State
 * (read-only); later samples advance it alone, so a race's samples keep one
 * continuous hysteresis while Platform_InputUpdate replays installed pads. */
global_variable struct NativeG29MappingState s_sampleG29State;
global_variable s32 s_sampleG29Armed = 1;

extern s32 g_padCommEnable;

internal u16 NativeInput_GetSnapshotButtons(const struct PlatformInputPadSnapshot *snapshot)
{
	return (u16)(snapshot->buttons[0] | (snapshot->buttons[1] << 8));
}

internal void NativeInput_SetSnapshotButtons(struct PlatformInputPadSnapshot *snapshot, u16 buttons)
{
	snapshot->buttons[0] = (u8)(buttons & 0xff);
	snapshot->buttons[1] = (u8)(buttons >> 8);
}

/* The snapshot Platform_InputUpdate starts each slot from: slot 0 is the
 * connected digital pad with nothing pressed, every other slot is
 * disconnected. Writes only *snapshot. */
internal void NativeInput_MakeResetSnapshot(s32 slot, struct PlatformInputPadSnapshot *snapshot)
{
	snapshot->connected = slot == 0;
	snapshot->status = snapshot->connected ? 0 : NATIVE_INPUT_PAD_DISCONNECT;
	snapshot->id = snapshot->connected ? NATIVE_INPUT_PAD_DIGITAL : NATIVE_INPUT_PAD_DISCONNECT;
	NativeInput_SetSnapshotButtons(snapshot, 0xffff);
	snapshot->analog[0] = 0x80;
	snapshot->analog[1] = 0x80;
	snapshot->analog[2] = 0x80;
	snapshot->analog[3] = 0x80;
	memset(snapshot->reserved, 0, sizeof(snapshot->reserved));
}

internal void NativeInput_ResetSnapshot(s32 slot)
{
	NativeInput_MakeResetSnapshot(slot, &s_controllers[slot].snapshot);
}

internal s32 NativeInput_IsValidControllerSlot(s32 slot)
{
	return (slot >= 0) && (slot < NATIVE_INPUT_MAX_CONTROLLERS);
}

internal s32 NativeInput_NextControllerSlot(s32 slot)
{
	slot++;
	if (slot >= NATIVE_INPUT_MAX_CONTROLLERS)
	{
		slot = 0;
	}

	return slot;
}

internal void NativeInput_MoveKeyboardOffControllerSlot(s32 slot)
{
	if (s_keyboardControllerSlot == slot)
	{
		s_keyboardControllerSlot = NativeInput_NextControllerSlot(s_keyboardControllerSlot);
	}
}

internal void NativeInput_MakeDisconnectedSnapshot(struct PlatformInputPadSnapshot *snapshot)
{
	if (snapshot == NULL)
	{
		return;
	}

	snapshot->connected = 0;
	snapshot->status = NATIVE_INPUT_PAD_DISCONNECT;
	snapshot->id = NATIVE_INPUT_PAD_DISCONNECT;
	NativeInput_SetSnapshotButtons(snapshot, 0xffff);
	snapshot->analog[0] = 0x80;
	snapshot->analog[1] = 0x80;
	snapshot->analog[2] = 0x80;
	snapshot->analog[3] = 0x80;
	memset(snapshot->reserved, 0, sizeof(snapshot->reserved));
}

internal void NativeInput_WritePadPacket(u8 *dst, const struct PlatformInputPadSnapshot *snapshot)
{
	if ((dst == NULL) || (snapshot == NULL))
	{
		return;
	}

	dst[0] = snapshot->status;
	dst[1] = snapshot->id;
	dst[2] = snapshot->buttons[0];
	dst[3] = snapshot->buttons[1];
	dst[4] = snapshot->analog[0];
	dst[5] = snapshot->analog[1];
	dst[6] = snapshot->analog[2];
	dst[7] = snapshot->analog[3];
}

internal s32 NativeInput_UseMultitapBus(void)
{
	for (s32 slot = NATIVE_INPUT_PHYSICAL_SLOT_COUNT; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		if (s_controllers[slot].snapshot.connected != 0)
		{
			return 1;
		}
	}

	return 0;
}

internal void NativeInput_WritePadBus(void)
{
	u8 *slot0 = s_padSlotData[0];
	u8 *slot1 = s_padSlotData[1];
	s32 useMultitap = NativeInput_UseMultitapBus();
	s32 slot;

	if (slot0 != NULL)
	{
		if (useMultitap != 0)
		{
			slot0[0] = 0;
			slot0[1] = NATIVE_INPUT_PAD_MULTITAP;
			for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
			{
				NativeInput_WritePadPacket(&slot0[NATIVE_INPUT_MULTITAP_HEADER + (slot * NATIVE_INPUT_PAD_PACKET_BYTES)], &s_controllers[slot].snapshot);
			}
		}
		else
		{
			NativeInput_WritePadPacket(slot0, &s_controllers[0].snapshot);
		}
	}

	if (slot1 != NULL)
	{
		if (useMultitap != 0)
		{
			struct PlatformInputPadSnapshot disconnected;

			NativeInput_MakeDisconnectedSnapshot(&disconnected);
			NativeInput_WritePadPacket(slot1, &disconnected);
		}
		else
		{
			NativeInput_WritePadPacket(slot1, &s_controllers[1].snapshot);
		}
	}
}

internal void NativeInput_WriteInstalledSnapshots(void)
{
	for (s32 slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		s_controllers[slot].snapshot = s_installedSnapshots[slot];
	}

	NativeInput_WritePadBus();
}

internal void NativeInput_DefaultMappings(void)
{
	s_keyboardMapping.kc_square = SDL_SCANCODE_X;
	s_keyboardMapping.kc_circle = SDL_SCANCODE_V;
	s_keyboardMapping.kc_triangle = SDL_SCANCODE_Z;
	s_keyboardMapping.kc_cross = SDL_SCANCODE_C;

	s_keyboardMapping.kc_l1 = SDL_SCANCODE_LSHIFT;
	s_keyboardMapping.kc_l2 = SDL_SCANCODE_LCTRL;
	s_keyboardMapping.kc_l3 = SDL_SCANCODE_LEFTBRACKET;

	s_keyboardMapping.kc_r1 = SDL_SCANCODE_RSHIFT;
	s_keyboardMapping.kc_r2 = SDL_SCANCODE_RCTRL;
	s_keyboardMapping.kc_r3 = SDL_SCANCODE_RIGHTBRACKET;

	s_keyboardMapping.kc_dpad_up = SDL_SCANCODE_UP;
	s_keyboardMapping.kc_dpad_down = SDL_SCANCODE_DOWN;
	s_keyboardMapping.kc_dpad_left = SDL_SCANCODE_LEFT;
	s_keyboardMapping.kc_dpad_right = SDL_SCANCODE_RIGHT;

	s_keyboardMapping.kc_select = SDL_SCANCODE_SPACE;
	s_keyboardMapping.kc_start = SDL_SCANCODE_RETURN;

	s_controllerMapping.gc_square = SDL_GAMEPAD_BUTTON_WEST;
	s_controllerMapping.gc_circle = SDL_GAMEPAD_BUTTON_EAST;
	s_controllerMapping.gc_triangle = SDL_GAMEPAD_BUTTON_NORTH;
	s_controllerMapping.gc_cross = SDL_GAMEPAD_BUTTON_SOUTH;

	s_controllerMapping.gc_l1 = SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
	s_controllerMapping.gc_l2 = SDL_GAMEPAD_AXIS_LEFT_TRIGGER | NATIVE_INPUT_MAP_FLAG_AXIS;
	s_controllerMapping.gc_l3 = SDL_GAMEPAD_BUTTON_LEFT_STICK;

	s_controllerMapping.gc_r1 = SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
	s_controllerMapping.gc_r2 = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER | NATIVE_INPUT_MAP_FLAG_AXIS;
	s_controllerMapping.gc_r3 = SDL_GAMEPAD_BUTTON_RIGHT_STICK;

	s_controllerMapping.gc_dpad_up = SDL_GAMEPAD_BUTTON_DPAD_UP;
	s_controllerMapping.gc_dpad_down = SDL_GAMEPAD_BUTTON_DPAD_DOWN;
	s_controllerMapping.gc_dpad_left = SDL_GAMEPAD_BUTTON_DPAD_LEFT;
	s_controllerMapping.gc_dpad_right = SDL_GAMEPAD_BUTTON_DPAD_RIGHT;

	s_controllerMapping.gc_select = SDL_GAMEPAD_BUTTON_BACK;
	s_controllerMapping.gc_start = SDL_GAMEPAD_BUTTON_START;

	s_controllerMapping.gc_axis_left_x = SDL_GAMEPAD_AXIS_LEFTX | NATIVE_INPUT_MAP_FLAG_AXIS;
	s_controllerMapping.gc_axis_left_y = SDL_GAMEPAD_AXIS_LEFTY | NATIVE_INPUT_MAP_FLAG_AXIS;
	s_controllerMapping.gc_axis_right_x = SDL_GAMEPAD_AXIS_RIGHTX | NATIVE_INPUT_MAP_FLAG_AXIS;
	s_controllerMapping.gc_axis_right_y = SDL_GAMEPAD_AXIS_RIGHTY | NATIVE_INPUT_MAP_FLAG_AXIS;
}

internal s32 NativeInput_ControllerButtonState(SDL_Gamepad *controller, s32 buttonOrAxis)
{
	if (controller == NULL)
	{
		return 0;
	}

	if ((buttonOrAxis & NATIVE_INPUT_MAP_FLAG_AXIS) != 0)
	{
		s32 axis = buttonOrAxis & ~(NATIVE_INPUT_MAP_FLAG_AXIS | NATIVE_INPUT_MAP_FLAG_INVERSE);
		s32 value = SDL_GetGamepadAxis(controller, (SDL_GamepadAxis)axis);

		if ((abs(value) > NATIVE_INPUT_AXIS_DEADZONE) && ((buttonOrAxis & NATIVE_INPUT_MAP_FLAG_INVERSE) != 0))
		{
			value *= -1;
		}

		return value;
	}

	if (buttonOrAxis < 0)
	{
		return 0;
	}

	return SDL_GetGamepadButton(controller, (SDL_GamepadButton)buttonOrAxis) * 32767;
}

internal s32 NativeInput_HasDevice(const struct NativeInputController *controller)
{
	return (controller->controller != NULL) || (controller->joystick != NULL);
}

internal u8 NativeInput_AxisToByte(s32 axis)
{
	s32 value = (axis / 256) + 128;

	if (value < 0)
	{
		return 0;
	}

	if (value > 0xff)
	{
		return 0xff;
	}

	return (u8)value;
}

internal s32 NativeInput_AxisIsActive(s32 axis)
{
	return abs(axis) > NATIVE_INPUT_AXIS_DEADZONE;
}

/* Maps the SDL gamepad of host slot `slot` onto *snapshot; analogEnabled
 * selects the reported id. A SELECT+START chord is always suppressed to
 * 0xffff. It also toggles *toggleAnalogEnabled (edge-tracked through
 * *toggleSwitchingAnalog) only when both pointers are non-NULL, and activity
 * records `slot` in *lastActiveSlot only when that pointer is non-NULL.
 * Platform_InputUpdate passes the slot's own state; the local sample passes
 * NULL for all three, so it writes only *snapshot (LR-37, LR-39). */
internal void NativeInput_ApplyController(
	SDL_Gamepad *controller,
	s32 analogEnabled,
	s32 slot,
	struct PlatformInputPadSnapshot *snapshot,
	s32 *toggleAnalogEnabled,
	s32 *toggleSwitchingAnalog,
	s32 *lastActiveSlot)
{
	const struct NativeInputControllerMapping *mapping = &s_controllerMapping;
	u16 buttons = 0xffff;

	if ((controller == NULL) || (SDL_GamepadConnected(controller) == 0))
	{
		return;
	}

	snapshot->connected = 1;
	snapshot->status = 0;
	snapshot->id = analogEnabled ? NATIVE_INPUT_PAD_ANALOG : NATIVE_INPUT_PAD_DIGITAL;

	if (NativeInput_ControllerButtonState(controller, mapping->gc_square) > 16384)
	{
		buttons &= ~0x8000;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_circle) > 16384)
	{
		buttons &= ~0x2000;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_triangle) > 16384)
	{
		buttons &= ~0x1000;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_cross) > 16384)
	{
		buttons &= ~0x4000;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_l1) > 16384)
	{
		buttons &= ~0x400;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_r1) > 16384)
	{
		buttons &= ~0x800;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_l2) > 16384)
	{
		buttons &= ~0x100;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_r2) > 16384)
	{
		buttons &= ~0x200;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_dpad_up) > 16384)
	{
		buttons &= ~0x10;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_dpad_down) > 16384)
	{
		buttons &= ~0x40;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_dpad_left) > 16384)
	{
		buttons &= ~0x80;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_dpad_right) > 16384)
	{
		buttons &= ~0x20;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_l3) > 16384)
	{
		buttons &= ~0x2;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_r3) > 16384)
	{
		buttons &= ~0x4;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_select) > 16384)
	{
		buttons &= ~0x1;
	}
	if (NativeInput_ControllerButtonState(controller, mapping->gc_start) > 16384)
	{
		buttons &= ~0x8;
	}

	s32 rightX = NativeInput_ControllerButtonState(controller, mapping->gc_axis_right_x);
	s32 rightY = NativeInput_ControllerButtonState(controller, mapping->gc_axis_right_y);
	s32 leftX = NativeInput_ControllerButtonState(controller, mapping->gc_axis_left_x);
	s32 leftY = NativeInput_ControllerButtonState(controller, mapping->gc_axis_left_y);

	if ((buttons != 0xffff) || NativeInput_AxisIsActive(rightX) || NativeInput_AxisIsActive(rightY) || NativeInput_AxisIsActive(leftX) ||
	    NativeInput_AxisIsActive(leftY))
	{
		if (lastActiveSlot != NULL)
		{
			*lastActiveSlot = slot;
		}
	}

	s32 toggle = (toggleAnalogEnabled != NULL) && (toggleSwitchingAnalog != NULL);
	if (((buttons & 0x1) == 0) && ((buttons & 0x8) == 0))
	{
		buttons = 0xffff;
		if (toggle)
		{
			if (*toggleSwitchingAnalog == 0)
			{
				*toggleAnalogEnabled = *toggleAnalogEnabled == 0;
			}
			*toggleSwitchingAnalog = 1;
		}
	}
	else if (toggle)
	{
		*toggleSwitchingAnalog = 0;
	}

	NativeInput_SetSnapshotButtons(snapshot, buttons);
	snapshot->analog[0] = NativeInput_AxisToByte(rightX);
	snapshot->analog[1] = NativeInput_AxisToByte(rightY);
	snapshot->analog[2] = NativeInput_AxisToByte(leftX);
	snapshot->analog[3] = NativeInput_AxisToByte(leftY);
}

internal s32 NativeInput_G29DiagnosticsEnabled(void)
{
	const char *value = SDL_getenv(NATIVE_INPUT_G29_DIAGNOSTIC_ENV);
	return (value != NULL) && (value[0] != '\0') && !((value[0] == '0') && (value[1] == '\0'));
}

internal s32 NativeInput_G29DiagnosticAxisChanged(s32 current, s32 previous)
{
	s32 difference = current - previous;
	if (difference < 0)
	{
		difference = -difference;
	}
	return difference >= NATIVE_INPUT_G29_DIAGNOSTIC_DELTA;
}

internal void NativeInput_LogG29Diagnostic(
	const char *reason,
	SDL_Joystick *joystick,
	const struct NativeG29RawInput *raw,
	const struct NativeG29MappingState *mappingState,
	const struct NativeG29MappedInput *mapped)
{
	struct NativeInputG29Diagnostic current;
	s32 reportedAxisCount;
	s32 capturedAxisCount;
	s32 changed;

	if (!NativeInput_G29DiagnosticsEnabled() || (joystick == NULL) || (raw == NULL) ||
	    (mappingState == NULL) || (mapped == NULL))
	{
		return;
	}

	memset(&current, 0, sizeof(current));
	reportedAxisCount = SDL_GetNumJoystickAxes(joystick);
	current.axisCount = reportedAxisCount;
	capturedAxisCount = reportedAxisCount;
	if (capturedAxisCount < 0)
	{
		capturedAxisCount = 0;
	}
	if (capturedAxisCount > NATIVE_INPUT_G29_DIAGNOSTIC_AXES)
	{
		capturedAxisCount = NATIVE_INPUT_G29_DIAGNOSTIC_AXES;
	}
	for (s32 axis = 0; axis < capturedAxisCount; axis++)
	{
		current.axes[axis] = SDL_GetJoystickAxis(joystick, axis);
	}
	current.mappingState = *mappingState;
	current.buttons = mapped->buttons;

	changed = !s_g29Diagnostic.valid || (current.axisCount != s_g29Diagnostic.axisCount) ||
	          (current.mappingState.throttleAwake != s_g29Diagnostic.mappingState.throttleAwake) ||
	          (current.mappingState.brakeAwake != s_g29Diagnostic.mappingState.brakeAwake) ||
	          (current.mappingState.throttlePressed != s_g29Diagnostic.mappingState.throttlePressed) ||
	          (current.mappingState.brakePressed != s_g29Diagnostic.mappingState.brakePressed) ||
	          (current.buttons != s_g29Diagnostic.buttons);
	for (s32 axis = 0; axis < capturedAxisCount; axis++)
	{
		if (NativeInput_G29DiagnosticAxisChanged(current.axes[axis], s_g29Diagnostic.axes[axis]))
		{
			changed = 1;
		}
	}

	if (!changed)
	{
		return;
	}

	fprintf(stderr,
	        "[CTR Native] G29 diagnostic (%s): axes=%d captured=%d [",
	        reason,
	        (int)reportedAxisCount,
	        (int)capturedAxisCount);
	for (s32 axis = 0; axis < capturedAxisCount; axis++)
	{
		fprintf(stderr, "%s%d", axis == 0 ? "" : ",", (int)current.axes[axis]);
	}
	if (reportedAxisCount > capturedAxisCount)
	{
		fprintf(stderr, ",...");
	}
	fprintf(stderr,
	        "] throttle(axis%d)=%d brake(axis%d)=%d awake=%u/%u pressed=%u/%u ps1=0x%04x\n",
	        NATIVE_G29_THROTTLE_AXIS,
	        (int)raw->axes[NATIVE_G29_THROTTLE_AXIS],
	        NATIVE_G29_BRAKE_AXIS,
	        (int)raw->axes[NATIVE_G29_BRAKE_AXIS],
	        (unsigned int)current.mappingState.throttleAwake,
	        (unsigned int)current.mappingState.brakeAwake,
	        (unsigned int)current.mappingState.throttlePressed,
	        (unsigned int)current.mappingState.brakePressed,
	        (unsigned int)current.buttons);

	s_g29Diagnostic = current;
	s_g29Diagnostic.valid = 1;
}

/* Maps the direct G29 joystick of host slot `slot` onto *snapshot, advancing
 * the pedal hysteresis in *g29State. Activity records `slot` in
 * *lastActiveSlot only when that pointer is non-NULL, and the G29 diagnostic
 * is logged (and its host-only state updated) only when logDiagnostic is
 * nonzero. Platform_InputUpdate passes the slot's own state; the local
 * sample passes its sample-only hysteresis state, NULL, and 0 (LR-37). */
internal void NativeInput_ApplyG29(
	SDL_Joystick *joystick,
	s32 slot,
	struct NativeG29MappingState *g29State,
	struct PlatformInputPadSnapshot *snapshot,
	s32 *lastActiveSlot,
	s32 logDiagnostic)
{
	struct NativeG29RawInput raw;
	struct NativeG29MappedInput mapped;

	if ((joystick == NULL) || (SDL_JoystickConnected(joystick) == false))
	{
		return;
	}

	memset(&raw, 0, sizeof(raw));
	for (s32 axis = 0; axis < NATIVE_G29_AXIS_COUNT; axis++)
	{
		raw.axes[axis] = SDL_GetJoystickAxis(joystick, axis);
	}
	for (s32 button = 0; button < NATIVE_G29_BUTTON_COUNT; button++)
	{
		raw.buttons[button] = SDL_GetJoystickButton(joystick, button) ? 1u : 0u;
	}
	raw.hat = SDL_GetJoystickHat(joystick, 0);

	NativeG29Input_Map(&raw, g29State, &mapped);
	if (logDiagnostic != 0)
	{
		NativeInput_LogG29Diagnostic("change", joystick, &raw, g29State, &mapped);
	}
	snapshot->connected = 1;
	snapshot->status = 0;
	snapshot->id = NATIVE_INPUT_PAD_ANALOG;
	NativeInput_SetSnapshotButtons(snapshot, mapped.buttons);
	snapshot->analog[0] = 0x80;
	snapshot->analog[1] = 0x80;
	snapshot->analog[2] = NativeInput_AxisToByte(mapped.steering);
	snapshot->analog[3] = 0x80;

	if ((mapped.active != 0u) && (lastActiveSlot != NULL))
	{
		*lastActiveSlot = slot;
	}
}

internal u16 NativeInput_ReadKeyboard(void)
{
	const struct NativeInputKeyboardMapping *mapping = &s_keyboardMapping;
	u16 buttons = 0xffff;

	if (s_keyboardState == NULL)
	{
		return buttons;
	}

	if (s_keyboardState[mapping->kc_square])
	{
		buttons &= ~0x8000;
	}
	if (s_keyboardState[mapping->kc_circle])
	{
		buttons &= ~0x2000;
	}
	if (s_keyboardState[mapping->kc_triangle])
	{
		buttons &= ~0x1000;
	}
	if (s_keyboardState[mapping->kc_cross])
	{
		buttons &= ~0x4000;
	}
	if (s_keyboardState[mapping->kc_l1])
	{
		buttons &= ~0x400;
	}
	if (s_keyboardState[mapping->kc_l2])
	{
		buttons &= ~0x100;
	}
	if (s_keyboardState[mapping->kc_l3])
	{
		buttons &= ~0x2;
	}
	if (s_keyboardState[mapping->kc_r1])
	{
		buttons &= ~0x800;
	}
	if (s_keyboardState[mapping->kc_r2])
	{
		buttons &= ~0x200;
	}
	if (s_keyboardState[mapping->kc_r3])
	{
		buttons &= ~0x4;
	}
	if (s_keyboardState[mapping->kc_dpad_up])
	{
		buttons &= ~0x10;
	}
	if (s_keyboardState[mapping->kc_dpad_down])
	{
		buttons &= ~0x40;
	}
	if (s_keyboardState[mapping->kc_dpad_left])
	{
		buttons &= ~0x80;
	}
	if (s_keyboardState[mapping->kc_dpad_right])
	{
		buttons &= ~0x20;
	}
	if (s_keyboardState[mapping->kc_select])
	{
		buttons &= ~0x1;
	}
	if (s_keyboardState[mapping->kc_start])
	{
		buttons &= ~0x8;
	}

	return buttons;
}

internal s32 NativeInput_KeyboardSuppressed(void)
{
	if (s_keyboardState == NULL)
	{
		return 0;
	}

	return s_keyboardState[SDL_SCANCODE_RALT] || s_keyboardState[SDL_SCANCODE_LALT];
}

/* ANDs the keyboard buttons into *snapshot when the keyboard is mapped to
 * host slot `slot`. Reads s_keyboardControllerSlot; writes only *snapshot. */
internal void NativeInput_ApplyKeyboard(s32 slot, u16 keyboardButtons, struct PlatformInputPadSnapshot *snapshot)
{
	if (slot != s_keyboardControllerSlot)
	{
		return;
	}

	if (snapshot->connected == 0)
	{
		snapshot->connected = 1;
		snapshot->status = 0;
		snapshot->id = NATIVE_INPUT_PAD_DIGITAL;
	}

	u16 buttons = NativeInput_GetSnapshotButtons(snapshot);
	NativeInput_SetSnapshotButtons(snapshot, buttons & keyboardButtons);
}

internal s32 NativeInput_FindActiveControllerSlot(void)
{
	if (NativeInput_IsValidControllerSlot(s_lastActiveControllerSlot) && NativeInput_HasDevice(&s_controllers[s_lastActiveControllerSlot]))
	{
		return s_lastActiveControllerSlot;
	}

	for (s32 slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		if (NativeInput_HasDevice(&s_controllers[slot]))
		{
			return slot;
		}
	}

	return -1;
}

internal void NativeInput_SwapControllerSlots(s32 slotA, s32 slotB)
{
	if (!NativeInput_IsValidControllerSlot(slotA) || !NativeInput_IsValidControllerSlot(slotB) || (slotA == slotB))
	{
		return;
	}

	struct NativeInputController controller = s_controllers[slotA];
	s_controllers[slotA] = s_controllers[slotB];
	s_controllers[slotB] = controller;
	if ((slotA == 0) || (slotB == 0))
	{
		/* Slot 0 now holds another device and its g29State (LR-37). */
		s_sampleG29Armed = 1;
	}

	s32 mapping = s_controllerToSlotMapping[slotA];
	s_controllerToSlotMapping[slotA] = s_controllerToSlotMapping[slotB];
	s_controllerToSlotMapping[slotB] = mapping;

	if (s_lastActiveControllerSlot == slotA)
	{
		s_lastActiveControllerSlot = slotB;
	}
	else if (s_lastActiveControllerSlot == slotB)
	{
		s_lastActiveControllerSlot = slotA;
	}
}

internal s32 NativeInput_FindSlotForDeviceIndex(Sint32 deviceIndex)
{
	s32 slot;

	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		if (s_controllerToSlotMapping[slot] == deviceIndex)
		{
			return slot;
		}
	}

	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		if ((s_controllerToSlotMapping[slot] < 0) && !NativeInput_HasDevice(&s_controllers[slot]))
		{
			return slot;
		}
	}

	return -1;
}

internal void NativeInput_CloseController(s32 slot)
{
	if ((slot < 0) || (slot >= NATIVE_INPUT_MAX_CONTROLLERS))
	{
		return;
	}

	struct NativeInputController *controller = &s_controllers[slot];
	if (controller->controller != NULL)
	{
		SDL_CloseGamepad(controller->controller);
	}
	if (controller->joystick != NULL)
	{
		if (controller->instanceId == s_directG29InstanceId)
		{
			s_directG29InstanceId = -1;
			s_g29Diagnostic.valid = 0;
		}
		SDL_CloseJoystick(controller->joystick);
	}

	controller->controller = NULL;
	controller->joystick = NULL;
	controller->instanceId = -1;
	memset(&controller->g29State, 0, sizeof(controller->g29State));
	controller->analogEnabled = 0;
	controller->switchingAnalog = 0;
	s_controllerToSlotMapping[slot] = -1;
	if (slot == 0)
	{
		/* Slot 0's g29State was just reset (LR-37). */
		s_sampleG29Armed = 1;
	}

	if (s_lastActiveControllerSlot == slot)
	{
		s_lastActiveControllerSlot = -1;
	}
}

internal void NativeInput_OpenController(SDL_JoystickID instanceId, s32 slot)
{
	struct NativeInputController *controller;
	if ((slot < 0) || (slot >= NATIVE_INPUT_MAX_CONTROLLERS))
	{
		return;
	}

	controller = &s_controllers[slot];
	if (NativeInput_HasDevice(controller))
	{
		return;
	}
	if (slot == 0)
	{
		/* A new slot-0 device starts from a reset g29State (LR-37). */
		s_sampleG29Armed = 1;
	}

	/* SDL's CAB1 controller database may classify the G29 as a Gamepad.  The
	 * G29 nevertheless needs the measured raw-joystick path: its Options
	 * control is direct button 24, outside the ordinary gamepad assumption.
	 * Identify and claim the one supported wheel before accepting generic SDL
	 * gamepads, while every non-G29 retains the existing path. */
	Uint16 vendor = SDL_GetJoystickVendorForID(instanceId);
	Uint16 product = SDL_GetJoystickProductForID(instanceId);
	enum NativeG29DeviceMatch match = NativeG29Input_MatchDevice(vendor, product, SDL_GetJoystickNameForID(instanceId));
	if (!NativeG29Input_ShouldUseDirect(match, SDL_IsGamepad(instanceId)) && SDL_IsGamepad(instanceId))
	{
		controller->controller = SDL_OpenGamepad(instanceId);
		if (controller->controller == NULL)
		{
			fprintf(stderr, "[CTR Native] SDL Gamepad open failed for instance %u: %s\n", (unsigned int)instanceId, SDL_GetError());
			return;
		}

		SDL_Joystick *joystick = SDL_GetGamepadJoystick(controller->controller);
		controller->instanceId = joystick != NULL ? SDL_GetJoystickID(joystick) : instanceId;
		controller->analogEnabled = 1;
		controller->switchingAnalog = 0;
		s_controllerToSlotMapping[slot] = (s32)controller->instanceId;
		NativeInput_MoveKeyboardOffControllerSlot(slot);
		fprintf(stderr, "[CTR Native] Input slot %d opened SDL Gamepad instance %u\n", slot + 1, (unsigned int)controller->instanceId);
		return;
	}

	if (match == NATIVE_G29_DEVICE_NO_MATCH)
	{
		return;
	}
	enum NativeG29DeviceClaim claim = NativeG29Input_CheckClaim((s32)s_directG29InstanceId, (s32)instanceId);
	if (claim != NATIVE_G29_DEVICE_CLAIM_AVAILABLE)
	{
		if (claim == NATIVE_G29_DEVICE_CLAIM_DUPLICATE)
		{
			fprintf(stderr,
			        "[CTR Native] G29 instance %u rejected: direct G29 already bound as instance %u\n",
			        (unsigned int)instanceId,
			        (unsigned int)s_directG29InstanceId);
		}
		return;
	}

	controller->joystick = SDL_OpenJoystick(instanceId);
	if (controller->joystick == NULL)
	{
		fprintf(stderr, "[CTR Native] G29 joystick open failed for instance %u: %s\n", (unsigned int)instanceId, SDL_GetError());
		return;
	}

	if ((SDL_GetNumJoystickAxes(controller->joystick) < NATIVE_G29_AXIS_COUNT) ||
	    (SDL_GetNumJoystickButtons(controller->joystick) < NATIVE_G29_BUTTON_COUNT) ||
	    (SDL_GetNumJoystickHats(controller->joystick) < 1))
	{
		fprintf(stderr, "[CTR Native] G29 instance %u rejected: insufficient axes/buttons/hats\n", (unsigned int)instanceId);
		SDL_CloseJoystick(controller->joystick);
		controller->joystick = NULL;
		return;
	}

	controller->instanceId = SDL_GetJoystickID(controller->joystick);
	s_directG29InstanceId = controller->instanceId;
	memset(&controller->g29State, 0, sizeof(controller->g29State));
	s_g29Diagnostic.valid = 0;
	if (NativeInput_G29DiagnosticsEnabled())
	{
		struct NativeG29RawInput raw;
		struct NativeG29MappingState mappingState = controller->g29State;
		struct NativeG29MappedInput mapped;

		memset(&raw, 0, sizeof(raw));
		for (s32 axis = 0; axis < NATIVE_G29_AXIS_COUNT; axis++)
		{
			raw.axes[axis] = SDL_GetJoystickAxis(controller->joystick, axis);
		}
		for (s32 button = 0; button < NATIVE_G29_BUTTON_COUNT; button++)
		{
			raw.buttons[button] = SDL_GetJoystickButton(controller->joystick, button) ? 1u : 0u;
		}
		raw.hat = SDL_GetJoystickHat(controller->joystick, 0);
		NativeG29Input_Map(&raw, &mappingState, &mapped);
		NativeInput_LogG29Diagnostic("open", controller->joystick, &raw, &mappingState, &mapped);
	}
	controller->analogEnabled = 1;
	controller->switchingAnalog = 0;
	s_controllerToSlotMapping[slot] = (s32)controller->instanceId;
	NativeInput_MoveKeyboardOffControllerSlot(slot);
	fprintf(stderr,
	        "[CTR Native] Input slot %d opened Logitech G29 instance %u (VID=%04x PID=%04x match=%s)\n",
	        slot + 1,
	        (unsigned int)controller->instanceId,
	        (unsigned int)vendor,
	        (unsigned int)product,
	        match == NATIVE_G29_DEVICE_VID_PID ? "VID/PID" : "name-fallback");
}

internal void NativeInput_OpenKnownControllers(void)
{
	s32 count = 0;

	SDL_JoystickID *gamepads = SDL_GetJoysticks(&count);
	for (s32 i = 0; i < count; i++)
	{
		s32 slot = NativeInput_FindSlotForDeviceIndex(gamepads[i]);

		if (slot >= 0)
		{
			NativeInput_OpenController(gamepads[i], slot);
		}
	}
	SDL_free(gamepads);
}

int Platform_InputInit(void)
{
	if (s_inputInitialized != 0)
	{
		return 1;
	}

	memset(s_controllers, 0, sizeof(s_controllers));
	memset(s_padSlotData, 0, sizeof(s_padSlotData));
	for (s32 slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		s_controllers[slot].instanceId = -1;
		s_controllerToSlotMapping[slot] = -1;
		NativeInput_ResetSnapshot(slot);
		s_installedSnapshots[slot] = s_controllers[slot].snapshot;
	}

	NativeInput_DefaultMappings();
	s_keyboardControllerSlot = NATIVE_INPUT_DEFAULT_KEYBOARD_SLOT;
	s_lastActiveControllerSlot = -1;
	s_directG29InstanceId = -1;
	memset(&s_g29Diagnostic, 0, sizeof(s_g29Diagnostic));
	s_installedSnapshotsActive = 0;
	s_sampleG29Armed = 1;
	s_keyboardState = SDL_GetKeyboardState(NULL);

	if (SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC) == 0)
	{
		fprintf(stderr, "[CTR Native] Failed to initialise SDL input subsystem: %s\n", SDL_GetError());
		return 0;
	}

	SDL_AddGamepadMappingsFromFile("gamecontrollerdb.txt");
	NativeInput_OpenKnownControllers();

	s_inputInitialized = 1;
	return 1;
}

void Platform_InputShutdown(void)
{
	for (s32 slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		NativeInput_CloseController(slot);
	}

	if (s_inputInitialized != 0)
	{
		SDL_QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC);
	}

	s_inputInitialized = 0;
	s_installedSnapshotsActive = 0;
	s_sampleG29Armed = 1;
	s_keyboardControllerSlot = NATIVE_INPUT_DEFAULT_KEYBOARD_SLOT;
	s_lastActiveControllerSlot = -1;
	s_directG29InstanceId = -1;
	memset(&s_g29Diagnostic, 0, sizeof(s_g29Diagnostic));
	memset(s_padSlotData, 0, sizeof(s_padSlotData));
	s_keyboardState = NULL;
}

void Platform_InputUpdate(void)
{
	if (s_inputInitialized == 0)
	{
		return;
	}

	if (s_installedSnapshotsActive != 0)
	{
		// NOTE(aalhendi): replay/state installs PSX-shaped pad bytes here;
		// SDL host state is not serialized.
		NativeInput_WriteInstalledSnapshots();
		return;
	}

	if (g_padCommEnable == 0)
	{
		return;
	}

	SDL_PumpEvents();
	u16 keyboardButtons = NativeInput_KeyboardSuppressed() ? 0xffff : NativeInput_ReadKeyboard();

	for (s32 slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		struct NativeInputController *controller = &s_controllers[slot];

		NativeInput_ResetSnapshot(slot);
		NativeInput_ApplyController(controller->controller,
		                            controller->analogEnabled,
		                            slot,
		                            &controller->snapshot,
		                            &controller->analogEnabled,
		                            &controller->switchingAnalog,
		                            &s_lastActiveControllerSlot);
		NativeInput_ApplyG29(controller->joystick, slot, &controller->g29State, &controller->snapshot, &s_lastActiveControllerSlot, 1);
		NativeInput_ApplyKeyboard(slot, keyboardButtons, &controller->snapshot);
	}
	NativeInput_WritePadBus();
}

/* LR-4 local sample (docs/LOCKSTEP_RACE_MILESTONE.md LR-37..LR-39): host
 * slot 0 built as Platform_InputUpdate builds it (the slot-0 reset, the
 * slot-0 gamepad, the slot-0 G29, then the keyboard when it is mapped to
 * slot 0 and Alt is not held), into caller storage only. It never writes
 * s_controllers[].snapshot, the pad bus, anything Platform_InputCaptureState
 * saves, or the G29 diagnostic state and log. It does not pump host events
 * and does not consult g_padCommEnable (LR-38). It never toggles analog mode:
 * a SELECT+START chord is suppressed without the toggle (LR-39). */
int Platform_InputSampleLocalPad(struct PlatformInputPadSnapshot *dst)
{
	struct PlatformInputPadSnapshot sample;
	const struct NativeInputController *controller = &s_controllers[0];

	if (dst == NULL)
	{
		return 0;
	}

	NativeInput_MakeResetSnapshot(0, &sample);
	if (s_inputInitialized == 0)
	{
		*dst = sample;
		return 0;
	}

	if (s_sampleG29Armed != 0)
	{
		s_sampleG29State = controller->g29State;
		s_sampleG29Armed = 0;
	}

	u16 keyboardButtons = NativeInput_KeyboardSuppressed() ? 0xffff : NativeInput_ReadKeyboard();
	NativeInput_ApplyController(controller->controller, controller->analogEnabled, 0, &sample, NULL, NULL, NULL);
	NativeInput_ApplyG29(controller->joystick, 0, &s_sampleG29State, &sample, NULL, 0);
	NativeInput_ApplyKeyboard(0, keyboardButtons, &sample);
	*dst = sample;
	return 1;
}

void Platform_InputControllerAdded(int deviceIndex)
{
	if (s_inputInitialized == 0)
	{
		return;
	}

	s32 slot = NativeInput_FindSlotForDeviceIndex(deviceIndex);
	if (slot >= 0)
	{
		NativeInput_OpenController(deviceIndex, slot);
	}
}

void Platform_InputControllerRemoved(int instanceId)
{
	for (s32 slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		if (s_controllers[slot].instanceId == (SDL_JoystickID)instanceId)
		{
			NativeInput_CloseController(slot);
			return;
		}
	}
}

int Platform_InputCycleKeyboardController(void)
{
	s_keyboardControllerSlot = NativeInput_NextControllerSlot(s_keyboardControllerSlot);
	return s_keyboardControllerSlot + 1;
}

int Platform_InputCycleGamepadController(void)
{
	s32 slot = NativeInput_FindActiveControllerSlot();

	if (slot < 0)
	{
		return 0;
	}

	s32 nextSlot = NativeInput_NextControllerSlot(slot);
	NativeInput_SwapControllerSlots(slot, nextSlot);
	NativeInput_WritePadBus();
	return nextSlot + 1;
}

void Platform_InputPadInit(int slot, unsigned char *padData)
{
	if ((slot < 0) || (slot >= NATIVE_INPUT_PHYSICAL_SLOT_COUNT))
	{
		return;
	}

	s_padSlotData[slot] = padData;
	NativeInput_WritePadBus();
}

int Platform_InputPadGetState(int port)
{
	s32 physicalSlot = (port >> 4) & 1;
	s32 tap = port & 3;
	s32 slot;

	if (NativeInput_UseMultitapBus() != 0)
	{
		if (physicalSlot != 0)
		{
			return PadStateDiscon;
		}
		slot = tap;
	}
	else
	{
		if (tap != 0)
		{
			return PadStateDiscon;
		}
		slot = physicalSlot;
	}

	if ((slot < 0) || (slot >= NATIVE_INPUT_MAX_CONTROLLERS))
	{
		return PadStateDiscon;
	}

	return s_controllers[slot].snapshot.connected ? PadStateStable : PadStateDiscon;
}

int Platform_InputCapturePadSnapshots(struct PlatformInputPadSnapshot *dst, int count)
{
	if ((dst == NULL) || (count < NATIVE_INPUT_MAX_CONTROLLERS))
	{
		return 0;
	}

	for (s32 slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		dst[slot] = s_controllers[slot].snapshot;
	}

	return NATIVE_INPUT_MAX_CONTROLLERS;
}

int Platform_InputInstallPadSnapshots(const struct PlatformInputPadSnapshot *src, int count)
{
	if ((src == NULL) || (count < NATIVE_INPUT_MAX_CONTROLLERS))
	{
		return 0;
	}

	for (s32 slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		s_installedSnapshots[slot] = src[slot];
	}

	if (s_installedSnapshotsActive == 0)
	{
		s_sampleG29Armed = 1;
	}
	s_installedSnapshotsActive = 1;
	NativeInput_WriteInstalledSnapshots();
	return NATIVE_INPUT_MAX_CONTROLLERS;
}

void Platform_InputClearInstalledPadSnapshots(void)
{
	s_installedSnapshotsActive = 0;
	s_sampleG29Armed = 1;
}

int Platform_InputGetStateSize(void)
{
	return (int)sizeof(struct NativeInputStateSnapshot);
}

int Platform_InputCaptureState(void *dst, int dstSize)
{
	struct NativeInputStateSnapshot *snapshot = (struct NativeInputStateSnapshot *)dst;

	if ((dst == NULL) || (dstSize < (int)sizeof(*snapshot)))
	{
		return 0;
	}

	memset(snapshot, 0, sizeof(*snapshot));
	snapshot->magic = NATIVE_INPUT_STATE_MAGIC;
	snapshot->version = NATIVE_INPUT_STATE_VERSION;
	snapshot->size = sizeof(*snapshot);
	snapshot->keyboardControllerSlot = s_keyboardControllerSlot;
	snapshot->lastActiveControllerSlot = s_lastActiveControllerSlot;
	snapshot->installedSnapshotsActive = s_installedSnapshotsActive;

	for (s32 slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		snapshot->installedSnapshots[slot] = s_installedSnapshots[slot];
		snapshot->controllers[slot].snapshot = s_controllers[slot].snapshot;
		snapshot->controllers[slot].g29State = s_controllers[slot].g29State;
		snapshot->controllers[slot].analogEnabled = s_controllers[slot].analogEnabled;
		snapshot->controllers[slot].switchingAnalog = s_controllers[slot].switchingAnalog;
		snapshot->controllers[slot].controllerToSlotMapping = s_controllerToSlotMapping[slot];
	}

	return 1;
}

int Platform_InputRestoreState(const void *src, int srcSize)
{
	const struct NativeInputStateSnapshot *snapshot = (const struct NativeInputStateSnapshot *)src;
	s32 slot;

	if ((src == NULL) || (srcSize < (int)sizeof(*snapshot)))
	{
		return 0;
	}
	if ((snapshot->magic != NATIVE_INPUT_STATE_MAGIC) || (snapshot->version != NATIVE_INPUT_STATE_VERSION) || (snapshot->size != sizeof(*snapshot)))
	{
		return 0;
	}
	if (!NativeInput_IsValidControllerSlot(snapshot->keyboardControllerSlot))
	{
		return 0;
	}
	if ((snapshot->lastActiveControllerSlot < -1) || (snapshot->lastActiveControllerSlot >= NATIVE_INPUT_MAX_CONTROLLERS))
	{
		return 0;
	}
	if ((snapshot->installedSnapshotsActive < 0) || (snapshot->installedSnapshotsActive > 1))
	{
		return 0;
	}
	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		if ((snapshot->controllers[slot].analogEnabled < 0) || (snapshot->controllers[slot].analogEnabled > 1))
		{
			return 0;
		}
		if ((snapshot->controllers[slot].switchingAnalog < 0) || (snapshot->controllers[slot].switchingAnalog > 1))
		{
			return 0;
		}
		if (!NativeG29Input_ValidateMappingState(&snapshot->controllers[slot].g29State))
		{
			return 0;
		}
		if (snapshot->controllers[slot].controllerToSlotMapping < -1)
		{
			return 0;
		}
	}

	s_keyboardControllerSlot = snapshot->keyboardControllerSlot;
	s_lastActiveControllerSlot = snapshot->lastActiveControllerSlot;
	s_installedSnapshotsActive = snapshot->installedSnapshotsActive != 0;

	for (slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		s_installedSnapshots[slot] = snapshot->installedSnapshots[slot];
		s_controllers[slot].snapshot = snapshot->controllers[slot].snapshot;
		s_controllers[slot].g29State = snapshot->controllers[slot].g29State;
		s_controllers[slot].analogEnabled = snapshot->controllers[slot].analogEnabled;
		s_controllers[slot].switchingAnalog = snapshot->controllers[slot].switchingAnalog;
		s_controllerToSlotMapping[slot] = snapshot->controllers[slot].controllerToSlotMapping;
	}
	/* Slot 0's g29State and the installed-pad flag were replaced (LR-37). */
	s_sampleG29Armed = 1;
	NativeInput_WritePadBus();

	return 1;
}

void Platform_InputPadVibrate(int port, unsigned char *table, int len)
{
	s32 physicalSlot = (port >> 4) & 1;
	s32 tap = port & 3;
	s32 slot;

	if (NativeInput_UseMultitapBus() != 0)
	{
		if (physicalSlot != 0)
		{
			return;
		}
		slot = tap;
	}
	else
	{
		if (tap != 0)
		{
			return;
		}
		slot = physicalSlot;
	}

	if ((slot < 0) || (slot >= NATIVE_INPUT_MAX_CONTROLLERS) || (table == NULL) || (len <= 0))
	{
		return;
	}

	struct NativeInputController *controller = &s_controllers[slot];
	if (!NativeInput_HasDevice(controller))
	{
		return;
	}

	u16 freqHigh = table[0] * 255;
	u16 freqLow = len > 1 ? table[1] * 255 : 0;

	if ((freqLow != 0) && (freqLow < 4096))
	{
		freqLow = 4096;
	}

	if ((freqHigh != 0) && (freqHigh < 4096))
	{
		freqHigh = 4096;
	}

	if (controller->controller != NULL)
	{
		SDL_RumbleGamepad(controller->controller, freqLow, freqHigh, 200);
	}
	/* Direct G29 force feedback remains a separate M6 hardware gate. */
}
