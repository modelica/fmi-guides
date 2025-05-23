#include "FlexRayCommon.h"

void GetSlotBoundariesMt(const ClusterConfig* clusterConfig,
                         fmi3UInt64 iteration,
                         fmi3UInt8 cycle,
                         fmi3UInt16 slot,
                         fmi3UInt64* slotStartMt,
                         fmi3UInt64* slotEndMt)
{
    const fmi3UInt64 iterationOffsetMt = iteration * clusterConfig->NumberOfCycles * clusterConfig->MacroTicksPerCycle;
    const fmi3UInt64 cycleOffsetMt = iterationOffsetMt + (fmi3UInt64)cycle * clusterConfig->MacroTicksPerCycle;
    const fmi3UInt64 dynamicSegmentOffsetMt =
        (fmi3UInt64)clusterConfig->NumberOfStaticSlots * clusterConfig->StaticSlotDurationMt;

    if (slot <= clusterConfig->NumberOfStaticSlots)
    {
        const fmi3UInt64 slotStartOffsetMt = (fmi3UInt64)(slot - 1) * clusterConfig->StaticSlotDurationMt;
        const fmi3UInt64 slotEndOffsetMt = (fmi3UInt64)(slot - 1 + 1) * clusterConfig->StaticSlotDurationMt;
        *slotStartMt = cycleOffsetMt + slotStartOffsetMt;
        *slotEndMt = cycleOffsetMt + slotEndOffsetMt;
    }
    else
    {
        const fmi3UInt64 miniSlotOffsetMt =
            (fmi3UInt64)(slot - clusterConfig->NumberOfStaticSlots - 1) * clusterConfig->MinislotDurationMt;
        *slotStartMt = cycleOffsetMt + dynamicSegmentOffsetMt + miniSlotOffsetMt;
        *slotEndMt = cycleOffsetMt + dynamicSegmentOffsetMt +
                     (fmi3UInt64)clusterConfig->NumberOfMinislots * clusterConfig->MinislotDurationMt;
    }
}
