# Logitech G29 native input

CTR Native opens ordinary controllers through SDL's Gamepad API.  The Logitech
G29 on CAB1 is a lower-level SDL joystick, so it uses a narrow direct-joystick
fallback instead of a virtual Xbox controller.

## Selection and mapping

The fallback accepts Logitech VID `046d`, G29 PID `c24f`.  A case-insensitive
`G29` name is accepted only when SDL omits either hardware ID and every nonzero
ID SDL does provide matches the G29.  It rejects any explicitly different VID
or PID and requires at least four axes, twenty-five buttons, and one hat.  Other
joysticks remain ignored; SDL Gamepads and the keyboard retain their existing
paths.

A matched G29 is always opened through this direct path, even if SDL's
controller database also classifies it as a Gamepad.  CAB1's Options control is
direct joystick button 24 and would otherwise be lost to the generic mapping.

Only one direct G29 may be bound on a process.  Enumeration order claims the
first matching physical G29; later matching instances are logged and ignored
instead of becoming another local PS1 pad.  A duplicate add event for the
already-bound SDL instance is also ignored.  Disconnecting the selected wheel
releases the claim, so a later add event may claim a wheel again.

The layout below was measured on CAB1 and is converted once into the same
active-low PS1 pad snapshot used by replay and canonical input:

| G29 control | SDL input | PS1 control |
| --- | --- | --- |
| Steering | axis 0, left negative | left stick X |
| Throttle | axis 2, rest `+32767`, pressed `-32768` | Cross |
| Brake | axis 3, rest `+32767`, pressed `-32768` | Square |
| Cross / Square / Circle / Triangle | buttons 0 / 1 / 2 / 3 | same |
| R2 / L2 | buttons 4 / 5 | R2 / L2 |
| Right / left paddle | buttons 6 / 7 | R1 + Circle/item / L1 |
| Share / Options | buttons 9 / 24 | Select / Start |
| D-pad | hat 0 | D-pad |

Steering has a 512-count center deadzone, re-anchored to preserve full range.
Pedals ignore SDL's initial zero value until an axis crosses 30,000 magnitude.
They press below 22,500 and release above 26,300, providing hysteresis around
the digital threshold.

Direct G29 force feedback is intentionally not enabled by this input slice.

## Pedal diagnostic

For a hardware check, launch with `CTR_NATIVE_G29_DIAGNOSTICS=1`.  The direct
G29 path then logs its complete reported axis count and up to the first sixteen
axis values when the wheel opens, and again only after a material axis change
(2,048 counts), pedal wake/press transition, or mapped PS1 button change.  Each
line includes the configured throttle/brake raw values, awake/pressed flags,
and the resulting active-low PS1 button word.  The variable is off by default;
it changes neither mapping nor saved/replay input state.

The pedal wake/hysteresis flags are now part of native input-state snapshot
version 2.  Pre-v2 quick states/checkpoints contain the smaller v1 input block
and are rejected rather than guessed or partially restored.  Replay/canonical
pad snapshots are unchanged because they already store the converted PS1 bytes.

## CAB1 acceptance

After a fresh Release build, verify the startup log contains exactly one
`opened Logitech G29` line for input slot 1.  Using only the wheel:

1. Wake both pedals once and confirm no phantom menu input at rest.
2. Check D-pad navigation, Cross confirm, Square/Triangle back, Options Start,
   and Share Select.
3. In a solo race, check steering direction and center, throttle acceleration,
   brake/reverse, both paddles for hopping/powersliding, and face buttons.
4. Finish a race, exit normally, cold-launch, and repeat a short control check.

The SDL3 hardware IDs, axis polarity, threshold feel, and exact menu semantics
remain live-hardware acceptance items even though the pure conversion and
device-selection logic are unit tested.
