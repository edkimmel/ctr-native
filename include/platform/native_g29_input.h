#ifndef PLATFORM_NATIVE_G29_INPUT_H
#define PLATFORM_NATIVE_G29_INPUT_H

#include <stdint.h>

#define NATIVE_G29_VENDOR_ID                 0x046du
#define NATIVE_G29_PRODUCT_ID                0xc24fu
#define NATIVE_G29_AXIS_COUNT                4
#define NATIVE_G29_BUTTON_COUNT              25
#define NATIVE_G29_BUTTON_CROSS              0
#define NATIVE_G29_BUTTON_SQUARE             1
#define NATIVE_G29_BUTTON_CIRCLE             2
#define NATIVE_G29_BUTTON_TRIANGLE           3
#define NATIVE_G29_BUTTON_R2                 4
#define NATIVE_G29_BUTTON_L2                 5
/* CAB1 measured the physical right/left paddles at buttons 6/7. */
#define NATIVE_G29_BUTTON_R1                 6
#define NATIVE_G29_BUTTON_L1                 7
#define NATIVE_G29_BUTTON_SHARE              9
#define NATIVE_G29_BUTTON_OPTIONS            24
#define NATIVE_G29_STEERING_AXIS             0
/* CAB1 live diagnostic: gas is axis 1, brake is axis 2 (active-low). */
#define NATIVE_G29_THROTTLE_AXIS             1
#define NATIVE_G29_BRAKE_AXIS                2
#define NATIVE_G29_STEERING_DEADZONE         512
#define NATIVE_G29_PEDAL_WAKE_THRESHOLD      30000
#define NATIVE_G29_PEDAL_PRESS_THRESHOLD     22500
#define NATIVE_G29_PEDAL_RELEASE_THRESHOLD   26300

enum NativeG29DeviceMatch
{
	NATIVE_G29_DEVICE_NO_MATCH = 0,
	NATIVE_G29_DEVICE_VID_PID = 1,
	NATIVE_G29_DEVICE_NAME_FALLBACK = 2
};

enum NativeG29DeviceClaim
{
	NATIVE_G29_DEVICE_CLAIM_AVAILABLE = 0,
	NATIVE_G29_DEVICE_CLAIM_SAME_INSTANCE = 1,
	NATIVE_G29_DEVICE_CLAIM_DUPLICATE = 2
};

struct NativeG29RawInput
{
	int16_t axes[NATIVE_G29_AXIS_COUNT];
	uint8_t buttons[NATIVE_G29_BUTTON_COUNT];
	uint8_t hat;
};

struct NativeG29MappingState
{
	uint8_t throttleAwake;
	uint8_t brakeAwake;
	uint8_t throttlePressed;
	uint8_t brakePressed;
};

struct NativeG29MappedInput
{
	uint16_t buttons;
	int16_t steering;
	uint8_t active;
};

enum NativeG29DeviceMatch NativeG29Input_MatchDevice(uint16_t vendor, uint16_t product, const char *name);
enum NativeG29DeviceClaim NativeG29Input_CheckClaim(int32_t selectedInstanceId, int32_t candidateInstanceId);
int NativeG29Input_ShouldUseDirect(enum NativeG29DeviceMatch match, int isSdlGamepad);
int NativeG29Input_ValidateMappingState(const struct NativeG29MappingState *state);
int16_t NativeG29Input_ShapeSteering(int16_t raw);
void NativeG29Input_Map(
	const struct NativeG29RawInput *raw,
	struct NativeG29MappingState *state,
	struct NativeG29MappedInput *mapped);

#endif
