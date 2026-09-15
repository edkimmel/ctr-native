# Logitech G29 native input

CTR Native opens ordinary controllers through SDL's Gamepad API.  The Logitech
G29 on CAB1 is a lower-level SDL joystick, so it uses a narrow direct-joystick
fallback instead of a virtual Xbox controller.

## Selection and mapping

The fallback accepts Logitech VID `046d`, G29 PID `c24f`.  A case-insensitive
`G29` name is accepted only when SDL omits either hardware ID.  It rejects an
explicitly different VID/PID and requires at least four axes, twelve buttons,
and one hat.  Other joysticks remain ignored; SDL Gamepads and the keyboard
retain their existing paths.

The layout below was measured on CAB1 and is converted once into the same
active-low PS1 pad snapshot used by replay and canonical input:

| G29 control | SDL input | PS1 control |
| --- | --- | --- |
| Steering | axis 0, left negative | left stick X |
| Throttle | axis 2, rest `+32767`, pressed `-32768` | Cross |
| Brake | axis 3, rest `+32767`, pressed `-32768` | Square |
| Cross / Square / Circle / Triangle | buttons 0 / 1 / 2 / 3 | same |
| Right / left paddle | buttons 4 / 5 | R1 / L1 |
| R2 / L2 | buttons 6 / 7 | R2 / L2 |
| Share / Options | buttons 8 / 9 | Select / Start |
| R3 / L3 | buttons 10 / 11 | R3 / L3 |
| D-pad | hat 0 | D-pad |

Steering has a 512-count center deadzone, re-anchored to preserve full range.
Pedals ignore SDL's initial zero value until an axis crosses 30,000 magnitude.
They press below 22,500 and release above 26,300, providing hysteresis around
the digital threshold.

Direct G29 force feedback is intentionally not enabled by this input slice.

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
