#ifndef PLATFORM_NATIVE_ARCADE_RACE_DRIVE_H
#define PLATFORM_NATIVE_ARCADE_RACE_DRIVE_H

/*
 * Linked-race drive core (docs/LOCKSTEP_RACE_MILESTONE.md LR-1). A pure core
 * over caller-owned values: no socket, clock, SDL, heap, or game dependency.
 * Slice LR-S7 adds only the pad normalization (LR-4, LR-5, LR-40); slice
 * LR-S8 grows this module into the full per-tick drive core.
 */

#include "platform/native_canonical_state.h"

#include <stdint.h>

/* The only pad ids a normalized pad carries. */
#define NATIVE_ARCADE_RACE_DRIVE_PAD_ID_DIGITAL 0x41u
#define NATIVE_ARCADE_RACE_DRIVE_PAD_ID_ANALOG  0x73u

/* START in the 16-bit PSX button word, buttons[0] | (buttons[1] << 8). The
 * word is active-low: a set bit is a released button. */
#define NATIVE_ARCADE_RACE_DRIVE_START_MASK 0x0008u

/* The neutral connected pad: status 0, digital id, nothing pressed, every
 * analog axis centred. */
#define NATIVE_ARCADE_RACE_DRIVE_NEUTRAL_BUTTONS 0xffffu
#define NATIVE_ARCADE_RACE_DRIVE_NEUTRAL_ANALOG  0x80u

/* Writes the neutral connected pad (connected 1, status 0, id 0x41, buttons
 * 0xff 0xff, analog 0x80 x4) to *out. NULL out: no-op. */
void NativeArcadeRaceDrive_NeutralPad(struct NativeCanonicalInputPadV1 *out);

/*
 * Normalizes one pad (LR-4, LR-40). A pad whose connected byte is 0 becomes
 * the neutral connected pad. Any other connected byte counts as connected;
 * such a pad keeps its button bits and analog bytes, except that START is
 * released, and gets connected 1, status 0 (its own status byte is
 * overwritten, never used to decide disconnection), and id 0x73 if its id
 * is 0x73, else 0x41. in and out may be the same pad. NULL in or out: no-op,
 * out untouched.
 */
void NativeArcadeRaceDrive_NormalizePad(const struct NativeCanonicalInputPadV1 *in, struct NativeCanonicalInputPadV1 *out);

#endif
