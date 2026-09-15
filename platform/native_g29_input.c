#include <platform/native_g29_input.h>

#include <stddef.h>
#include <string.h>

#define NATIVE_G29_HAT_UP       0x01u
#define NATIVE_G29_HAT_RIGHT    0x02u
#define NATIVE_G29_HAT_DOWN     0x04u
#define NATIVE_G29_HAT_LEFT     0x08u

#define NATIVE_G29_PSX_SELECT   0x0001u
#define NATIVE_G29_PSX_L3       0x0002u
#define NATIVE_G29_PSX_R3       0x0004u
#define NATIVE_G29_PSX_START    0x0008u
#define NATIVE_G29_PSX_UP       0x0010u
#define NATIVE_G29_PSX_RIGHT    0x0020u
#define NATIVE_G29_PSX_DOWN     0x0040u
#define NATIVE_G29_PSX_LEFT     0x0080u
#define NATIVE_G29_PSX_L2       0x0100u
#define NATIVE_G29_PSX_R2       0x0200u
#define NATIVE_G29_PSX_L1       0x0400u
#define NATIVE_G29_PSX_R1       0x0800u
#define NATIVE_G29_PSX_TRIANGLE 0x1000u
#define NATIVE_G29_PSX_CIRCLE   0x2000u
#define NATIVE_G29_PSX_CROSS    0x4000u
#define NATIVE_G29_PSX_SQUARE   0x8000u

static int NativeG29Input_AsciiLower(int value)
{
	if ((value >= 'A') && (value <= 'Z'))
	{
		return value + ('a' - 'A');
	}
	return value;
}

static int NativeG29Input_NameContainsG29(const char *name)
{
	if (name == NULL)
	{
		return 0;
	}

	for (const char *cursor = name; (cursor[0] != '\0') && (cursor[1] != '\0') && (cursor[2] != '\0'); cursor++)
	{
		if ((NativeG29Input_AsciiLower((unsigned char)cursor[0]) == 'g') &&
		    (cursor[1] == '2') && (cursor[2] == '9'))
		{
			return 1;
		}
	}
	return 0;
}

enum NativeG29DeviceMatch NativeG29Input_MatchDevice(uint16_t vendor, uint16_t product, const char *name)
{
	if ((vendor == NATIVE_G29_VENDOR_ID) && (product == NATIVE_G29_PRODUCT_ID))
	{
		return NATIVE_G29_DEVICE_VID_PID;
	}

	/* Some Windows HID stacks omit VID/PID from SDL's device record.  Permit
	 * the measured G29 name only when one of those IDs is absent and every ID
	 * SDL did supply agrees; never trust the name over a mismatched ID. */
	if (((vendor == 0u) || (vendor == NATIVE_G29_VENDOR_ID)) &&
	    ((product == 0u) || (product == NATIVE_G29_PRODUCT_ID)) &&
	    ((vendor == 0u) || (product == 0u)) &&
	    NativeG29Input_NameContainsG29(name))
	{
		return NATIVE_G29_DEVICE_NAME_FALLBACK;
	}

	return NATIVE_G29_DEVICE_NO_MATCH;
}

enum NativeG29DeviceClaim NativeG29Input_CheckClaim(int32_t selectedInstanceId, int32_t candidateInstanceId)
{
	if (selectedInstanceId < 0)
	{
		return NATIVE_G29_DEVICE_CLAIM_AVAILABLE;
	}
	if (selectedInstanceId == candidateInstanceId)
	{
		return NATIVE_G29_DEVICE_CLAIM_SAME_INSTANCE;
	}
	return NATIVE_G29_DEVICE_CLAIM_DUPLICATE;
}

int NativeG29Input_ShouldUseDirect(enum NativeG29DeviceMatch match, int isSdlGamepad)
{
	/* A matched G29 always uses its direct layout.  SDL's Gamepad database
	 * classification does not describe CAB1's Options-at-24 enumeration. */
	(void)isSdlGamepad;
	return match != NATIVE_G29_DEVICE_NO_MATCH;
}

int NativeG29Input_ValidateMappingState(const struct NativeG29MappingState *state)
{
	return (state != NULL) &&
	       (state->throttleAwake <= 1u) &&
	       (state->brakeAwake <= 1u) &&
	       (state->throttlePressed <= 1u) &&
	       (state->brakePressed <= 1u);
}

int16_t NativeG29Input_ShapeSteering(int16_t raw)
{
	int32_t value = raw;
	int32_t magnitude = value < 0 ? -value : value;
	int32_t maximum = value < 0 ? 32768 : 32767;

	if (magnitude <= NATIVE_G29_STEERING_DEADZONE)
	{
		return 0;
	}

	magnitude = ((magnitude - NATIVE_G29_STEERING_DEADZONE) * maximum) /
	            (maximum - NATIVE_G29_STEERING_DEADZONE);
	return (int16_t)(value < 0 ? -magnitude : magnitude);
}

static void NativeG29Input_UpdatePedal(
	int16_t raw,
	uint8_t *awake,
	uint8_t *pressed)
{
	int32_t magnitude = raw < 0 ? -(int32_t)raw : raw;
	if (magnitude > NATIVE_G29_PEDAL_WAKE_THRESHOLD)
	{
		*awake = 1;
	}

	if (*awake == 0)
	{
		*pressed = 0;
	}
	else if (raw < NATIVE_G29_PEDAL_PRESS_THRESHOLD)
	{
		*pressed = 1;
	}
	else if (raw > NATIVE_G29_PEDAL_RELEASE_THRESHOLD)
	{
		*pressed = 0;
	}
}

void NativeG29Input_Map(
	const struct NativeG29RawInput *raw,
	struct NativeG29MappingState *state,
	struct NativeG29MappedInput *mapped)
{
	uint16_t buttons = 0xffffu;
	if ((raw == NULL) || (state == NULL) || (mapped == NULL))
	{
		return;
	}

	NativeG29Input_UpdatePedal(raw->axes[NATIVE_G29_THROTTLE_AXIS], &state->throttleAwake, &state->throttlePressed);
	NativeG29Input_UpdatePedal(raw->axes[NATIVE_G29_BRAKE_AXIS], &state->brakeAwake, &state->brakePressed);

	/* CAB1's SDL mapping enumerates Options at button 24.  Button 9 is
	 * Share/Select, not Start.  The PS1 pad packet remains active-low. */
	if ((state->throttlePressed != 0u) || (raw->buttons[NATIVE_G29_BUTTON_CROSS] != 0u)) buttons &= (uint16_t)~NATIVE_G29_PSX_CROSS;
	if ((state->brakePressed != 0u) || (raw->buttons[NATIVE_G29_BUTTON_SQUARE] != 0u)) buttons &= (uint16_t)~NATIVE_G29_PSX_SQUARE;
	if (raw->buttons[NATIVE_G29_BUTTON_CIRCLE] != 0u) buttons &= (uint16_t)~NATIVE_G29_PSX_CIRCLE;
	if (raw->buttons[NATIVE_G29_BUTTON_TRIANGLE] != 0u) buttons &= (uint16_t)~NATIVE_G29_PSX_TRIANGLE;
	if (raw->buttons[NATIVE_G29_BUTTON_R2] != 0u) buttons &= (uint16_t)~NATIVE_G29_PSX_R2;
	if (raw->buttons[NATIVE_G29_BUTTON_L2] != 0u) buttons &= (uint16_t)~NATIVE_G29_PSX_L2;
	if (raw->buttons[NATIVE_G29_BUTTON_R1] != 0u) buttons &= (uint16_t)~NATIVE_G29_PSX_R1;
	if (raw->buttons[NATIVE_G29_BUTTON_L1] != 0u) buttons &= (uint16_t)~NATIVE_G29_PSX_L1;
	if (raw->buttons[NATIVE_G29_BUTTON_SHARE] != 0u) buttons &= (uint16_t)~NATIVE_G29_PSX_SELECT;
	if (raw->buttons[NATIVE_G29_BUTTON_OPTIONS] != 0u) buttons &= (uint16_t)~NATIVE_G29_PSX_START;

	if ((raw->hat & NATIVE_G29_HAT_UP) != 0u) buttons &= (uint16_t)~NATIVE_G29_PSX_UP;
	if ((raw->hat & NATIVE_G29_HAT_RIGHT) != 0u) buttons &= (uint16_t)~NATIVE_G29_PSX_RIGHT;
	if ((raw->hat & NATIVE_G29_HAT_DOWN) != 0u) buttons &= (uint16_t)~NATIVE_G29_PSX_DOWN;
	if ((raw->hat & NATIVE_G29_HAT_LEFT) != 0u) buttons &= (uint16_t)~NATIVE_G29_PSX_LEFT;

	mapped->buttons = buttons;
	mapped->steering = NativeG29Input_ShapeSteering(raw->axes[NATIVE_G29_STEERING_AXIS]);
	mapped->active = (uint8_t)((buttons != 0xffffu) || (mapped->steering != 0));
}
