#include "App.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

#include "fmi3LsBusFlexRay.h"
#include "fmi3LsBusUtil.h"
#include "fmi3LsBusUtilFlexRay.h"

#include "FlexRayCommon.h"
#include "Logging.h"
#include "OperationBuffer.h"
#include "Utility.h"

/*
 * \brief Identifies variables of this FMU by their value references.
 */
typedef enum
{
    FMU_VAR_NODE1_RX_DATA = 0,
    FMU_VAR_NODE1_TX_DATA = 1,
    FMU_VAR_NODE1_RX_CLOCK = 2,
    FMU_VAR_NODE1_TX_CLOCK = 3,

    FMU_VAR_NODE2_RX_DATA = 4,
    FMU_VAR_NODE2_TX_DATA = 5,
    FMU_VAR_NODE2_RX_CLOCK = 6,
    FMU_VAR_NODE2_TX_CLOCK = 7,

    
    FMU_VAR_NODE1_BUS_NOTIFICATIONS = 10,
    FMU_VAR_NODE1_DELIVERY_ON_BOUNDARY = 11,
    FMU_VAR_NODE2_BUS_NOTIFICATIONS = 12,
    FMU_VAR_NODE2_DELIVERY_ON_BOUNDARY = 13,
} FmuVariables;

enum
{
    NUM_NODES = 2
};

/**
 * \brief Node-specific data.
 */
typedef struct
{
    fmi3Byte RxBuffer[2048];              /**< The FMI-LS-BUS reception buffer. */
    fmi3UInt64 RxBufferSize;              /**< The current size of the FMI-LS-BUS reception buffer. */
    fmi3LsBusUtilBufferInfo RxBufferInfo; /**< The reception buffer info. */
    fmi3Clock RxClock;                    /**< The FMI-LS-BUS reception clock. */

    fmi3Byte TxBuffer[2048];              /**< The FMI-LS-BUS transmission buffer. */
    fmi3UInt64 TxBufferSize;              /**< The current size of the FMI-LS-BUS transmission buffer. */
    fmi3LsBusUtilBufferInfo TxBufferInfo; /**< The transmission buffer info. */
    fmi3Clock TxClock;                    /**< The FMI-LS-BUS transmission clock. */

    OperationBuffer RxOperationBuffer; /**< The buffer for received bus operations. */

    fmi3UInt64 NextTxClockTimeNs;       /**< The currently configured time of the TX (countdown) clock. */
    fmi3Boolean NextTxClockTimeSet;     /**< Whether the TX (countdown) clock is currently configured. */
    fmi3Boolean NextTxClockTimeUpdated; /**< Whether the TX (countdown) clock has been reconfigured. */
} NodeData;

/**
 * \brief Instance-specific data of this FMU.
 */
struct AppType
{
    FmuInstance* FmuInstance; /**< The FMU instance. */

    NodeData Node[2]; /**< The data of nodes. */

    ClusterConfig ClusterConfig;
    bool ClusterConfigValid;

    fmi3UInt64 StartTimeNs;
    bool StartTimeValid;

    fmi3UInt64 CurrentIteration;
    fmi3UInt8 CurrentCycle;
    fmi3UInt16 CurrentSlot;

    fmi3UInt64 CurrentTimeNs;     /**< The current simulation time in ns. */
    bool DiscreteStatesEvaluated; /**< Whether the discrete states of the FMU have been evaluated. */
};

/**
 * Updates the time of the next known bus event.
 *
 * \param instance The FMU instance.
 * \param nodeId The node ID whose TX clock to configure.
 * \param nextEventTimeNs The simulation time of the next bus event.
 * \param nextEventTimeValid Whether the next bus event time is known.
 */
static void SetNextBusEventTime(const FmuInstance* instance,
                                const fmi3UInt32 nodeId,
                                const fmi3UInt64 nextEventTimeNs,
                                const bool nextEventTimeValid)
{
    NodeData* nodeData = &instance->App->Node[nodeId];

    if (nextEventTimeValid)
    {
        if (nodeData->NextTxClockTimeSet == fmi3False || nodeData->NextTxClockTimeNs != nextEventTimeNs)
        {
            nodeData->NextTxClockTimeNs = nextEventTimeNs;
            nodeData->NextTxClockTimeSet = fmi3True;
            nodeData->NextTxClockTimeUpdated = fmi3True;
        }
    }
    else
    {
        nodeData->NextTxClockTimeSet = fmi3False;
    }
}

/**
 * Determines the time of the current FlexRay slot.
 *
 * \param app The FMU application instance data.
 * \param slotOffset Whether to examine the current FlexRay slot (0) or the next one (1).
 * \return The simulation time of the next pending FlexRay transmission.
 */
static fmi3UInt64 GetCurrentSlotTime(const AppType* app, const fmi3UInt16 slotOffset)
{
    assert(app->ClusterConfigValid);
    const fmi3UInt16 numberOfSlots = app->ClusterConfig.NumberOfStaticSlots + app->ClusterConfig.NumberOfMinislots;

    fmi3UInt64 iteration = app->CurrentIteration;
    fmi3UInt8 cycleId = app->CurrentCycle;
    fmi3UInt16 slotId = app->CurrentSlot + slotOffset;

    while (slotId > numberOfSlots)
    {
        slotId -= numberOfSlots;
        cycleId++;
    }

    while (cycleId >= app->ClusterConfig.NumberOfCycles)
    {
        cycleId -= app->ClusterConfig.NumberOfCycles;
        iteration++;
    }

    fmi3UInt64 slotStartMt, slotEndMt;
    GetSlotBoundariesMt(&app->ClusterConfig, iteration, cycleId, slotId, &slotStartMt, &slotEndMt);

    return app->StartTimeNs + slotStartMt * app->ClusterConfig.MacroTickDurationNs;
}

/**
 * Updates the next time of the transmit countdown clock.
 *
 * \param nodeId The node ID whose TX clock to update.
 * \param instance The FMU instance.
 */
static void UpdateCountdownClock(const FmuInstance* instance, fmi3UInt32 nodeId)
{
    NodeData* nodeData = &instance->App->Node[nodeId];

    const bool messagesAvailable = !FMI3_LS_BUS_BUFFER_IS_EMPTY(&nodeData->TxBufferInfo);
    if (messagesAvailable)
    {
        SetNextBusEventTime(instance, nodeId, instance->App->CurrentTimeNs, true);
        return;
    }

    if (!instance->App->ClusterConfigValid || !instance->App->StartTimeValid)
    {
        SetNextBusEventTime(instance, nodeId, 0, false);
        return;
    }

    fmi3UInt64 nextSlotTime = GetCurrentSlotTime(instance->App, 0);

    // The next slot is not in the future -> We should use the next slot for setting the clock
    fmi3UInt16 slotOffset = 1;
    while (nextSlotTime <= instance->App->CurrentTimeNs)
    {
        // Check whether we need to advance an iteration
        nextSlotTime = GetCurrentSlotTime(instance->App, slotOffset);
        slotOffset++;
    }

    assert(nextSlotTime > instance->App->CurrentTimeNs);
    SetNextBusEventTime(instance, nodeId, nextSlotTime, true);
}

AppType* App_Instantiate(FmuInstance* instance)
{
    AppType* app = calloc(1, sizeof(AppType));
    if (app == NULL)
    {
        return NULL;
    }

    app->FmuInstance = instance;

    for (size_t i = 0; i < NUM_NODES; i++)
    {
        app->Node[i].RxBufferSize = 0;
        app->Node[i].RxClock = fmi3ClockInactive;

        app->Node[i].TxBufferSize = 0;
        app->Node[i].TxClock = fmi3ClockInactive;

        app->Node[i].NextTxClockTimeSet = fmi3False;
        app->Node[i].NextTxClockTimeUpdated = fmi3False;

        FMI3_LS_BUS_BUFFER_INFO_INIT(&app->Node[i].TxBufferInfo, app->Node[i].TxBuffer, sizeof app->Node[i].TxBuffer);
        OperationBuffer_Init(&app->Node[i].RxOperationBuffer);
    }

    return app;
}

void App_Init(FmuInstance* instance)
{
    AppType* app = instance->App;

    app->CurrentTimeNs = 0;
    app->DiscreteStatesEvaluated = false;

    app->CurrentSlot = 1;
    app->CurrentCycle = 0;
    app->CurrentIteration = 0;

    app->ClusterConfigValid = false;
    app->StartTimeValid = false;
}

void App_Free(AppType* instance)
{
    free(instance);
}

static void ProcessNonDataOperations(FmuInstance* instance, fmi3UInt32 nodeId)
{
    AppType* app = instance->App;

    NodeData* nodeData = &instance->App->Node[nodeId];

    while (true)
    {
        const fmi3LsBusOperationHeader* nextReceiveOperation = OperationBuffer_Peek(&nodeData->RxOperationBuffer);

        if (nextReceiveOperation == NULL)
        {
            break;
        }

        if (nextReceiveOperation->opCode == FMI3_LS_BUS_FLEXRAY_OP_TRANSMIT)
        {
            break;
        }

        if (nextReceiveOperation->opCode == FMI3_LS_BUS_FLEXRAY_OP_CONFIGURATION)
        {
            const fmi3LsBusFlexRayOperationConfiguration* nextReceiveOperationConfig =
                (const fmi3LsBusFlexRayOperationConfiguration*)nextReceiveOperation;

            if (nextReceiveOperationConfig->parameterType == FMI3_LS_BUS_FLEXRAY_CONFIG_PARAM_TYPE_FLEXRAY_CONFIG)
            {
                const fmi3LsBusFlexRayConfigurationFlexRayConfig* flexRayConfig =
                    &nextReceiveOperationConfig->flexRayConfig;

                if (!app->ClusterConfigValid)
                {
                    app->ClusterConfig.MacroTickDurationNs = flexRayConfig->macrotickDuration;
                    app->ClusterConfig.MacroTicksPerCycle = flexRayConfig->macroticksPerCycle;
                    app->ClusterConfig.StaticSlotDurationMt = flexRayConfig->staticSlotLength;
                    app->ClusterConfig.NumberOfCycles = flexRayConfig->cycleCountMax + 1;
                    app->ClusterConfig.NumberOfStaticSlots = flexRayConfig->numberOfStaticSlots;
                    app->ClusterConfig.MinislotDurationMt = flexRayConfig->minislotLength;
                    app->ClusterConfig.ActionPointOffset = flexRayConfig->actionPointOffset;
                    app->ClusterConfig.StaticPayloadLength = flexRayConfig->staticPayloadLength;
                    app->ClusterConfig.MinislotActionPointOffset = flexRayConfig->minislotActionPointOffset;
                    app->ClusterConfig.NumberOfMinislots = flexRayConfig->numberOfMinislots;
                    app->ClusterConfig.MinislotLength = flexRayConfig->minislotLength;
                    app->ClusterConfig.SymbolActionPointOffset = flexRayConfig->symbolActionPointOffset;
                    app->ClusterConfig.SymbolWindowLength = flexRayConfig->symbolWindowLength;
                    app->ClusterConfig.NitLength = flexRayConfig->nitLength;
                    app->ClusterConfig.NMVectorLength = flexRayConfig->nmVectorLength;
                    app->ClusterConfig.DynamicSlotIdleTime = flexRayConfig->dynamicSlotIdleTime;

                    app->ClusterConfigValid = true;

                    LogFmuMessage(instance, fmi3OK, "Info",
                                  "Received FlexRay cluster configuration, configuring bus simulation.");
                }
                else
                {
                    LogFmuMessage(instance, fmi3OK, "Info",
                                  "Received FlexRay cluster configuration, reusing existing.");
                }
            }
            else
            {
                LogFmuMessage(instance, fmi3Warning, "Warning", "Unexpected configuration operation type.");
            }
        }

        if (nextReceiveOperation->opCode == FMI3_LS_BUS_FLEXRAY_OP_SYMBOL)
        {
            const fmi3LsBusFlexRayOperationSymbol* nextReceiveOperationSymbol =
                (const fmi3LsBusFlexRayOperationSymbol*)nextReceiveOperation;

            if (nextReceiveOperationSymbol->cycleId != 0 ||
                nextReceiveOperationSymbol->type != FMI3_LS_BUS_FLEXRAY_SYMBOL_COLLISION_AVOIDANCE_SYMBOL ||
                app->StartTimeValid)
            {
                LogFmuMessage(instance, fmi3Warning, "Warning",
                              "Limitation: Only CAS symbols during startup are supported.");
            }
            else
            {
                // Broadcast frame
                for (fmi3UInt32 otherNodeId = 0; otherNodeId < NUM_NODES; otherNodeId++)
                {
                    if (otherNodeId == nodeId)
                    {
                        continue;
                    }

                    FMI3_LS_BUS_FLEXRAY_CREATE_OP_SYMBOL(
                        &app->Node[otherNodeId].TxBufferInfo, nextReceiveOperationSymbol->cycleId,
                        nextReceiveOperationSymbol->channel, nextReceiveOperationSymbol->type);
                }
            }
        }

        if (nextReceiveOperation->opCode == FMI3_LS_BUS_FLEXRAY_OP_START_COMMUNICATION)
        {
            const fmi3LsBusFlexRayOperationStartCommunication* nextReceiveOperationStartCommunication =
                (const fmi3LsBusFlexRayOperationStartCommunication*)nextReceiveOperation;

            // Communication has already started
            if (app->StartTimeValid)
            {
                LogFmuMessage(instance, fmi3Warning, "Warning", "Unexpected StartCommunication operation");
            }
            else
            {
                // Record FlexRay cycle start time
                app->StartTimeNs = nextReceiveOperationStartCommunication->startTime;
                app->StartTimeValid = true;

                // Broadcast frame
                for (fmi3UInt32 otherNodeId = 0; otherNodeId < NUM_NODES; otherNodeId++)
                {
                    if (otherNodeId == nodeId)
                    {
                        continue;
                    }

                    FMI3_LS_BUS_FLEXRAY_CREATE_OP_START_COMMUNICATION(
                        &app->Node[otherNodeId].TxBufferInfo, nextReceiveOperationStartCommunication->startTime);
                }
            }
        }

        OperationBuffer_Pop(&app->Node[nodeId].RxOperationBuffer);
    }
}

bool App_DoStep(FmuInstance* instance, fmi3Float64 currentTime, fmi3Float64 targetTime)
{
    AppType* app = instance->App;
    app->CurrentTimeNs = ConvertTimeToNs(targetTime);

    for (fmi3UInt32 nodeId = 0; nodeId < NUM_NODES; nodeId++)
    {
        ProcessNonDataOperations(instance, nodeId);
    }

    if (app->StartTimeValid && app->ClusterConfigValid)
    {
        // The current simulation time in macro ticks since the start of the FlexRay cluster
        const fmi3UInt64 currentTimeMt =
            (app->CurrentTimeNs - app->StartTimeNs + app->ClusterConfig.MacroTickDurationNs / 2) /
            app->ClusterConfig.MacroTickDurationNs;

        while (true)
        {
            fmi3UInt64 iterationOffsetMt =
                app->CurrentIteration * app->ClusterConfig.NumberOfCycles * app->ClusterConfig.MacroTicksPerCycle;
            fmi3UInt64 cycleEndOffsetMt =
                iterationOffsetMt + ((fmi3UInt64)app->CurrentCycle + 1) * app->ClusterConfig.MacroTicksPerCycle;

            while (true)
            {
                fmi3UInt64 slotStartMt, slotEndMt;
                GetSlotBoundariesMt(&app->ClusterConfig, app->CurrentIteration, app->CurrentCycle, app->CurrentSlot,
                                    &slotStartMt, &slotEndMt);

                // If the next slot has is in the future, we are done iterating
                if (currentTimeMt < slotStartMt)
                {
                    break;
                }

                bool didReceiveFrame = false;
                // Try to receive a frame for this slot
                for (fmi3UInt32 nodeId = 0; nodeId < NUM_NODES; nodeId++)
                {
                    const fmi3LsBusOperationHeader* nextReceiveOperation =
                        OperationBuffer_Peek(&app->Node[nodeId].RxOperationBuffer);
                    if (nextReceiveOperation != NULL && nextReceiveOperation->opCode == FMI3_LS_BUS_FLEXRAY_OP_TRANSMIT)
                    {
                        const fmi3LsBusFlexRayOperationTransmit* nextReceiveOperationTransmit =
                            (const fmi3LsBusFlexRayOperationTransmit*)nextReceiveOperation;

                        if (nextReceiveOperationTransmit->slotId == app->CurrentSlot &&
                            nextReceiveOperationTransmit->cycleId == app->CurrentCycle)
                        {
                            // Dequeue bus operation
                            OperationBuffer_Pop(&app->Node[nodeId].RxOperationBuffer);
                            LogFmuMessage(app->FmuInstance, fmi3OK, "Trace",
                                          "Received frame in cycle %u, slot %u from node %u of with %u bytes of data",
                                          nextReceiveOperationTransmit->cycleId, nextReceiveOperationTransmit->slotId,
                                          nodeId, nextReceiveOperationTransmit->dataLength);

                            if (didReceiveFrame)
                            {
                                LogFmuMessage(app->FmuInstance, fmi3Warning, "Warning",
                                              "Received multiple frames in cycle %u, slot %u",
                                              nextReceiveOperationTransmit->cycleId,
                                              nextReceiveOperationTransmit->slotId);
                                continue;
                            }

                            didReceiveFrame = true;

                            // Forward bus operation and transmit confirmation
                            for (fmi3UInt32 otherNodeId = 0; otherNodeId < NUM_NODES; otherNodeId++)
                            {
                                if (otherNodeId == nodeId)
                                {
                                    FMI3_LS_BUS_FLEXRAY_CREATE_OP_CONFIRM(
                                        &app->Node[otherNodeId].TxBufferInfo, nextReceiveOperationTransmit->cycleId,
                                        nextReceiveOperationTransmit->slotId, nextReceiveOperationTransmit->channel);
                                }
                                else
                                {
                                    FMI3_LS_BUS_FLEXRAY_CREATE_OP_TRANSMIT(
                                        &app->Node[otherNodeId].TxBufferInfo, nextReceiveOperationTransmit->cycleId,
                                        nextReceiveOperationTransmit->slotId, nextReceiveOperationTransmit->channel,
                                        nextReceiveOperationTransmit->startupFrameIndicator,
                                        nextReceiveOperationTransmit->syncFrameIndicator,
                                        nextReceiveOperationTransmit->nullFrameIndicator,
                                        nextReceiveOperationTransmit->payloadPreambleIndicator,
                                        nextReceiveOperationTransmit->dataLength, nextReceiveOperationTransmit->data);
                                }
                            }
                        }
                    }
                }

                if (currentTimeMt >= slotEndMt || didReceiveFrame)
                {
                    app->CurrentSlot++;
                }
                else
                {
                    break;
                }
            }

            if (currentTimeMt < cycleEndOffsetMt)
            {
                break;
            }

            app->CurrentCycle++;
            app->CurrentSlot = 1;

            if (app->CurrentCycle >= app->ClusterConfig.NumberOfCycles)
            {
                app->CurrentCycle = 0;
                app->CurrentIteration++;
            }
        }
    }

    return false;
}

void App_EvaluateDiscreteStates(FmuInstance* instance)
{
    for (fmi3UInt32 nodeId = 0; nodeId < NUM_NODES; nodeId++)
    {
        NodeData* nodeData = &instance->App->Node[nodeId];

        if (nodeData->RxClock == fmi3ClockActive)
        {
            // Copy all received bus operations to the reception bus operation buffer
            const fmi3LsBusOperationHeader* operation = NULL;
            size_t readPosition = 0;
            while (FMI3_LS_BUS_READ_NEXT_OPERATION_DIRECT(nodeData->RxBuffer, nodeData->RxBufferSize, readPosition,
                                                          operation) == fmi3True)
            {
                if (!OperationBuffer_Push(&nodeData->RxOperationBuffer, operation))
                {
                    LogFmuMessage(instance, fmi3OK, "Warning",
                                  "A bus operation has been dropped because it is either too large or the receive "
                                  "operation buffer "
                                  "is full.");
                }

                ProcessNonDataOperations(instance, nodeId);
            }
        }
    }

    for (fmi3UInt32 nodeId = 0; nodeId < NUM_NODES; nodeId++)
    {
        // Update the variable length from the buffer info
        instance->App->Node[nodeId].TxBufferSize = FMI3_LS_BUS_BUFFER_LENGTH(&instance->App->Node[nodeId].TxBufferInfo);
        UpdateCountdownClock(instance, nodeId);
    }
}

void App_UpdateDiscreteStates(FmuInstance* instance)
{
    for (fmi3UInt32 nodeId = 0; nodeId < NUM_NODES; nodeId++)
    {
        NodeData* nodeData = &instance->App->Node[nodeId];

        if (nodeData->TxClock == fmi3ClockActive)
        {
            // Invalidate the currently configured clock interval
            nodeData->NextTxClockTimeSet = fmi3False;
            nodeData->NextTxClockTimeUpdated = fmi3False;

            // Reset the TX clock and buffer
            nodeData->TxClock = fmi3ClockInactive;
            FMI3_LS_BUS_BUFFER_INFO_RESET(&nodeData->TxBufferInfo);

            // Update the countdown clock interval
            UpdateCountdownClock(instance, nodeId);
        }

        if (nodeData->RxClock == fmi3ClockActive)
        {
            // Reset the RX clock
            nodeData->RxClock = fmi3ClockInactive;
        }
    }
}

bool App_SetBoolean(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Boolean value)
{
    return false;
}

bool App_GetBoolean(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Boolean* value)
{
    switch (valueReference)
    {
        case FMU_VAR_NODE1_BUS_NOTIFICATIONS:
        case FMU_VAR_NODE2_BUS_NOTIFICATIONS:
        case FMU_VAR_NODE1_DELIVERY_ON_BOUNDARY:
        case FMU_VAR_NODE2_DELIVERY_ON_BOUNDARY:
            *value = true;
            return true;

        default:
            return false;
    }
}

bool App_SetFloat64(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Float64 value)
{
    return false;
}

bool App_SetBinary(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Binary value, size_t valueLength)
{
    if (valueReference == FMU_VAR_NODE1_RX_DATA)
    {
        LogFmuMessage(instance, fmi3OK, "Trace", "Set node 1 RX buffer of %llu bytes", valueLength);
        memcpy(instance->App->Node[0].RxBuffer, value, valueLength);
        instance->App->Node[0].RxBufferSize = valueLength;
        return true;
    }

    if (valueReference == FMU_VAR_NODE2_RX_DATA)
    {
        LogFmuMessage(instance, fmi3OK, "Trace", "Set node 2 RX buffer of %llu bytes", valueLength);
        memcpy(instance->App->Node[1].RxBuffer, value, valueLength);
        instance->App->Node[1].RxBufferSize = valueLength;
        return true;
    }

    return false;
}

bool App_GetBinary(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Binary* value, size_t* valueLength)
{
    if ((valueReference == FMU_VAR_NODE1_TX_DATA || valueReference == FMU_VAR_NODE2_TX_DATA) &&
        !instance->App->DiscreteStatesEvaluated)
    {
        App_EvaluateDiscreteStates(instance);
    }

    if (valueReference == FMU_VAR_NODE1_TX_DATA)
    {
        LogFmuMessage(instance, fmi3OK, "Trace", "Get node 1 TX buffer of %llu bytes",
                      instance->App->Node[0].TxBufferSize);
        *value = instance->App->Node[0].TxBuffer;
        *valueLength = instance->App->Node[0].TxBufferSize;
        return true;
    }

    if (valueReference == FMU_VAR_NODE2_TX_DATA)
    {
        LogFmuMessage(instance, fmi3OK, "Trace", "Get node 2 TX buffer of %llu bytes",
                      instance->App->Node[1].TxBufferSize);
        *value = instance->App->Node[1].TxBuffer;
        *valueLength = instance->App->Node[1].TxBufferSize;
        return true;
    }

    return false;
}

bool App_SetClock(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Clock value)
{
    if (valueReference == FMU_VAR_NODE1_RX_CLOCK)
    {
        LogFmuMessage(instance, fmi3OK, "Trace", "Set node 1 RX clock to %u", value);
        instance->App->Node[0].RxClock = value;
        return true;
    }

    if (valueReference == FMU_VAR_NODE1_TX_CLOCK)
    {
        LogFmuMessage(instance, fmi3OK, "Trace", "Set node 1 TX clock to %u", value);
        instance->App->Node[0].TxClock = value;
        return true;
    }

    if (valueReference == FMU_VAR_NODE2_RX_CLOCK)
    {
        LogFmuMessage(instance, fmi3OK, "Trace", "Set node 2 RX clock to %u", value);
        instance->App->Node[1].RxClock = value;
        return true;
    }

    if (valueReference == FMU_VAR_NODE2_TX_CLOCK)
    {
        LogFmuMessage(instance, fmi3OK, "Trace", "Set node 2 TX clock to %u", value);
        instance->App->Node[1].TxClock = value;
        return true;
    }

    return false;
}

bool App_GetClock(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Clock* value)
{
    return false;
}

bool App_GetIntervalFraction(FmuInstance* instance,
                             fmi3ValueReference valueReference,
                             fmi3UInt64* counter,
                             fmi3UInt64* resolution,
                             fmi3IntervalQualifier* qualifier)
{
    if (valueReference == FMU_VAR_NODE1_TX_CLOCK)
    {
        if (instance->App->Node[0].NextTxClockTimeSet == fmi3False)
        {
            *qualifier = fmi3IntervalNotYetKnown;
            return true;
        }

        if (instance->App->Node[0].NextTxClockTimeUpdated == fmi3True)
        {
            instance->App->Node[0].NextTxClockTimeUpdated = fmi3False;
            *counter = instance->App->Node[0].NextTxClockTimeNs - instance->App->CurrentTimeNs;
            *resolution = 1000000000;
            *qualifier = fmi3IntervalChanged;
            return true;
        }
        else
        {
            *qualifier = fmi3IntervalUnchanged;
            return true;
        }
    }

    if (valueReference == FMU_VAR_NODE2_TX_CLOCK)
    {
        if (instance->App->Node[1].NextTxClockTimeSet == fmi3False)
        {
            *qualifier = fmi3IntervalNotYetKnown;
            return true;
        }

        if (instance->App->Node[1].NextTxClockTimeUpdated == fmi3True)
        {
            instance->App->Node[1].NextTxClockTimeUpdated = fmi3False;
            *counter = instance->App->Node[1].NextTxClockTimeNs - instance->App->CurrentTimeNs;
            *resolution = 1000000000;
            *qualifier = fmi3IntervalChanged;
            return true;
        }
        else
        {
            *qualifier = fmi3IntervalUnchanged;
            return true;
        }
    }

    return false;
}
