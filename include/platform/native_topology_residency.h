#ifndef PLATFORM_NATIVE_TOPOLOGY_RESIDENCY_H
#define PLATFORM_NATIVE_TOPOLOGY_RESIDENCY_H

#include <stdint.h>

/* Pointer-free, source-only validation of a caller-observed active-mempack
 * topology layout.  Addresses are 32-bit native address values, never host
 * pointers: this module neither dereferences nor acquires them. */
#define NATIVE_TOPOLOGY_RESIDENCY_NAV_PATH_COUNT 3u
#define NATIVE_TOPOLOGY_RESIDENCY_LEVEL_PREFIX_BYTES 0x18cu
#define NATIVE_TOPOLOGY_RESIDENCY_MESH_BYTES 0x20u
#define NATIVE_TOPOLOGY_RESIDENCY_QUAD_BYTES 0x5cu
#define NATIVE_TOPOLOGY_RESIDENCY_RESTART_BYTES 0x0cu
#define NATIVE_TOPOLOGY_RESIDENCY_NAV_TABLE_BYTES 12u
#define NATIVE_TOPOLOGY_RESIDENCY_NAV_HEADER_BYTES 0x4cu
#define NATIVE_TOPOLOGY_RESIDENCY_NAV_FRAME_BYTES 0x14u
#define NATIVE_TOPOLOGY_RESIDENCY_NAV_MAGIC (-0x1303)
#define NATIVE_TOPOLOGY_RESIDENCY_MAX_QUAD_COUNT 32766u
#define NATIVE_TOPOLOGY_RESIDENCY_MAX_RESTART_COUNT 255u
#define NATIVE_TOPOLOGY_RESIDENCY_MAX_NAV_POINT_COUNT 32766u
#define NATIVE_TOPOLOGY_RESIDENCY_SNAPSHOT_TAG UINT32_C(0x3152544e) /* NTR1 */

struct NativeTopologyResidencyLeaseV1
{
	uint32_t base;
	uint32_t span;
	uint64_t epoch;
};

struct NativeTopologyResidencyNavPathV1
{
	uint32_t headerAddress;
	uint32_t frameAddress;
	int16_t magic;
	uint16_t pointCount;
};

struct NativeTopologyResidencyObservedV1
{
	uint32_t levelAddress;
	uint32_t meshAddress;
	uint32_t quadAddress;
	uint32_t quadCount;
	uint32_t restartAddress;
	uint32_t restartCount;
	uint32_t navTableAddress;
	struct NativeTopologyResidencyNavPathV1 nav[NATIVE_TOPOLOGY_RESIDENCY_NAV_PATH_COUNT];
};

struct NativeTopologyResidencySnapshotV1
{
	uint32_t tag;
	struct NativeTopologyResidencyLeaseV1 lease;
	struct NativeTopologyResidencyObservedV1 observed;
};

/* Capture validates every supplied address range against lease.  On failure
 * snapshotOut is unchanged.  Empty quads/restarts are represented by zero
 * count and zero address.  A nav path is empty only when all four of its
 * fields are zero; a present zero-point path has a valid header and a zero
 * frame address. */
int NativeTopologyResidencyV1_Capture(struct NativeTopologyResidencySnapshotV1 *snapshotOut,
	const struct NativeTopologyResidencyLeaseV1 *lease,
	const struct NativeTopologyResidencyObservedV1 *observed);

/* Revalidates supplied facts then compares the full lease identity and every
 * observed field.  Epoch is deliberately part of identity. */
int NativeTopologyResidencyV1_Validate(const struct NativeTopologyResidencySnapshotV1 *snapshot,
	const struct NativeTopologyResidencyLeaseV1 *lease,
	const struct NativeTopologyResidencyObservedV1 *observed);

#endif
