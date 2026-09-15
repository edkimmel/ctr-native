#include <platform/native_g29_input.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #expression); return 1; } } while (0)

static void Map(
	struct NativeG29RawInput *raw,
	struct NativeG29MappingState *state,
	struct NativeG29MappedInput *mapped)
{
	memset(mapped, 0, sizeof(*mapped));
	NativeG29Input_Map(raw, state, mapped);
}

int main(void)
{
	struct NativeG29RawInput raw;
	struct NativeG29MappingState state;
	struct NativeG29MappedInput mapped;
	static const uint16_t hatMasks[4] = {0x0010u, 0x0020u, 0x0040u, 0x0080u};

	CHECK(NativeG29Input_MatchDevice(0x046d, 0xc24f, NULL) == NATIVE_G29_DEVICE_VID_PID);
	CHECK(NativeG29Input_MatchDevice(0, 0, "Logitech G HUB G29 Driving Force Racing Wheel USB") == NATIVE_G29_DEVICE_NAME_FALLBACK);
	CHECK(NativeG29Input_MatchDevice(0x046d, 0, "g29 Driving Force") == NATIVE_G29_DEVICE_NAME_FALLBACK);
	CHECK(NativeG29Input_MatchDevice(0, 0xc24f, "G29 Driving Force") == NATIVE_G29_DEVICE_NAME_FALLBACK);
	CHECK(NativeG29Input_MatchDevice(0x1234, 0, "G29 Driving Force") == NATIVE_G29_DEVICE_NO_MATCH);
	CHECK(NativeG29Input_MatchDevice(0, 0x5678, "G29 Driving Force") == NATIVE_G29_DEVICE_NO_MATCH);
	CHECK(NativeG29Input_MatchDevice(0x1234, 0x5678, "G29 impostor") == NATIVE_G29_DEVICE_NO_MATCH);
	CHECK(NativeG29Input_MatchDevice(0x046d, 0xc266, "G923") == NATIVE_G29_DEVICE_NO_MATCH);
	CHECK(NativeG29Input_MatchDevice(0, 0, NULL) == NATIVE_G29_DEVICE_NO_MATCH);
	CHECK(NativeG29Input_CheckClaim(-1, 17) == NATIVE_G29_DEVICE_CLAIM_AVAILABLE);
	CHECK(NativeG29Input_CheckClaim(17, 17) == NATIVE_G29_DEVICE_CLAIM_SAME_INSTANCE);
	CHECK(NativeG29Input_CheckClaim(17, 23) == NATIVE_G29_DEVICE_CLAIM_DUPLICATE);
	CHECK(NativeG29Input_ShouldUseDirect(NATIVE_G29_DEVICE_VID_PID, 0));
	CHECK(NativeG29Input_ShouldUseDirect(NATIVE_G29_DEVICE_VID_PID, 1));
	CHECK(NativeG29Input_ShouldUseDirect(NATIVE_G29_DEVICE_NAME_FALLBACK, 1));
	CHECK(!NativeG29Input_ShouldUseDirect(NATIVE_G29_DEVICE_NO_MATCH, 1));

	memset(&state, 0, sizeof(state));
	CHECK(NativeG29Input_ValidateMappingState(&state));
	CHECK(!NativeG29Input_ValidateMappingState(NULL));
	state.brakePressed = 2;
	CHECK(!NativeG29Input_ValidateMappingState(&state));
	memset(&state, 0, sizeof(state));

	CHECK(NativeG29Input_ShapeSteering(0) == 0);
	CHECK(NativeG29Input_ShapeSteering(NATIVE_G29_STEERING_DEADZONE) == 0);
	CHECK(NativeG29Input_ShapeSteering(-NATIVE_G29_STEERING_DEADZONE) == 0);
	CHECK(NativeG29Input_ShapeSteering(32767) == 32767);
	CHECK(NativeG29Input_ShapeSteering(-32768) == -32768);
	CHECK(NativeG29Input_ShapeSteering(16000) > 15000);
	CHECK(NativeG29Input_ShapeSteering(-16000) < -15000);
	CHECK(NATIVE_G29_STEERING_AXIS == 0);
	CHECK(NATIVE_G29_THROTTLE_AXIS == 1);
	CHECK(NATIVE_G29_BRAKE_AXIS == 2);

	memset(&raw, 0, sizeof(raw));
	memset(&state, 0, sizeof(state));
	Map(&raw, &state, &mapped);
	CHECK(mapped.buttons == 0xffffu && mapped.steering == 0 && mapped.active == 0);
	CHECK(state.throttleAwake == 0 && state.brakeAwake == 0);

	/* The G29 reports zero until its pedal axes wake. Zero must never create
	 * phantom half-pressed gas/brake inputs. Rest is +32767; press is -32768. */
	raw.axes[1] = 32767;
	raw.axes[2] = 32767;
	Map(&raw, &state, &mapped);
	CHECK(state.throttleAwake == 1 && state.brakeAwake == 1);
	CHECK(mapped.buttons == 0xffffu);

	raw.axes[1] = 22000;
	Map(&raw, &state, &mapped);
	CHECK((mapped.buttons & 0x4000u) == 0 && state.throttlePressed == 1);
	raw.axes[1] = 24000;
	Map(&raw, &state, &mapped);
	CHECK((mapped.buttons & 0x4000u) == 0); /* hysteresis hold */
	raw.axes[1] = 27000;
	Map(&raw, &state, &mapped);
	CHECK((mapped.buttons & 0x4000u) != 0 && state.throttlePressed == 0);

	raw.axes[2] = -32768;
	Map(&raw, &state, &mapped);
	CHECK((mapped.buttons & 0x8000u) == 0 && state.brakePressed == 1);
	raw.axes[2] = 32767;
	Map(&raw, &state, &mapped);
	CHECK((mapped.buttons & 0x8000u) != 0 && state.brakePressed == 0);

	memset(&raw, 0, sizeof(raw));
	raw.axes[1] = 32767;
	raw.axes[2] = 32767;
	/* CAB1's direct SDL enumeration reserves button 24 for Options.  This is
	 * the menu-start control; button 9 is Share/Select, not Start. */
	raw.buttons[NATIVE_G29_BUTTON_OPTIONS] = 1;
	Map(&raw, &state, &mapped);
	CHECK(mapped.buttons == 0xfff7u && mapped.active == 1);
	memset(raw.buttons, 0, sizeof(raw.buttons));
	raw.buttons[NATIVE_G29_BUTTON_SHARE] = 1;
	Map(&raw, &state, &mapped);
	CHECK(mapped.buttons == 0xfffeu && mapped.active == 1);
	memset(raw.buttons, 0, sizeof(raw.buttons));
	raw.buttons[10] = 1;
	Map(&raw, &state, &mapped);
	CHECK(mapped.buttons == 0xffffu && mapped.active == 0);
	static const int mappedButtons[] = {
		NATIVE_G29_BUTTON_CROSS, NATIVE_G29_BUTTON_SQUARE,
		NATIVE_G29_BUTTON_CIRCLE, NATIVE_G29_BUTTON_TRIANGLE,
		NATIVE_G29_BUTTON_PHYSICAL_RIGHT_PADDLE,
		NATIVE_G29_BUTTON_PHYSICAL_LEFT_PADDLE
	};
	static const uint16_t mappedMasks[] = {
		0x4000u, 0x8000u, 0x2000u, 0x1000u,
		0x2000u, 0x0800u
	};
	/* CAB1's physical paddles are raw buttons 4/5.  The right paddle is item
	 * only (not R2/rear view); the left paddle is drift only (not L2). */
	memset(raw.buttons, 0, sizeof(raw.buttons));
	raw.buttons[NATIVE_G29_BUTTON_PHYSICAL_RIGHT_PADDLE] = 1;
	Map(&raw, &state, &mapped);
	CHECK(mapped.buttons == 0xdfffu && mapped.active == 1);
	memset(raw.buttons, 0, sizeof(raw.buttons));
	raw.buttons[NATIVE_G29_BUTTON_CIRCLE] = 1;
	Map(&raw, &state, &mapped);
	CHECK(mapped.buttons == 0xdfffu && mapped.active == 1);
	memset(raw.buttons, 0, sizeof(raw.buttons));
	raw.buttons[NATIVE_G29_BUTTON_PHYSICAL_LEFT_PADDLE] = 1;
	Map(&raw, &state, &mapped);
	CHECK(mapped.buttons == 0xf7ffu && mapped.active == 1);
	memset(raw.buttons, 0, sizeof(raw.buttons));
	raw.buttons[6] = 1;
	raw.buttons[7] = 1;
	Map(&raw, &state, &mapped);
	CHECK(mapped.buttons == 0xffffu && mapped.active == 0);
	for (int index = 0; index < (int)(sizeof(mappedButtons) / sizeof(mappedButtons[0])); index++)
	{
		memset(raw.buttons, 0, sizeof(raw.buttons));
		raw.buttons[mappedButtons[index]] = 1;
		raw.hat = 0;
		Map(&raw, &state, &mapped);
		CHECK(mapped.buttons == (uint16_t)(0xffffu & ~mappedMasks[index]));
	}
	memset(raw.buttons, 0, sizeof(raw.buttons));
	for (int direction = 0; direction < 4; direction++)
	{
		raw.hat = (uint8_t)(1u << direction);
		Map(&raw, &state, &mapped);
		CHECK(mapped.buttons == (uint16_t)(0xffffu & ~hatMasks[direction]));
	}

	memset(raw.buttons, 1, sizeof(raw.buttons));
	raw.hat = 0x0fu;
	Map(&raw, &state, &mapped);
	CHECK(mapped.buttons == 0x0706u && mapped.active == 1);

	memset(&raw, 0, sizeof(raw));
	raw.axes[0] = 12000;
	raw.axes[1] = 32767;
	raw.axes[2] = 32767;
	Map(&raw, &state, &mapped);
	CHECK(mapped.steering > 0 && mapped.buttons == 0xffffu && mapped.active == 1);

	puts("native_g29_input_test: PASS");
	return 0;
}
