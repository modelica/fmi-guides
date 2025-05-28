/**
 * \file App.h
 * \brief Application implementing a FlexRay node.
 */

#include "App.h"

#include <assert.h>
#include <stdlib.h>

#include "FlexRayCommon.h"
#include "fmi3LsBusFlexRay.h"
#include "fmi3LsBusUtil.h"
#include "fmi3LsBusUtilFlexRay.h"

#include "Logging.h"
#include "OperationBuffer.h"

/*
 * \brief Type of an FMU clock.
 */
typedef enum
{
    FMU_CLOCK_TYPE_TRIGGERED,
    FMU_CLOCK_TYPE_COUNTDOWN,
    FMU_CLOCK_TYPE_PERIODIC
} FmuClockType;

/*
 * \brief Identifies variables of this FMU by their value references.
 */
typedef enum
{
    FMU_VAR_RX_DATA = 0,
    FMU_VAR_TX_DATA = 1,
    FMU_VAR_RX_CLOCK = 2,
    FMU_VAR_TX_CLOCK = 3,
    
    FMU_VAR_DELIVERY_ON_BOUNDARY = 127,
    FMU_VAR_BUS_NOTIFICATIONS = 128,
    FMU_VAR_IS_SECOND_NODE = 129
} FmuVariables;

/*
 * \brief The type of clock used by this FMU.
 */
static FmuClockType TxClockType = FMU_CLOCK_TYPE_COUNTDOWN;

/**
 * \brief A FlexRay buffer which allows transmitting or receiving in a given FlexRay slot.
 */
typedef struct
{
    fmi3LsBusFlexRayChannel Channel;
    fmi3LsBusFlexRayCycleId CycleId;
    fmi3LsBusFlexRaySlotId SlotId;
    fmi3Boolean IsStartup;  // Only for transmission

    fmi3Byte Data[64];
    fmi3UInt8 DataLength;
    bool IsValid;
} FlexRayBuffer;

#define NUMBER_OF_TRANSMIT_BUFFERS (3)
#define NUMBER_OF_RECEIVE_BUFFERS (3)

#define TX_TASK_INTERVAL (1000000)
#define RX_TASK_INTERVAL (1000000)

/**
 * The current phase of the FlexRay startup.
 */
typedef enum
{
    STARTUP_PHASE_COLDSTART_LISTEN_WAIT_SEND_CAS,   /**< Wait to send CAS, part of "Coldstart Listen" */
    STARTUP_PHASE_COLDSTART_LISTEN_WAIT_START_COMM, /**< Wait to send StartCommunication, part of "Coldstart Listen" */
    STARTUP_PHASE_COLDSTART_LISTEN_PASSIVE, /**< Wait to receive StartCommunication, part of "Coldstart Listen" */
    STARTUP_PHASE_COLDSTART_COLLISION_RESOLUTION,
    STARTUP_PHASE_COLDSTART_CONSISTENCY_CHECK,
    STARTUP_PHASE_INTEGRATION_COLDSTART_CHECK,
    STARTUP_PHASE_COLDSTART_JOIN,
    STARTUP_PHASE_COMPLETE
} StartupPhase;

/**
 * \brief Instance-specific data of this FMU.
 */
struct AppType
{
    FmuInstance* FmuInstance; /**< The FMU instance. */

    fmi3Byte RxBuffer[2048];              /**< The FMI-LS-BUS reception buffer. */
    fmi3UInt64 RxBufferSize;              /**< The current size of the FMI-LS-BUS reception buffer. */
    fmi3LsBusUtilBufferInfo RxBufferInfo; /**< The reception buffer info. */
    fmi3Clock RxClock;                    /**< The FMI-LS-BUS reception clock. */

    fmi3Byte TxBuffer[2048];              /**< The FMI-LS-BUS transmission buffer. */
    fmi3UInt64 TxBufferSize;              /**< The current size of the FMI-LS-BUS transmission buffer. */
    fmi3LsBusUtilBufferInfo TxBufferInfo; /**< The transmission buffer info. */
    fmi3Clock TxClock;                    /**< The FMI-LS-BUS transmission clock. */

    fmi3Boolean BusNotificationsEnabled; /**< Whether bus notifications are enabled. */
    fmi3Boolean IsSecondFmu;             /**< Whether this FMU is the second demo FMU. */

    fmi3Boolean DiscreteStatesEvaluated; /**< Whether the discrete states of the FMU have been evaluated. */

    OperationBuffer RxOperationBuffer; /**< The buffer for received bus operations. */

    fmi3UInt64 NextTxClockTimeNs;       /**< The currently configured time of the TX (countdown) clock. */
    fmi3Boolean NextTxClockTimeSet;     /**< Whether the TX (countdown) clock is currently configured. */
    fmi3Boolean NextTxClockTimeUpdated; /**< Whether the TX (countdown) clock has been reconfigured. */

    fmi3UInt64 CurrentTimeNs; /**< The current simulation time in ns. */

    ClusterConfig ClusterConfig; /**< The FlexRay cluster configuration. */

    FlexRayBuffer TransmitBuffers[NUMBER_OF_TRANSMIT_BUFFERS]; /**< The FlexRay transmission buffers. */
    FlexRayBuffer ReceiveBuffers[NUMBER_OF_RECEIVE_BUFFERS];   /**< The FlexRay reception buffers. */

    fmi3UInt8 NextTransmitBuffer;  /**< The next pending transmit buffer. */
    fmi3UInt8 NextReceiveBuffer;   /**< The next pending reception buffer. */
    fmi3UInt64 CurrentRxIteration; /**< The iteration in which the next transmit buffer is pending. */
    fmi3UInt64 CurrentTxIteration; /**< The iteration in which the next reception buffer is pending. */

    fmi3UInt64 StartTimeNs; /**< The start time of the first FlexRay cycle. */

    fmi3UInt64 CurrentIteration; /**< The current FlexRay iteration that is being processed. */
    fmi3UInt8 CurrentCycle;      /**< The current FlexRay cycle that is being processed. */

    fmi3Boolean BusNotificationPending; /**< Whether a bus notification is currently pending. */

    StartupPhase CurrentStartupPhase; /**< The current phase of the FlexRay startup. */
    fmi3UInt64 NextStartupTransition; /**< The time at which the next event of the FlexRay startup takes place. */

    fmi3UInt16 StartupFramesSeenInCycle;          /**< The number of startup frames seen in the current cycle. */
    fmi3UInt64 CyclesWithAtLeastOneStartupFrame;  /**< The number of FlexRay cycles where at least one startup frame has
                                                     been observed. */
    fmi3UInt64 CyclesWithAtLeastTwoStartupFrames; /**< The number of FlexRay cycles where at least two startup frames
                                                     have been observed. */

    fmi3UInt64 NextTxTaskTime; /**< The next time at which the TX task shall be executed. */
    fmi3UInt64 NextRxTaskTime; /**< The next time at which the RX task shall be executed. */
    fmi3UInt8 TxCounter;       /**< The counter value which is sent by the TX task. */
};

/**
 * Determines the time of the next pending FlexRay transmission.
 *
 * \param app The FMU application instance data.
 * \param slotOffset Whether to examine the next pending transmission buffer (0) or the next one (1).
 * \return The simulation time of the next pending FlexRay transmission.
 */
static fmi3UInt64 GetNextTxBufferTime(const AppType* app, const fmi3UInt8 slotOffset)
{
    assert(slotOffset <= NUMBER_OF_TRANSMIT_BUFFERS);
    fmi3UInt8 currentBuffer = app->NextTransmitBuffer + slotOffset;

    fmi3UInt64 currentIteration = app->CurrentTxIteration;
    if (currentBuffer > NUMBER_OF_TRANSMIT_BUFFERS)
    {
        currentBuffer = currentBuffer % NUMBER_OF_TRANSMIT_BUFFERS;
        currentIteration++;
    }

    const FlexRayBuffer* nextBuffer = &app->TransmitBuffers[currentBuffer];

    fmi3UInt64 slotStartMt, slotEndMt;
    GetSlotBoundariesMt(&app->ClusterConfig, currentIteration, nextBuffer->CycleId, nextBuffer->SlotId, &slotStartMt,
                        &slotEndMt);

    return app->StartTimeNs + slotStartMt * app->ClusterConfig.MacroTickDurationNs;
}

/**
 * Converts a time from seconds to nanoseconds.
 *
 * \param time The time in seconds.
 * \return The time in nanoseconds.
 */
static fmi3UInt64 ConvertTimeToNs(fmi3Float64 time)
{
    return (fmi3UInt64)(time * 1.0E9 + 0.5);
}

/**
 * Updates the time of the next known bus event.
 *
 * \param instance The FMU instance.
 * \param nextEventTimeNs The simulation time of the next bus event.
 * \param nextEventTimeValid Whether the next bus event time is known.
 */
static void SetNextBusEventTime(const FmuInstance* instance,
                                const fmi3UInt64 nextEventTimeNs,
                                const bool nextEventTimeValid)
{
    if (nextEventTimeValid)
    {
        if (instance->App->NextTxClockTimeSet == fmi3False || instance->App->NextTxClockTimeNs != nextEventTimeNs)
        {
            instance->App->NextTxClockTimeNs = nextEventTimeNs;
            instance->App->NextTxClockTimeSet = fmi3True;
            instance->App->NextTxClockTimeUpdated = fmi3True;
        }
    }
    else
    {
        instance->App->NextTxClockTimeSet = fmi3False;
    }
}

/**
 * Updates the next time of the transmit countdown clock.
 *
 * \param instance The FMU instance.
 */
static void UpdateCountdownClock(const FmuInstance* instance)
{
    if (TxClockType != FMU_CLOCK_TYPE_COUNTDOWN)
    {
        return;
    }

    const bool messagesAvailable = !FMI3_LS_BUS_BUFFER_IS_EMPTY(&instance->App->TxBufferInfo);
    if (messagesAvailable)
    {
        SetNextBusEventTime(instance, instance->App->CurrentTimeNs, true);
        return;
    }

    if (instance->App->CurrentStartupPhase == STARTUP_PHASE_COLDSTART_LISTEN_WAIT_SEND_CAS ||
        instance->App->CurrentStartupPhase == STARTUP_PHASE_COLDSTART_LISTEN_WAIT_START_COMM)
    {
        SetNextBusEventTime(instance, instance->App->NextStartupTransition, true);
        return;
    }

    if (instance->App->CurrentStartupPhase == STARTUP_PHASE_COLDSTART_LISTEN_PASSIVE)
    {
        SetNextBusEventTime(instance, 0, false);
        return;
    }

    fmi3UInt64 nextTransmitBufferTime = GetNextTxBufferTime(instance->App, 0);

    // The next triggering is at the current point in time -> We should use the next transmit opportunity for setting
    // the clock
    if (nextTransmitBufferTime <= instance->App->CurrentTimeNs)
    {
        // Check whether we need to advance an iteration
        nextTransmitBufferTime = GetNextTxBufferTime(instance->App, 1);
    }

    SetNextBusEventTime(instance, nextTransmitBufferTime, true);
}

AppType* App_Instantiate(FmuInstance* instance)
{
    AppType* app = calloc(1, sizeof(AppType));
    if (app == NULL)
    {
        return NULL;
    }

    app->FmuInstance = instance;

    app->BusNotificationsEnabled = fmi3False;
    app->IsSecondFmu = fmi3False;

    FMI3_LS_BUS_BUFFER_INFO_INIT(&app->TxBufferInfo, app->TxBuffer, sizeof app->TxBuffer);

    OperationBuffer_Init(&app->RxOperationBuffer);

    return app;
}

void App_Init(FmuInstance* instance)
{
    AppType* app = instance->App;

    app->StartTimeNs = 0;
    app->CurrentTxIteration = 0;
    app->CurrentRxIteration = 0;
    app->NextTransmitBuffer = 0;
    app->NextReceiveBuffer = 0;

    app->CurrentIteration = 0;
    app->CurrentCycle = 0;
    app->BusNotificationPending = fmi3False;

    app->ClusterConfig.MacroTickDurationNs = 1000;
    app->ClusterConfig.MacroTicksPerCycle = 1000;
    app->ClusterConfig.StaticSlotDurationMt = 100;
    app->ClusterConfig.NumberOfCycles = 2;
    app->ClusterConfig.NumberOfStaticSlots = 2;
    app->ClusterConfig.MinislotDurationMt = 10;
    app->ClusterConfig.ActionPointOffset = 1;
    app->ClusterConfig.StaticPayloadLength = 64;
    app->ClusterConfig.MinislotActionPointOffset = 1;
    app->ClusterConfig.NumberOfMinislots = 10;
    app->ClusterConfig.MinislotLength = 10;
    app->ClusterConfig.SymbolActionPointOffset = 1;
    app->ClusterConfig.SymbolWindowLength = 10;
    app->ClusterConfig.NitLength = 2;
    app->ClusterConfig.NMVectorLength = 0;
    app->ClusterConfig.DynamicSlotIdleTime = 0;

    if (app->IsSecondFmu == fmi3False)
    {
        app->TransmitBuffers[0].Channel = FMI3_LS_BUS_FLEXRAY_CHANNEL_A;
        app->TransmitBuffers[0].CycleId = 0;
        app->TransmitBuffers[0].SlotId = 1;
        app->TransmitBuffers[0].IsStartup = fmi3True;
        app->TransmitBuffers[1].Channel = FMI3_LS_BUS_FLEXRAY_CHANNEL_A;
        app->TransmitBuffers[1].CycleId = 0;
        app->TransmitBuffers[1].SlotId = 3;
        app->TransmitBuffers[1].IsStartup = fmi3False;
        app->TransmitBuffers[2].Channel = FMI3_LS_BUS_FLEXRAY_CHANNEL_A;
        app->TransmitBuffers[2].CycleId = 1;
        app->TransmitBuffers[2].SlotId = 1;
        app->TransmitBuffers[2].IsStartup = fmi3True;

        app->ReceiveBuffers[0].Channel = FMI3_LS_BUS_FLEXRAY_CHANNEL_A;
        app->ReceiveBuffers[0].CycleId = 0;
        app->ReceiveBuffers[0].SlotId = 2;
        app->ReceiveBuffers[1].Channel = FMI3_LS_BUS_FLEXRAY_CHANNEL_A;
        app->ReceiveBuffers[1].CycleId = 0;
        app->ReceiveBuffers[1].SlotId = 4;
        app->ReceiveBuffers[2].Channel = FMI3_LS_BUS_FLEXRAY_CHANNEL_A;
        app->ReceiveBuffers[2].CycleId = 1;
        app->ReceiveBuffers[2].SlotId = 2;
    }
    else
    {
        app->TransmitBuffers[0].Channel = FMI3_LS_BUS_FLEXRAY_CHANNEL_A;
        app->TransmitBuffers[0].CycleId = 0;
        app->TransmitBuffers[0].SlotId = 2;
        app->TransmitBuffers[0].IsStartup = fmi3True;
        app->TransmitBuffers[1].Channel = FMI3_LS_BUS_FLEXRAY_CHANNEL_A;
        app->TransmitBuffers[1].CycleId = 0;
        app->TransmitBuffers[1].SlotId = 4;
        app->TransmitBuffers[1].IsStartup = fmi3False;
        app->TransmitBuffers[2].Channel = FMI3_LS_BUS_FLEXRAY_CHANNEL_A;
        app->TransmitBuffers[2].CycleId = 1;
        app->TransmitBuffers[2].SlotId = 2;
        app->TransmitBuffers[2].IsStartup = fmi3True;

        app->ReceiveBuffers[0].Channel = FMI3_LS_BUS_FLEXRAY_CHANNEL_A;
        app->ReceiveBuffers[0].CycleId = 0;
        app->ReceiveBuffers[0].SlotId = 1;
        app->ReceiveBuffers[1].Channel = FMI3_LS_BUS_FLEXRAY_CHANNEL_A;
        app->ReceiveBuffers[1].CycleId = 0;
        app->ReceiveBuffers[1].SlotId = 3;
        app->ReceiveBuffers[2].Channel = FMI3_LS_BUS_FLEXRAY_CHANNEL_A;
        app->ReceiveBuffers[2].CycleId = 1;
        app->ReceiveBuffers[2].SlotId = 1;
    }

    for (fmi3UInt8 i = 0; i < NUMBER_OF_TRANSMIT_BUFFERS; i++)
    {
        app->TransmitBuffers[i].IsValid = false;
    }

    for (fmi3UInt8 i = 0; i < NUMBER_OF_RECEIVE_BUFFERS; i++)
    {
        app->ReceiveBuffers[i].IsValid = false;
    }

    app->CurrentTimeNs = 0;

    app->NextTxTaskTime = TX_TASK_INTERVAL;
    app->NextRxTaskTime = RX_TASK_INTERVAL;

    app->TxCounter = 0;

    if (app->IsSecondFmu == fmi3False)
    {
        // Schedule CAS at 10ms (normally this may be randomized)
        app->NextStartupTransition = 10ull * 1000 * 1000;
    }
    else
    {
        // Schedule CAS at 20ms (normally this may be randomized)
        app->NextStartupTransition = 20ull * 1000 * 1000;
    }

    app->CurrentStartupPhase = STARTUP_PHASE_COLDSTART_LISTEN_WAIT_SEND_CAS;
    app->StartupFramesSeenInCycle = 0;
    app->CyclesWithAtLeastOneStartupFrame = 0;
    app->CyclesWithAtLeastTwoStartupFrames = 0;

    FMI3_LS_BUS_FLEXRAY_CREATE_OP_CONFIGURATION_FLEXRAY_CONFIG(
        &instance->App->TxBufferInfo, instance->App->ClusterConfig.MacroTickDurationNs,
        instance->App->ClusterConfig.MacroTicksPerCycle, instance->App->ClusterConfig.NumberOfCycles - 1,
        instance->App->ClusterConfig.ActionPointOffset, instance->App->ClusterConfig.StaticSlotDurationMt,
        instance->App->ClusterConfig.NumberOfStaticSlots, instance->App->ClusterConfig.StaticPayloadLength,
        instance->App->ClusterConfig.MinislotActionPointOffset, instance->App->ClusterConfig.NumberOfMinislots,
        instance->App->ClusterConfig.MinislotLength, instance->App->ClusterConfig.SymbolActionPointOffset,
        instance->App->ClusterConfig.SymbolWindowLength, instance->App->ClusterConfig.NitLength,
        instance->App->ClusterConfig.NMVectorLength, instance->App->ClusterConfig.DynamicSlotIdleTime,
        FMI3_LS_BUS_FLEXRAY_CONFIG_PARAM_COLDSTART_NODE_TYPE_TT_D_COLDSTART_NODE);

    UpdateCountdownClock(instance);
}

void App_Free(AppType* instance)
{
    free(instance);
}

void TxTask(FmuInstance* instance)
{
    AppType* app = instance->App;

    app->TransmitBuffers[0].Data[0] = app->TxCounter;
    app->TransmitBuffers[0].DataLength = 1;
    app->TransmitBuffers[0].IsValid = true;
    LogFmuMessage(instance, fmi3OK, "Info", "Wrote %u to buffer 0", app->TxCounter);

    app->TransmitBuffers[1].Data[0] = app->TxCounter;
    app->TransmitBuffers[1].DataLength = 1;
    app->TransmitBuffers[1].IsValid = true;
    LogFmuMessage(instance, fmi3OK, "Info", "Wrote %u to buffer 1", app->TxCounter);

    app->TxCounter++;
}

static void RxTask(FmuInstance* instance)
{
    AppType* app = instance->App;

    if (app->ReceiveBuffers[0].IsValid)
    {
        LogFmuMessage(instance, fmi3OK, "Info", "Read %u from buffer 0", app->ReceiveBuffers[0].Data[0]);
        app->ReceiveBuffers[0].IsValid = false;
    }

    if (app->ReceiveBuffers[1].IsValid)
    {
        LogFmuMessage(instance, fmi3OK, "Info", "Read %u from buffer 1", app->ReceiveBuffers[1].Data[0]);
        app->ReceiveBuffers[1].IsValid = false;
    }
}

static void ScheduleTasks(FmuInstance* instance)
{
    AppType* app = instance->App;

    if (app->CurrentTimeNs >= app->NextTxTaskTime)
    {
        TxTask(instance);
        app->NextTxTaskTime += TX_TASK_INTERVAL;
    }

    if (app->CurrentTimeNs >= app->NextRxTaskTime)
    {
        RxTask(instance);

        app->NextRxTaskTime += RX_TASK_INTERVAL;
    }
}

static void ProcessNonDataOperations(FmuInstance* instance)
{
    AppType* app = instance->App;

    while (true)
    {
        const fmi3LsBusOperationHeader* nextReceiveOperation = OperationBuffer_Peek(&app->RxOperationBuffer);

        if (nextReceiveOperation == NULL)
        {
            break;
        }

        if (nextReceiveOperation->opCode == FMI3_LS_BUS_FLEXRAY_OP_TRANSMIT)
        {
            break;
        }

        if (nextReceiveOperation->opCode == FMI3_LS_BUS_FLEXRAY_OP_SYMBOL)
        {
            const fmi3LsBusFlexRayOperationSymbol* nextReceiveOperationSymbol =
                (const fmi3LsBusFlexRayOperationSymbol*)nextReceiveOperation;

            if (nextReceiveOperationSymbol->type == FMI3_LS_BUS_FLEXRAY_SYMBOL_COLLISION_AVOIDANCE_SYMBOL)
            {
                if (instance->App->CurrentStartupPhase == STARTUP_PHASE_COLDSTART_LISTEN_WAIT_SEND_CAS ||
                    instance->App->CurrentStartupPhase == STARTUP_PHASE_COLDSTART_LISTEN_WAIT_START_COMM ||
                    instance->App->CurrentStartupPhase == STARTUP_PHASE_COLDSTART_LISTEN_PASSIVE)
                {
                    LogFmuMessage(instance, fmi3OK, "Info", "Received CAS symbol");
                    LogFmuMessage(instance, fmi3OK, "Info", "Moving to startup phase COLDSTART_LISTEN_PASSIVE");

                    instance->App->CurrentStartupPhase = STARTUP_PHASE_COLDSTART_LISTEN_PASSIVE;
                    instance->App->NextStartupTransition = 0;
                    UpdateCountdownClock(instance);
                }
                else
                {
                    LogFmuMessage(instance, fmi3Warning, "Warning", "Unexpected CAS symbol");
                }
            }
            else
            {
                LogFmuMessage(instance, fmi3Warning, "Warning", "Received unexpected symbol");
            }
        }

        if (nextReceiveOperation->opCode == FMI3_LS_BUS_FLEXRAY_OP_START_COMMUNICATION)
        {
            const fmi3LsBusFlexRayOperationStartCommunication* nextReceiveOperationStartCommunication =
                (const fmi3LsBusFlexRayOperationStartCommunication*)nextReceiveOperation;

            if (instance->App->CurrentStartupPhase == STARTUP_PHASE_COLDSTART_LISTEN_WAIT_SEND_CAS ||
                instance->App->CurrentStartupPhase == STARTUP_PHASE_COLDSTART_LISTEN_WAIT_START_COMM ||
                instance->App->CurrentStartupPhase == STARTUP_PHASE_COLDSTART_LISTEN_PASSIVE)
            {
                LogFmuMessage(instance, fmi3OK, "Info", "Received StartCommunication operation with start with %llu",
                              nextReceiveOperationStartCommunication->startTime);
                LogFmuMessage(instance, fmi3OK, "Info", "Moving to startup phase INTEGRATION_COLDSTART_CHECK");

                instance->App->CurrentStartupPhase = STARTUP_PHASE_INTEGRATION_COLDSTART_CHECK;
                instance->App->NextStartupTransition = 0;
                instance->App->StartTimeNs = nextReceiveOperationStartCommunication->startTime;
                UpdateCountdownClock(instance);
            }
            else
            {
                LogFmuMessage(instance, fmi3Warning, "Warning", "Unexpected StartCommunication operation");
            }
        }

        if (nextReceiveOperation->opCode == FMI3_LS_BUS_FLEXRAY_OP_CONFIRM)
        {
            LogFmuMessage(instance, fmi3OK, "Trace", "Received transmission confirmation");
            app->BusNotificationPending = fmi3False;
        }

        if (nextReceiveOperation->opCode == FMI3_LS_BUS_FLEXRAY_OP_BUS_ERROR)
        {
            LogFmuMessage(instance, fmi3OK, "Trace", "Received bus error indication");
            app->BusNotificationPending = fmi3False;
        }

        OperationBuffer_Pop(&app->RxOperationBuffer);
    }
}

/**
 * Processes all transmission and reception that are pending in the current simulation time step.
 *
 * \param app The application instance.
 */
void ProcessTxRxBuffers(AppType* app)
{
    // The current simulation time in macro ticks since the start of the FlexRay cluster
    const fmi3UInt64 currentTimeMt =
        (app->CurrentTimeNs - app->StartTimeNs + app->ClusterConfig.MacroTickDurationNs / 2) /
        app->ClusterConfig.MacroTickDurationNs;

    // Iterate over all FlexRay cycles that occur(ed) in the current time step.
    while (true)
    {
        fmi3UInt64 iterationOffsetMt =
            app->CurrentIteration * app->ClusterConfig.NumberOfCycles * app->ClusterConfig.MacroTicksPerCycle;
        fmi3UInt64 cycleEndOffsetMt =
            iterationOffsetMt + ((fmi3UInt64)app->CurrentCycle + 1) * app->ClusterConfig.MacroTicksPerCycle;

        // Receive all pending frames of this cycle
        while (true)
        {
            // Retrieve the next receive buffer and make sure it occurs in the current cycle, otherwise stop
            FlexRayBuffer* nextReceiveBuffer = &app->ReceiveBuffers[app->NextReceiveBuffer];
            if (app->CurrentIteration != app->CurrentRxIteration || app->CurrentCycle != nextReceiveBuffer->CycleId)
            {
                break;
            }

            fmi3UInt64 slotStartMt, slotEndMt;
            GetSlotBoundariesMt(&app->ClusterConfig, app->CurrentIteration, app->CurrentCycle,
                                nextReceiveBuffer->SlotId, &slotStartMt, &slotEndMt);

            // If the next slot has is in the future, we are done iterating
            if (currentTimeMt < slotStartMt)
            {
                break;
            }

            bool didReceiveFrame = false;
            const fmi3LsBusOperationHeader* nextReceiveOperation = OperationBuffer_Peek(&app->RxOperationBuffer);
            if (nextReceiveOperation != NULL && nextReceiveOperation->opCode == FMI3_LS_BUS_FLEXRAY_OP_TRANSMIT)
            {
                const fmi3LsBusFlexRayOperationTransmit* nextReceiveOperationTransmit =
                    (const fmi3LsBusFlexRayOperationTransmit*)nextReceiveOperation;

                if (nextReceiveBuffer->Channel == nextReceiveOperationTransmit->channel &&
                    nextReceiveBuffer->CycleId == nextReceiveOperationTransmit->cycleId &&
                    nextReceiveBuffer->SlotId == nextReceiveOperationTransmit->slotId)
                {
                    // Dequeue bus operation
                    OperationBuffer_Pop(&app->RxOperationBuffer);
                    LogFmuMessage(app->FmuInstance, fmi3OK, "Debug",
                                  "Received frame in cycle %u, slot %u of with %u bytes of data",
                                  nextReceiveOperationTransmit->cycleId, nextReceiveOperationTransmit->slotId,
                                  nextReceiveOperationTransmit->dataLength);

                    if (nextReceiveOperationTransmit->nullFrameIndicator == FMI3_LS_BUS_FALSE)
                    {
                        // Note: This is not secure at all, check buffer size in real world code
                        memcpy(nextReceiveBuffer->Data, nextReceiveOperationTransmit->data,
                               nextReceiveOperationTransmit->dataLength);
                        nextReceiveBuffer->IsValid = true;
                    }
                    didReceiveFrame = true;

                    if (nextReceiveOperationTransmit->startupFrameIndicator == FMI3_LS_BUS_TRUE)
                    {
                        app->StartupFramesSeenInCycle++;
                    }

                    ProcessNonDataOperations(app->FmuInstance);
                }
            }

            if (currentTimeMt > slotEndMt || didReceiveFrame)
            {
                app->NextReceiveBuffer++;
                if (app->NextReceiveBuffer == NUMBER_OF_RECEIVE_BUFFERS)
                {
                    app->NextReceiveBuffer = 0;
                    app->CurrentRxIteration++;
                }
            }
            else
            {
                break;
            }
        }

        // Transmit all pending frames of this cycle
        while (true)
        {
            // Do not transmit if we are waiting for a bus notification
            if (app->BusNotificationPending)
            {
                break;
            }

            // Retrieve the next transmission buffer and make sure it occurs in the current cycle, otherwise stop
            FlexRayBuffer* nextTransmitBuffer = &app->TransmitBuffers[app->NextTransmitBuffer];
            if (app->CurrentIteration != app->CurrentTxIteration || app->CurrentCycle != nextTransmitBuffer->CycleId)
            {
                break;
            }

            fmi3UInt64 slotStartMt, slotEndMt;
            GetSlotBoundariesMt(&app->ClusterConfig, app->CurrentIteration, app->CurrentCycle,
                                nextTransmitBuffer->SlotId, &slotStartMt, &slotEndMt);

            // If the next slot has is in the future, we are done iterating
            if (currentTimeMt < slotStartMt)
            {
                break;
            }

            // If we are a following coldstart node in the INTEGRATION_COLDSTART_CHECK phase, we do not transmit yet
            // but still try to synchronize to the FlexRay cycle
            if (app->CurrentStartupPhase != STARTUP_PHASE_INTEGRATION_COLDSTART_CHECK)
            {
                // If we are still in startup or the buffer contains no valid data, we transmit a null frame
                const bool isNullFrame =
                    app->CurrentStartupPhase != STARTUP_PHASE_COMPLETE || !nextTransmitBuffer->IsValid;

                if (!isNullFrame)
                {
                    LogFmuMessage(app->FmuInstance, fmi3OK, "Debug",
                                  "Transmitting frame in cycle %u, slot %u of with %u bytes of data",
                                  nextTransmitBuffer->CycleId, nextTransmitBuffer->SlotId,
                                  nextTransmitBuffer->DataLength);
                }
                else
                {
                    LogFmuMessage(app->FmuInstance, fmi3OK, "Debug", "Transmitting null frame in cycle %u, slot %u",
                                  nextTransmitBuffer->CycleId, nextTransmitBuffer->SlotId);
                }

                FMI3_LS_BUS_FLEXRAY_CREATE_OP_TRANSMIT(
                    &app->TxBufferInfo, nextTransmitBuffer->CycleId, nextTransmitBuffer->SlotId,
                    nextTransmitBuffer->Channel, nextTransmitBuffer->IsStartup ? fmi3True : fmi3False,
                    nextTransmitBuffer->IsStartup ? fmi3True : fmi3False, isNullFrame ? fmi3True : fmi3False, fmi3False,
                    isNullFrame ? 0 : nextTransmitBuffer->DataLength, nextTransmitBuffer->Data);

                if (nextTransmitBuffer->IsStartup)
                {
                    app->StartupFramesSeenInCycle++;
                }

                // The buffer has been transmitted, invalidate it
                if (!isNullFrame)
                {
                    nextTransmitBuffer->IsValid = false;
                }

                if (app->BusNotificationsEnabled)
                {
                    app->BusNotificationPending = fmi3True;
                }
            }

            app->NextTransmitBuffer++;
            if (app->NextTransmitBuffer == NUMBER_OF_TRANSMIT_BUFFERS)
            {
                app->NextTransmitBuffer = 0;
                app->CurrentTxIteration++;
            }
        }

        if (currentTimeMt < cycleEndOffsetMt)
        {
            break;
        }

        if (app->StartupFramesSeenInCycle >= 1)
        {
            app->CyclesWithAtLeastOneStartupFrame++;
        }
        else
        {
            app->CyclesWithAtLeastOneStartupFrame = 0;
        }

        if (app->StartupFramesSeenInCycle >= 2)
        {
            app->CyclesWithAtLeastTwoStartupFrames++;
        }
        else
        {
            app->CyclesWithAtLeastTwoStartupFrames = 0;
        }

        LogFmuMessage(app->FmuInstance, fmi3OK, "Info",
                      "Seen %u startup frames in the last cycle. The last %u cycles had >= 1 startup frames. "
                      "The last %u cycles had >= 2 startup frames.",
                      app->StartupFramesSeenInCycle, app->CyclesWithAtLeastOneStartupFrame,
                      app->CyclesWithAtLeastTwoStartupFrames);
        app->StartupFramesSeenInCycle = 0;

        // Once we see at least 4 cycles with at least one startup frame, we join the cold start sequence
        if (app->CurrentStartupPhase == STARTUP_PHASE_INTEGRATION_COLDSTART_CHECK &&
            app->CyclesWithAtLeastOneStartupFrame >= 4)
        {
            LogFmuMessage(app->FmuInstance, fmi3OK, "Info", "Moving to startup phase COLDSTART_JOIN");
            app->CurrentStartupPhase = STARTUP_PHASE_COLDSTART_JOIN;
            UpdateCountdownClock(app->FmuInstance);
        }

        // Once we send startup frames for at least 4 cycles, we move to the cold start consistency check where
        // other startup nodes may join
        if (app->CurrentStartupPhase == STARTUP_PHASE_COLDSTART_COLLISION_RESOLUTION &&
            app->CyclesWithAtLeastOneStartupFrame >= 4)
        {
            LogFmuMessage(app->FmuInstance, fmi3OK, "Info", "Moving to startup phase COLDSTART_CONSISTENCY_CHECK");
            app->CurrentStartupPhase = STARTUP_PHASE_COLDSTART_CONSISTENCY_CHECK;
        }

        // Once we see at least 4 cycles with at least two startup frame, we complete the cold start sequence
        if ((app->CurrentStartupPhase == STARTUP_PHASE_COLDSTART_JOIN ||
             app->CurrentStartupPhase == STARTUP_PHASE_COLDSTART_CONSISTENCY_CHECK) &&
            app->CyclesWithAtLeastTwoStartupFrames >= 4)
        {
            LogFmuMessage(app->FmuInstance, fmi3OK, "Info", "FlexRay startup completed");
            app->CurrentStartupPhase = STARTUP_PHASE_COMPLETE;
        }

        app->CurrentCycle++;

        if (app->CurrentCycle >= app->ClusterConfig.NumberOfCycles)
        {
            app->CurrentCycle = 0;
            app->CurrentIteration++;
        }
    }
}

bool App_DoStep(FmuInstance* instance, fmi3Float64 currentTime, fmi3Float64 targetTime)
{
    AppType* app = instance->App;
    app->CurrentTimeNs = ConvertTimeToNs(targetTime);

    ScheduleTasks(instance);

    ProcessNonDataOperations(instance);

    if (instance->App->CurrentStartupPhase == STARTUP_PHASE_COLDSTART_LISTEN_WAIT_SEND_CAS &&
        app->CurrentTimeNs >= instance->App->NextStartupTransition)
    {
        LogFmuMessage(instance, fmi3OK, "Info", "Sending CAS symbol");

        FMI3_LS_BUS_FLEXRAY_CREATE_OP_SYMBOL(&app->TxBufferInfo, 0,
                                             FMI3_LS_BUS_FLEXRAY_CHANNEL_A | FMI3_LS_BUS_FLEXRAY_CHANNEL_B,
                                             FMI3_LS_BUS_FLEXRAY_SYMBOL_COLLISION_AVOIDANCE_SYMBOL);

        fmi3UInt64 cycleDurationNs =
            (fmi3UInt64)app->ClusterConfig.MacroTicksPerCycle * app->ClusterConfig.MacroTickDurationNs;
        // Schedule start of FlexRay communication
        // Align the start time to be a multiple of the cycle time for simplicity
        fmi3UInt64 startTime =
            app->CurrentTimeNs + cycleDurationNs + (cycleDurationNs - app->CurrentTimeNs % cycleDurationNs);

        instance->App->CurrentStartupPhase = STARTUP_PHASE_COLDSTART_LISTEN_WAIT_START_COMM;
        instance->App->NextStartupTransition = startTime;
    }

    if (instance->App->CurrentStartupPhase == STARTUP_PHASE_COLDSTART_LISTEN_WAIT_START_COMM &&
        app->CurrentTimeNs >= instance->App->NextStartupTransition)
    {
        LogFmuMessage(instance, fmi3OK, "Info", "Transmitting StartCommunication operation with start time %llu",
                      instance->App->NextStartupTransition);
        LogFmuMessage(instance, fmi3OK, "Info", "Moving to startup phase COLDSTART_COLLISION_RESOLUTION");

        FMI3_LS_BUS_FLEXRAY_CREATE_OP_START_COMMUNICATION(&app->TxBufferInfo, instance->App->NextStartupTransition);

        instance->App->CurrentStartupPhase = STARTUP_PHASE_COLDSTART_COLLISION_RESOLUTION;
        instance->App->StartTimeNs = instance->App->NextStartupTransition;
        instance->App->NextStartupTransition = 0;
    }

    if (instance->App->CurrentStartupPhase == STARTUP_PHASE_COLDSTART_COLLISION_RESOLUTION ||
        instance->App->CurrentStartupPhase == STARTUP_PHASE_COLDSTART_CONSISTENCY_CHECK ||
        instance->App->CurrentStartupPhase == STARTUP_PHASE_INTEGRATION_COLDSTART_CHECK ||
        instance->App->CurrentStartupPhase == STARTUP_PHASE_COLDSTART_JOIN ||
        instance->App->CurrentStartupPhase == STARTUP_PHASE_COMPLETE)
    {
        ProcessTxRxBuffers(app);
        // UpdateCountdownClock(instance);
    }

    if (TxClockType == FMU_CLOCK_TYPE_TRIGGERED && !FMI3_LS_BUS_BUFFER_IS_EMPTY(&instance->App->TxBufferInfo))
    {
        return true;
    }

    return false;
}

void App_EvaluateDiscreteStates(FmuInstance* instance)
{
    if (TxClockType == FMU_CLOCK_TYPE_TRIGGERED)
    {
        const bool messagesAvailable = !FMI3_LS_BUS_BUFFER_IS_EMPTY(&instance->App->TxBufferInfo);
        instance->App->TxClock = messagesAvailable ? fmi3ClockActive : fmi3ClockInactive;
    }

    if (instance->App->RxClock == fmi3ClockActive)
    {
        // Copy all received bus operations to the reception bus operation buffer
        const fmi3LsBusOperationHeader* operation = NULL;
        size_t readPosition = 0;
        while (FMI3_LS_BUS_READ_NEXT_OPERATION_DIRECT(instance->App->RxBuffer, instance->App->RxBufferSize,
                                                      readPosition, operation) == fmi3True)
        {
            if (!OperationBuffer_Push(&instance->App->RxOperationBuffer, operation))
            {
                LogFmuMessage(instance, fmi3OK, "Warning",
                              "A bus operation has been dropped because it is either too large or the receive "
                              "operation buffer "
                              "is full.");
            }
        }
    }

    // Update the variable length from the buffer info
    instance->App->TxBufferSize = FMI3_LS_BUS_BUFFER_LENGTH(&instance->App->TxBufferInfo);
}

void App_UpdateDiscreteStates(FmuInstance* instance)
{
    if (instance->App->TxClock == fmi3ClockActive)
    {
        // Invalidate the currently configured clock interval
        if (TxClockType == FMU_CLOCK_TYPE_COUNTDOWN)
        {
            instance->App->NextTxClockTimeSet = fmi3False;
            instance->App->NextTxClockTimeUpdated = fmi3False;
        }

        // Reset the TX clock and buffer
        instance->App->TxClock = fmi3ClockInactive;
        FMI3_LS_BUS_BUFFER_INFO_RESET(&instance->App->TxBufferInfo);

        // Update the countdown clock interval
        UpdateCountdownClock(instance);
    }

    if (instance->App->RxClock == fmi3ClockActive)
    {
        // Reset the RX clock
        instance->App->RxClock = fmi3ClockInactive;
    }
}

bool App_SetBoolean(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Boolean value)
{
    if (valueReference == FMU_VAR_DELIVERY_ON_BOUNDARY)
    {
        LogFmuMessage(instance, fmi3OK, "Trace", "%s delivery on boundary", value ? "Enabled" : "Disabled");
        return true;
    }

    if (valueReference == FMU_VAR_BUS_NOTIFICATIONS)
    {
        LogFmuMessage(instance, fmi3OK, "Trace", "%s bus notifications", value ? "Enabled" : "Disabled");
        instance->App->BusNotificationsEnabled = value;
        return true;
    }

    if (valueReference == FMU_VAR_IS_SECOND_NODE)
    {
        LogFmuMessage(instance, fmi3OK, "Trace", "Is second node FMU: %s", value ? "Yes" : "No");
        instance->App->IsSecondFmu = value;
        return true;
    }

    return false;
}

bool App_GetBoolean(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Boolean* value)
{
    return false;
}

bool App_SetFloat64(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Float64 value)
{
    return false;
}

bool App_SetBinary(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Binary value, size_t valueLength)
{
    if (valueReference == FMU_VAR_RX_DATA)
    {
        LogFmuMessage(instance, fmi3OK, "Trace", "Set RX buffer of %llu bytes", valueLength);
        memcpy(instance->App->RxBuffer, value, valueLength);
        instance->App->RxBufferSize = valueLength;
        return true;
    }

    return false;
}

bool App_GetBinary(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Binary* value, size_t* valueLength)
{
    if (valueReference == FMU_VAR_TX_DATA && !instance->App->DiscreteStatesEvaluated)
    {
        App_EvaluateDiscreteStates(instance);
    }

    if (valueReference == FMU_VAR_TX_DATA)
    {
        LogFmuMessage(instance, fmi3OK, "Trace", "Get TX buffer of %llu bytes", instance->App->TxBufferSize);
        *value = instance->App->TxBuffer;
        *valueLength = instance->App->TxBufferSize;
        return true;
    }

    return false;
}

bool App_SetClock(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Clock value)
{
    if (valueReference == FMU_VAR_RX_CLOCK)
    {
        LogFmuMessage(instance, fmi3OK, "Trace", "Set RX clock to %u", value);
        instance->App->RxClock = value;
        return true;
    }

    if (valueReference == FMU_VAR_TX_CLOCK)
    {
        if (TxClockType != FMU_CLOCK_TYPE_COUNTDOWN && TxClockType != FMU_CLOCK_TYPE_PERIODIC)
        {
            LogFmuMessage(instance, fmi3Error, "Error",
                          "SetClock may only be called for countdown and periodic clocks");
            return false;
        }

        LogFmuMessage(instance, fmi3OK, "Trace", "Set TX clock to %u", value);
        instance->App->TxClock = value;
        return true;
    }

    return false;
}

bool App_GetClock(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Clock* value)
{
    if (valueReference == FMU_VAR_TX_CLOCK && !instance->App->DiscreteStatesEvaluated)
    {
        App_EvaluateDiscreteStates(instance);
    }

    if (valueReference == FMU_VAR_TX_CLOCK)
    {
        if (TxClockType != FMU_CLOCK_TYPE_TRIGGERED)
        {
            LogFmuMessage(instance, fmi3Error, "Error", "GetClock may only be called for triggered clocks");
            return false;
        }

        LogFmuMessage(instance, fmi3OK, "Trace", "Get TX clock of %d", instance->App->TxClock);
        *value = instance->App->TxClock;

        // Reset clock since GetClock may only return fmi3ClockActive once per activation
        instance->App->TxClock = fmi3ClockInactive;

        return true;
    }

    return false;
}

bool App_GetIntervalFraction(FmuInstance* instance,
                             fmi3ValueReference valueReference,
                             fmi3UInt64* counter,
                             fmi3UInt64* resolution,
                             fmi3IntervalQualifier* qualifier)
{
    if (valueReference == FMU_VAR_TX_CLOCK)
    {
        if (TxClockType != FMU_CLOCK_TYPE_COUNTDOWN)
        {
            LogFmuMessage(instance, fmi3Error, "Error", "GetIntervalFraction may only be called for countdown clocks");
            return false;
        }

        if (instance->App->NextTxClockTimeSet == fmi3False)
        {
            *qualifier = fmi3IntervalNotYetKnown;
            return true;
        }

        if (instance->App->NextTxClockTimeUpdated == fmi3True)
        {
            instance->App->NextTxClockTimeUpdated = fmi3False;
            *counter = instance->App->NextTxClockTimeNs - instance->App->CurrentTimeNs;
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
