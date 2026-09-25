/*
 * native_input_sample_unit (docs/LOCKSTEP_RACE_MILESTONE.md LR-4, LR-S7,
 * LR-37..LR-39): Platform_InputSampleLocalPad reads host slot 0 into the
 * caller's snapshot only. platform/native_input.c is compiled into this test
 * as main.c compiles it into ctr_native, so the test reaches its statics.
 * SDL input is initialized headless and every host device is closed; SDL
 * virtual joysticks stand in for a gamepad and a G29, and a test-owned
 * array stands in for the keyboard.
 *
 * Each sample is checked against three observations taken before and after
 * it, which must be byte-identical: Platform_InputCapturePadSnapshots'
 * output, both pad-bus buffers registered with Platform_InputPadInit, and
 * Platform_InputCaptureState's bytes (plus the host-only G29 diagnostic).
 */
/* SDL first, as main.c includes it before the unity sources: macros.h
 * defines `internal`, which SDL's headers use as a field name. */
#include <SDL3/SDL.h>

#include "../platform/native_input.c"

#include <platform/native_sdl_assert.h>

#include <stdio.h>
#include <string.h>

s32 g_padCommEnable;

#define TEST_BUS_BYTES 64
#define TEST_BUS_SENTINEL 0xcd

static int s_failures;

#define CHECK(expression)                                                               \
	do                                                                                  \
	{                                                                                   \
		if (!(expression))                                                              \
		{                                                                               \
			fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #expression);   \
			s_failures++;                                                               \
			return 0;                                                                   \
		}                                                                               \
	} while (0)

static bool s_keys[SDL_SCANCODE_COUNT];
static u8 s_bus[NATIVE_INPUT_PHYSICAL_SLOT_COUNT][TEST_BUS_BYTES];

struct Observed
{
	struct PlatformInputPadSnapshot pads[PLATFORM_INPUT_PAD_COUNT];
	u8 bus[NATIVE_INPUT_PHYSICAL_SLOT_COUNT][TEST_BUS_BYTES];
	u8 state[sizeof(struct NativeInputStateSnapshot)];
	s32 diagnosticValid;
	s32 diagnosticAxisCount;
	int16_t diagnosticAxes[NATIVE_INPUT_G29_DIAGNOSTIC_AXES];
	struct NativeG29MappingState diagnosticMapping;
	uint16_t diagnosticButtons;
};

static int Observe(struct Observed *observed)
{
	memset(observed, 0, sizeof(*observed));
	CHECK(Platform_InputCapturePadSnapshots(observed->pads, PLATFORM_INPUT_PAD_COUNT) == PLATFORM_INPUT_PAD_COUNT);
	memcpy(observed->bus, s_bus, sizeof(s_bus));
	CHECK(Platform_InputGetStateSize() == (int)sizeof(observed->state));
	CHECK(Platform_InputCaptureState(observed->state, (int)sizeof(observed->state)) == 1);
	observed->diagnosticValid = s_g29Diagnostic.valid;
	observed->diagnosticAxisCount = s_g29Diagnostic.axisCount;
	memcpy(observed->diagnosticAxes, s_g29Diagnostic.axes, sizeof(observed->diagnosticAxes));
	observed->diagnosticMapping = s_g29Diagnostic.mappingState;
	observed->diagnosticButtons = s_g29Diagnostic.buttons;
	return 1;
}

static int SameObserved(const struct Observed *a, const struct Observed *b)
{
	CHECK(memcmp(a->pads, b->pads, sizeof(a->pads)) == 0);
	CHECK(memcmp(a->bus, b->bus, sizeof(a->bus)) == 0);
	CHECK(memcmp(a->state, b->state, sizeof(a->state)) == 0);
	CHECK(a->diagnosticValid == b->diagnosticValid);
	CHECK(a->diagnosticAxisCount == b->diagnosticAxisCount);
	CHECK(memcmp(a->diagnosticAxes, b->diagnosticAxes, sizeof(a->diagnosticAxes)) == 0);
	CHECK(memcmp(&a->diagnosticMapping, &b->diagnosticMapping, sizeof(a->diagnosticMapping)) == 0);
	CHECK(a->diagnosticButtons == b->diagnosticButtons);
	return 1;
}

/* Samples into dst with the three observations taken around the call; they
 * must not change. Returns 0 on any failure. */
static int SampleUntouched(struct PlatformInputPadSnapshot *dst)
{
	struct Observed before;
	struct Observed after;
	s32 lastActive = s_lastActiveControllerSlot;

	CHECK(Observe(&before));
	memset(dst, 0xa5, sizeof(*dst));
	CHECK(Platform_InputSampleLocalPad(dst) == 1);
	CHECK(Observe(&after));
	CHECK(SameObserved(&before, &after));
	CHECK(s_lastActiveControllerSlot == lastActive);
	return 1;
}

static u16 SnapshotButtons(const struct PlatformInputPadSnapshot *snapshot)
{
	return (u16)(snapshot->buttons[0] | (snapshot->buttons[1] << 8));
}

static int CheckSlot0Snapshot(const struct PlatformInputPadSnapshot *snapshot, u8 id, u16 buttons, u8 a0, u8 a1, u8 a2, u8 a3)
{
	static const u8 zero[sizeof(snapshot->reserved)] = {0};

	CHECK(snapshot->connected == 1);
	CHECK(snapshot->status == 0);
	CHECK(snapshot->id == id);
	CHECK(SnapshotButtons(snapshot) == buttons);
	CHECK(snapshot->analog[0] == a0);
	CHECK(snapshot->analog[1] == a1);
	CHECK(snapshot->analog[2] == a2);
	CHECK(snapshot->analog[3] == a3);
	CHECK(memcmp(snapshot->reserved, zero, sizeof(zero)) == 0);
	return 1;
}

static int CheckNeutral(const struct PlatformInputPadSnapshot *snapshot)
{
	return CheckSlot0Snapshot(snapshot, 0x41, 0xffff, 0x80, 0x80, 0x80, 0x80);
}

static void ClearKeys(void)
{
	memset(s_keys, 0, sizeof(s_keys));
}

static void MakeInstalledPads(struct PlatformInputPadSnapshot *pads)
{
	memset(pads, 0, sizeof(struct PlatformInputPadSnapshot) * PLATFORM_INPUT_PAD_COUNT);
	for (s32 slot = 0; slot < PLATFORM_INPUT_PAD_COUNT; slot++)
	{
		pads[slot].connected = 1;
		pads[slot].status = 0;
		pads[slot].id = (slot & 1) != 0 ? 0x73 : 0x41;
		pads[slot].buttons[0] = (u8)(0x11 * (slot + 1));
		pads[slot].buttons[1] = (u8)(0xf0 - slot);
		pads[slot].analog[0] = (u8)(0x10 + slot);
		pads[slot].analog[1] = (u8)(0x20 + slot);
		pads[slot].analog[2] = (u8)(0x30 + slot);
		pads[slot].analog[3] = (u8)(0x40 + slot);
	}
}

static int InstallPads(void)
{
	struct PlatformInputPadSnapshot pads[PLATFORM_INPUT_PAD_COUNT];

	MakeInstalledPads(pads);
	CHECK(Platform_InputInstallPadSnapshots(pads, PLATFORM_INPUT_PAD_COUNT) == PLATFORM_INPUT_PAD_COUNT);
	CHECK(s_installedSnapshotsActive == 1);
	return 1;
}

static void CloseAllDevices(void)
{
	for (s32 slot = 0; slot < NATIVE_INPUT_MAX_CONTROLLERS; slot++)
	{
		NativeInput_CloseController(slot);
	}
}

/* The observations can see a change: a harness that always compared equal
 * would make every side-effect check vacuous. */
static int TestObservationsBite(void)
{
	struct Observed before;
	struct Observed after;
	struct PlatformInputPadSnapshot saved = s_controllers[0].snapshot;
	s32 savedLastActive = s_lastActiveControllerSlot;
	s32 savedAnalog = s_controllers[0].analogEnabled;

	CHECK(Observe(&before));
	s_controllers[0].snapshot.analog[0] ^= 0x01;
	CHECK(Observe(&after));
	CHECK(memcmp(before.pads, after.pads, sizeof(before.pads)) != 0);
	CHECK(memcmp(before.state, after.state, sizeof(before.state)) != 0);
	NativeInput_WritePadBus();
	CHECK(Observe(&after));
	CHECK(memcmp(before.bus, after.bus, sizeof(before.bus)) != 0);
	s_controllers[0].snapshot = saved;
	NativeInput_WritePadBus();

	s_lastActiveControllerSlot = savedLastActive == 0 ? 1 : 0;
	CHECK(Observe(&after));
	CHECK(memcmp(before.state, after.state, sizeof(before.state)) != 0);
	s_lastActiveControllerSlot = savedLastActive;

	s_controllers[0].analogEnabled = savedAnalog == 0;
	CHECK(Observe(&after));
	CHECK(memcmp(before.state, after.state, sizeof(before.state)) != 0);
	s_controllers[0].analogEnabled = savedAnalog;

	s_controllers[0].g29State.throttleAwake ^= 1u;
	CHECK(Observe(&after));
	CHECK(memcmp(before.state, after.state, sizeof(before.state)) != 0);
	s_controllers[0].g29State.throttleAwake ^= 1u;

	CHECK(Observe(&after));
	CHECK(SameObserved(&before, &after));
	return 1;
}

static int TestNotInitialized(void)
{
	struct PlatformInputPadSnapshot dst;

	CHECK(s_inputInitialized == 0);
	memset(&dst, 0xa5, sizeof(dst));
	CHECK(Platform_InputSampleLocalPad(&dst) == 0);
	CHECK(CheckNeutral(&dst));
	CHECK(Platform_InputSampleLocalPad(NULL) == 0);
	return 1;
}

/* Installed pads active, no device, no key: the neutral slot-0 snapshot, and
 * nothing else changes. */
static int TestInstalledNeutral(void)
{
	struct PlatformInputPadSnapshot dst;

	CHECK(InstallPads());
	Platform_InputUpdate();
	/* The pad bus carries the installed pads (multitap: slots 2 and 3 are
	 * connected), so a stray write to it is visible. */
	CHECK(s_bus[0][1] == NATIVE_INPUT_PAD_MULTITAP);
	CHECK(s_bus[1][0] == NATIVE_INPUT_PAD_DISCONNECT);
	CHECK(s_bus[0][TEST_BUS_BYTES - 1] == TEST_BUS_SENTINEL);
	CHECK(TestObservationsBite());

	CHECK(SampleUntouched(&dst));
	CHECK(CheckNeutral(&dst));
	CHECK(Platform_InputSampleLocalPad(NULL) == 0);
	return 1;
}

/* The keyboard on slot 0: CROSS and START reach dst (unnormalized: START
 * stays pressed; the drive core releases it). Alt suppresses the keyboard,
 * and a keyboard mapped to another slot is not sampled. */
static int TestInstalledKeyboard(void)
{
	struct PlatformInputPadSnapshot dst;

	CHECK(s_installedSnapshotsActive == 1);
	ClearKeys();
	s_keys[s_keyboardMapping.kc_cross] = true;
	s_keys[s_keyboardMapping.kc_start] = true;
	s_keyboardControllerSlot = 0;

	CHECK(SampleUntouched(&dst));
	CHECK(CheckSlot0Snapshot(&dst, 0x41, 0xbff7, 0x80, 0x80, 0x80, 0x80));

	s_keys[SDL_SCANCODE_LALT] = true;
	CHECK(SampleUntouched(&dst));
	CHECK(CheckNeutral(&dst));
	s_keys[SDL_SCANCODE_LALT] = false;
	s_keys[SDL_SCANCODE_RALT] = true;
	CHECK(SampleUntouched(&dst));
	CHECK(CheckNeutral(&dst));
	s_keys[SDL_SCANCODE_RALT] = false;

	s_keyboardControllerSlot = 1;
	CHECK(SampleUntouched(&dst));
	CHECK(CheckNeutral(&dst));
	s_keyboardControllerSlot = 0;

	/* SELECT and START together from the keyboard are not a chord: the
	 * keyboard path never suppressed them. */
	s_keys[s_keyboardMapping.kc_select] = true;
	CHECK(SampleUntouched(&dst));
	CHECK(CheckSlot0Snapshot(&dst, 0x41, 0xbff6, 0x80, 0x80, 0x80, 0x80));
	ClearKeys();
	return 1;
}

/* Installed pads cleared: Platform_InputUpdate builds slot 0 from the
 * keyboard, the sample reads the same bytes, and still writes only dst. */
static int TestClearedKeyboard(void)
{
	struct PlatformInputPadSnapshot dst;
	struct PlatformInputPadSnapshot pads[PLATFORM_INPUT_PAD_COUNT];

	Platform_InputClearInstalledPadSnapshots();
	CHECK(s_installedSnapshotsActive == 0);
	g_padCommEnable = 1;
	ClearKeys();
	s_keys[s_keyboardMapping.kc_cross] = true;
	s_keys[s_keyboardMapping.kc_dpad_left] = true;
	s_keyboardControllerSlot = 0;
	Platform_InputUpdate();
	CHECK(Platform_InputCapturePadSnapshots(pads, PLATFORM_INPUT_PAD_COUNT) == PLATFORM_INPUT_PAD_COUNT);
	CHECK(CheckSlot0Snapshot(&pads[0], 0x41, 0xbf7f, 0x80, 0x80, 0x80, 0x80));

	CHECK(SampleUntouched(&dst));
	CHECK(memcmp(&dst, &pads[0], sizeof(dst)) == 0);

	/* The sample needs neither g_padCommEnable nor a pump (LR-38). */
	g_padCommEnable = 0;
	CHECK(SampleUntouched(&dst));
	CHECK(memcmp(&dst, &pads[0], sizeof(dst)) == 0);
	ClearKeys();
	CHECK(SampleUntouched(&dst));
	CHECK(CheckNeutral(&dst));
	return 1;
}

static SDL_JoystickID AttachVirtualGamepad(void)
{
	SDL_VirtualJoystickDesc desc;

	SDL_INIT_INTERFACE(&desc);
	desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
	desc.naxes = 4;
	desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_LEFTX) | (1u << SDL_GAMEPAD_AXIS_LEFTY) | (1u << SDL_GAMEPAD_AXIS_RIGHTX) |
	                 (1u << SDL_GAMEPAD_AXIS_RIGHTY);
	desc.nbuttons = 3;
	desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_SOUTH) | (1u << SDL_GAMEPAD_BUTTON_BACK) | (1u << SDL_GAMEPAD_BUTTON_START);
	desc.name = "CTR Native sample test gamepad";
	return SDL_AttachVirtualJoystick(&desc);
}

/* Joystick button and axis indices of the virtual gamepad (SDL numbers the
 * masked buttons and axes in gamepad order). */
#define PAD_BUTTON_SOUTH 0
#define PAD_BUTTON_BACK  1
#define PAD_BUTTON_START 2
#define PAD_AXIS_LEFTX   0
#define PAD_AXIS_RIGHTY  3

static int SetPad(SDL_Joystick *joystick, bool south, bool back, bool start, Sint16 leftX, Sint16 rightY)
{
	CHECK(SDL_SetJoystickVirtualButton(joystick, PAD_BUTTON_SOUTH, south));
	CHECK(SDL_SetJoystickVirtualButton(joystick, PAD_BUTTON_BACK, back));
	CHECK(SDL_SetJoystickVirtualButton(joystick, PAD_BUTTON_START, start));
	CHECK(SDL_SetJoystickVirtualAxis(joystick, PAD_AXIS_LEFTX, leftX));
	CHECK(SDL_SetJoystickVirtualAxis(joystick, PAD_AXIS_RIGHTY, rightY));
	SDL_UpdateJoysticks();
	return 1;
}

/* A gamepad on slot 0. With installed pads active the sample reads it,
 * never records the slot as active, and never toggles analog mode on a
 * SELECT+START chord (LR-39). With installed pads cleared,
 * Platform_InputUpdate still toggles, once per chord, as before. */
static int TestGamepad(void)
{
	struct PlatformInputPadSnapshot dst;
	struct PlatformInputPadSnapshot pads[PLATFORM_INPUT_PAD_COUNT];
	SDL_JoystickID id = AttachVirtualGamepad();
	SDL_Joystick *joystick;

	CHECK(id != 0);
	CHECK(SDL_IsGamepad(id));
	NativeInput_OpenController(id, 0);
	CHECK(s_controllers[0].controller != NULL);
	CHECK(s_controllers[0].joystick == NULL);
	CHECK(s_controllers[0].analogEnabled == 1);
	joystick = SDL_GetGamepadJoystick(s_controllers[0].controller);
	CHECK(joystick != NULL);
	/* Opening a pad moves the keyboard off its slot; put it back so the
	 * keyboard merge is exercised too (no key is down). */
	s_keyboardControllerSlot = 0;
	ClearKeys();

	CHECK(InstallPads());
	Platform_InputUpdate();
	s_lastActiveControllerSlot = -1;

	CHECK(SetPad(joystick, false, false, false, 0, 0));
	CHECK(SampleUntouched(&dst));
	CHECK(CheckSlot0Snapshot(&dst, 0x73, 0xffff, 0x80, 0x80, 0x80, 0x80));

	/* CROSS and full right on the left stick: the active slot stays -1. */
	CHECK(SetPad(joystick, true, false, false, 32767, -32768));
	CHECK(SampleUntouched(&dst));
	CHECK(CheckSlot0Snapshot(&dst, 0x73, 0xbfff, 0x80, 0x00, 0xff, 0x80));
	CHECK(s_lastActiveControllerSlot == -1);

	/* START alone reaches dst pressed (normalization is the core's job). */
	CHECK(SetPad(joystick, false, false, true, 0, 0));
	CHECK(SampleUntouched(&dst));
	CHECK(CheckSlot0Snapshot(&dst, 0x73, 0xfff7, 0x80, 0x80, 0x80, 0x80));

	/* The chord is suppressed, and analog mode never toggles, however many
	 * samples see it. */
	CHECK(SetPad(joystick, false, true, true, 0, 0));
	for (int i = 0; i < 3; i++)
	{
		CHECK(SampleUntouched(&dst));
		CHECK(CheckSlot0Snapshot(&dst, 0x73, 0xffff, 0x80, 0x80, 0x80, 0x80));
		CHECK(s_controllers[0].analogEnabled == 1);
		CHECK(s_controllers[0].switchingAnalog == 0);
	}

	/* Platform_InputUpdate's own toggle, unchanged: once per chord, and the
	 * chord's own snapshot still carries the pre-toggle id. */
	Platform_InputClearInstalledPadSnapshots();
	g_padCommEnable = 1;
	Platform_InputUpdate();
	CHECK(s_controllers[0].analogEnabled == 0);
	CHECK(s_controllers[0].switchingAnalog == 1);
	CHECK(s_lastActiveControllerSlot == 0);
	CHECK(Platform_InputCapturePadSnapshots(pads, PLATFORM_INPUT_PAD_COUNT) == PLATFORM_INPUT_PAD_COUNT);
	CHECK(CheckSlot0Snapshot(&pads[0], 0x73, 0xffff, 0x80, 0x80, 0x80, 0x80));
	Platform_InputUpdate();
	CHECK(s_controllers[0].analogEnabled == 0);
	CHECK(s_controllers[0].switchingAnalog == 1);

	/* The sample reads the mode read-only: now digital. */
	CHECK(SampleUntouched(&dst));
	CHECK(CheckSlot0Snapshot(&dst, 0x41, 0xffff, 0x80, 0x80, 0x80, 0x80));
	CHECK(s_controllers[0].analogEnabled == 0);
	CHECK(s_controllers[0].switchingAnalog == 1);

	CHECK(SetPad(joystick, true, false, false, 0, 0));
	Platform_InputUpdate();
	CHECK(s_controllers[0].switchingAnalog == 0);
	CHECK(Platform_InputCapturePadSnapshots(pads, PLATFORM_INPUT_PAD_COUNT) == PLATFORM_INPUT_PAD_COUNT);
	CHECK(CheckSlot0Snapshot(&pads[0], 0x41, 0xbfff, 0x80, 0x80, 0x80, 0x80));
	CHECK(SampleUntouched(&dst));
	CHECK(memcmp(&dst, &pads[0], sizeof(dst)) == 0);
	g_padCommEnable = 0;

	NativeInput_CloseController(0);
	CHECK(SDL_DetachVirtualJoystick(id));
	return 1;
}

static SDL_JoystickID AttachVirtualG29(void)
{
	SDL_VirtualJoystickDesc desc;

	SDL_INIT_INTERFACE(&desc);
	desc.type = SDL_JOYSTICK_TYPE_WHEEL;
	desc.vendor_id = NATIVE_G29_VENDOR_ID;
	desc.product_id = NATIVE_G29_PRODUCT_ID;
	desc.naxes = NATIVE_G29_AXIS_COUNT;
	desc.nbuttons = NATIVE_G29_BUTTON_COUNT;
	desc.nhats = 1;
	desc.name = "CTR Native sample test wheel";
	return SDL_AttachVirtualJoystick(&desc);
}

static int SetWheel(SDL_Joystick *joystick, Sint16 steering, Sint16 throttle, Sint16 brake, bool options)
{
	CHECK(SDL_SetJoystickVirtualAxis(joystick, NATIVE_G29_STEERING_AXIS, steering));
	CHECK(SDL_SetJoystickVirtualAxis(joystick, NATIVE_G29_THROTTLE_AXIS, throttle));
	CHECK(SDL_SetJoystickVirtualAxis(joystick, NATIVE_G29_BRAKE_AXIS, brake));
	CHECK(SDL_SetJoystickVirtualButton(joystick, NATIVE_G29_BUTTON_OPTIONS, options));
	SDL_UpdateJoysticks();
	return 1;
}

/* A G29 on slot 0, with the G29 diagnostic enabled. The sample advances its
 * own pedal hysteresis (LR-37), never slot 0's saved g29State, never the
 * diagnostic, and never the active slot. A re-arm (clear, or an install
 * that turns installed pads on) seeds it from the live g29State. */
static int TestG29(void)
{
	struct PlatformInputPadSnapshot dst;
	struct PlatformInputPadSnapshot pads[PLATFORM_INPUT_PAD_COUNT];
	struct NativeG29MappingState live;
	SDL_JoystickID id = AttachVirtualG29();
	SDL_Joystick *joystick;

	CHECK(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), NATIVE_INPUT_G29_DIAGNOSTIC_ENV, "1", true));
	CHECK(NativeInput_G29DiagnosticsEnabled());
	CHECK(id != 0);
	NativeInput_OpenController(id, 0);
	joystick = s_controllers[0].joystick;
	CHECK(joystick != NULL);
	CHECK(s_controllers[0].controller == NULL);
	CHECK(s_directG29InstanceId == s_controllers[0].instanceId);
	s_keyboardControllerSlot = 0;
	ClearKeys();

	CHECK(InstallPads());
	Platform_InputUpdate();
	s_lastActiveControllerSlot = -1;
	live = s_controllers[0].g29State;
	CHECK(live.throttleAwake == 0 && live.throttlePressed == 0);

	/* Pedals at rest wake the sample's hysteresis only. */
	CHECK(SetWheel(joystick, 0, 32767, 32767, false));
	CHECK(SampleUntouched(&dst));
	CHECK(CheckSlot0Snapshot(&dst, 0x73, 0xffff, 0x80, 0x80, 0x80, 0x80));
	CHECK(s_sampleG29State.throttleAwake == 1 && s_sampleG29State.brakeAwake == 1);
	CHECK(memcmp(&s_controllers[0].g29State, &live, sizeof(live)) == 0);

	/* Between the thresholds: not pressed yet. Below: CROSS. Between again:
	 * still pressed (hysteresis). Released above the release threshold. */
	CHECK(SetWheel(joystick, 0, 24000, 32767, false));
	CHECK(SampleUntouched(&dst));
	CHECK(SnapshotButtons(&dst) == 0xffff);
	CHECK(SetWheel(joystick, 0, 20000, 32767, false));
	CHECK(SampleUntouched(&dst));
	CHECK(SnapshotButtons(&dst) == 0xbfff);
	CHECK(SetWheel(joystick, 0, 24000, 32767, false));
	CHECK(SampleUntouched(&dst));
	CHECK(SnapshotButtons(&dst) == 0xbfff);
	CHECK(SetWheel(joystick, 0, 32767, 32767, false));
	CHECK(SampleUntouched(&dst));
	CHECK(SnapshotButtons(&dst) == 0xffff);
	CHECK(memcmp(&s_controllers[0].g29State, &live, sizeof(live)) == 0);

	/* Steering and Options (START, unnormalized). */
	CHECK(SetWheel(joystick, 16640, 32767, 32767, true));
	CHECK(SampleUntouched(&dst));
	CHECK(CheckSlot0Snapshot(&dst, 0x73, 0xfff7, 0x80, 0x80, 0xff, 0x80));
	CHECK(s_lastActiveControllerSlot == -1);

	/* Cleared: Platform_InputUpdate drives the live state (and the active
	 * slot and the diagnostic) as before. */
	CHECK(SetWheel(joystick, 0, 32767, 32767, false));
	Platform_InputClearInstalledPadSnapshots();
	g_padCommEnable = 1;
	Platform_InputUpdate();
	CHECK(SetWheel(joystick, 0, 20000, 32767, false));
	Platform_InputUpdate();
	CHECK(s_controllers[0].g29State.throttleAwake == 1 && s_controllers[0].g29State.throttlePressed == 1);
	CHECK(s_lastActiveControllerSlot == 0);
	CHECK(s_g29Diagnostic.valid == 1);
	CHECK(Platform_InputCapturePadSnapshots(pads, PLATFORM_INPUT_PAD_COUNT) == PLATFORM_INPUT_PAD_COUNT);
	CHECK(CheckSlot0Snapshot(&pads[0], 0x73, 0xbfff, 0x80, 0x80, 0x80, 0x80));

	/* The clear re-armed the sample: its first sample seeds from the live
	 * state and so matches Platform_InputUpdate's slot 0. */
	CHECK(SampleUntouched(&dst));
	CHECK(memcmp(&dst, &pads[0], sizeof(dst)) == 0);

	/* Release the pedal in the sample only, then turn installed pads on:
	 * that re-arms, so the next sample seeds from the live (pressed) state
	 * again and CROSS holds at 24000. */
	CHECK(SetWheel(joystick, 0, 32767, 32767, false));
	CHECK(SampleUntouched(&dst));
	CHECK(SnapshotButtons(&dst) == 0xffff);
	CHECK(s_sampleG29State.throttlePressed == 0);
	CHECK(InstallPads());
	Platform_InputUpdate();
	CHECK(SetWheel(joystick, 0, 24000, 32767, false));
	CHECK(SampleUntouched(&dst));
	CHECK(SnapshotButtons(&dst) == 0xbfff);

	/* A second install while already active does not re-arm. */
	CHECK(SetWheel(joystick, 0, 32767, 32767, false));
	CHECK(SampleUntouched(&dst));
	CHECK(SnapshotButtons(&dst) == 0xffff);
	CHECK(InstallPads());
	CHECK(SetWheel(joystick, 0, 24000, 32767, false));
	CHECK(SampleUntouched(&dst));
	CHECK(SnapshotButtons(&dst) == 0xffff);
	g_padCommEnable = 0;

	CHECK(SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), NATIVE_INPUT_G29_DIAGNOSTIC_ENV));
	NativeInput_CloseController(0);
	CHECK(SDL_DetachVirtualJoystick(id));
	return 1;
}

static int TestAfterShutdown(void)
{
	struct PlatformInputPadSnapshot dst;

	Platform_InputShutdown();
	CHECK(s_inputInitialized == 0);
	CHECK(s_sampleG29Armed == 1);
	memset(&dst, 0xa5, sizeof(dst));
	CHECK(Platform_InputSampleLocalPad(&dst) == 0);
	CHECK(CheckNeutral(&dst));
	CHECK(Platform_InputSampleLocalPad(NULL) == 0);
	return 1;
}

int main(void)
{
	NativeSdlAssert_Install(NULL);

	TestNotInitialized();

	if (Platform_InputInit() != 1)
	{
		fprintf(stderr, "native_input_sample_unit: Platform_InputInit failed\n");
		return 1;
	}
	/* No host device, whatever is plugged in, and a test-owned keyboard. */
	CloseAllDevices();
	ClearKeys();
	s_keyboardState = s_keys;
	s_keyboardControllerSlot = 0;
	g_padCommEnable = 0;
	memset(s_bus, TEST_BUS_SENTINEL, sizeof(s_bus));
	Platform_InputPadInit(0, s_bus[0]);
	Platform_InputPadInit(1, s_bus[1]);

	/* The cases build on each other's state, so the first failure stops the
	 * chain; input is shut down either way. */
	(void)((s_failures == 0) && TestInstalledNeutral() && TestInstalledKeyboard() && TestClearedKeyboard() && TestGamepad() && TestG29());
	TestAfterShutdown();

	if (s_failures != 0)
	{
		fprintf(stderr, "native_input_sample_unit: %d failure(s)\n", s_failures);
		return 1;
	}
	printf("native_input_sample_unit: ok\n");
	return 0;
}
