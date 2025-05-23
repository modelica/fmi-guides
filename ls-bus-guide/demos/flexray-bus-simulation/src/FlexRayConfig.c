#include "FlexRayConfig.h"

const ClusterConfig FlexRayClusterConfig = {.MacroTickDurationNs = 1000,
                                            .MacroTicksPerCycle = 1000,
                                            .StaticSlotDurationMt = 100,
                                            .NumberOfCycles = 1,
                                            .NumberOfStaticSlots = 2,
                                            .MinislotDurationMt = 10,
                                            .ActionPointOffset = 1,
                                            .StaticPayloadLength = 64,
                                            .MinislotActionPointOffset = 1,
                                            .NumberOfMinislots = 10,
                                            .MinislotLength = 10,
                                            .SymbolActionPointOffset = 1,
                                            .SymbolWindowLength = 10,
                                            .NitLength = 2,
                                            .NMVectorLength = 0,
                                            .DynamicSlotIdleTime = 0};
