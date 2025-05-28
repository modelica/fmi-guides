#pragma once

#include "fmi3PlatformTypes.h"

/**
 * Describes the configuration of the FlexRay cluster.
 */
typedef struct
{
    fmi3UInt32 MacroTickDurationNs;
    fmi3UInt16 MacroTicksPerCycle;
    fmi3UInt16 StaticSlotDurationMt;
    fmi3UInt8 NumberOfCycles;
    fmi3UInt16 NumberOfStaticSlots;
    fmi3UInt8 MinislotDurationMt;
    fmi3UInt8 ActionPointOffset;
    fmi3UInt8 StaticPayloadLength;
    fmi3UInt8 MinislotActionPointOffset;
    fmi3UInt16 NumberOfMinislots;
    fmi3UInt8 MinislotLength;
    fmi3UInt8 SymbolActionPointOffset;
    fmi3UInt8 SymbolWindowLength;
    fmi3UInt8 NitLength;
    fmi3UInt8 NMVectorLength;
    fmi3UInt32 DynamicSlotIdleTime;
} ClusterConfig;

/**
 * Determines the boundaries of a FlexRay slot in macro ticks.
 *
 * The start is the earliest point in time where a bus operation may be sent for this
 * slot whereas the end is the latest point in time where a bus operation may be received
 * for this slot.
 *
 * \param[in] clusterConfig The FlexRay cluster configuration.
 * \param[in] iteration The current FlexRay iteration.
 * \param[in] cycle The FlexRay cycle.
 * \param[in] slot The FlexRay slot
 * \param[out] slotStartMt The start of the slot in macro ticks.
 * \param[out] slotEndMt The end of the slot in macro ticks.
 */
void GetSlotBoundariesMt(const ClusterConfig* clusterConfig,
                         fmi3UInt64 iteration,
                         fmi3UInt8 cycle,
                         fmi3UInt16 slot,
                         fmi3UInt64* slotStartMt,
                         fmi3UInt64* slotEndMt);
