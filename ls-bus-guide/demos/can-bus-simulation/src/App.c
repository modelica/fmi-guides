#include "App.h"

#include <stdlib.h>

#include "fmi3LsBusUtilCan.h"

#include "Logging.h"


/**
 * \brief Identifies variables of this FMU with their value references.
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

    FMU_VAR_BUS_ERROR_PROBABILITY = 255
} FmuVariables;


enum { FrameQueueSize = 128 /**< The maximum size of the frame queue for a single node. */ };


/**
 * \brief Describes a single frame queued to be transmitted.
 */
typedef struct
{
    fmi3LsBusCanId Id;
    fmi3LsBusCanIde Ide;
    fmi3LsBusCanRtr Rtr;
    fmi3LsBusCanDataLength DataLength;
    fmi3Byte Data[64];
} FrameQueueEntry;


/**
 * \brief A queue for frames transmitted by a single node.
 * \note This is implemented as a ring buffer.
 */
typedef struct
{
    FrameQueueEntry Entries[FrameQueueSize];
    fmi3UInt8 WritePos;
    fmi3UInt8 ReadPos;
} FrameQueue;


#define FRAME_QUEUE_INCREMENT(i, n) (((i) + 1) % (n))
#define FRAME_QUEUE_DECREMENT(i, n) ((i) > 0 ? (i)-1 : (n)-1)


/**
 * \brief Type identifying bus nodes.
 */
typedef uint32_t NodeIdType;


enum { NumNodes = 2 /**< Number of terminals for attaching nodes to this bus simulation. */ };


/**
 * \brief Data for a single terminal of this bus FMU.
 */
typedef struct
{
    fmi3Byte RxBuffer[2048];
    fmi3LsBusUtilBufferInfo RxBufferInfo;
    fmi3Clock RxClock;

    fmi3Byte TxBuffer[2048];
    fmi3LsBusUtilBufferInfo TxBufferInfo;
    fmi3Clock TxClock;

    fmi3IntervalQualifier TxClockQualifier;

    FrameQueue FrameQueue;

    fmi3LsBusCanBaudrate BaudRate;
    fmi3LsBusCanArbitrationLostBehavior ArbitrationLostBehavior;
} NodeData;


/**
 * \brief Instance-specific data of this FMU.
 */
struct AppType
{
    fmi3LsBusCanBaudrate BusBaudRate; /**< Current baud rate of the simulated bus. When set to 0, bus communication is disabled. */
    fmi3Float64 BusErrorProbability; /**< Probability that a simulated error occurs for a transmitted frame. */

    NodeData Nodes[NumNodes];

    fmi3UInt64 TxClockCounter;
    fmi3UInt64 TxClockResolution;

    bool TxClockSet;
    bool DiscreteStatesEvaluated;
};


/**
 * \brief Enqueues a frame in this queue.
 * \param queue The queue.
 * \param transmitOp A transmit operation to enqueue.
 * \return Whether the frame could be successfully enqueued.
 */
static bool FrameQueue_Enqueue(FrameQueue* queue, const fmi3LsBusCanOperationCanTransmit* transmitOp)
{
    if (FRAME_QUEUE_INCREMENT(queue->WritePos, FrameQueueSize) == queue->ReadPos)
    {
        return false;
    }

    queue->Entries[queue->WritePos].Id = transmitOp->id;
    queue->Entries[queue->WritePos].Ide = transmitOp->ide;
    queue->Entries[queue->WritePos].Rtr = transmitOp->rtr;
    queue->Entries[queue->WritePos].DataLength = transmitOp->dataLength;

    queue->WritePos = FRAME_QUEUE_INCREMENT(queue->WritePos, FrameQueueSize);

    return true;
}

/**
 * \brief Returns the frame at the front of the queue.
 * \param queue The queue.
 * \return The frame at the front of the queue or NULL, if the queue is empty.
 */
static const FrameQueueEntry* FrameQueue_Peek(const FrameQueue* queue)
{
    if (queue->ReadPos == queue->WritePos)
    {
        return NULL;
    }

    return &queue->Entries[queue->ReadPos];
}

/**
 * \brief Removes the frame at the front of the queue.
 * \param queue The queue.
 * \return The frame at the front of the queue or NULL, if the queue is empty.
 */
static const FrameQueueEntry* FrameQueue_Pop(FrameQueue* queue)
{
    if (queue->ReadPos == queue->WritePos)
    {
        return NULL;
    }

    const FrameQueueEntry* entry = &queue->Entries[queue->ReadPos];
    queue->ReadPos = FRAME_QUEUE_INCREMENT(queue->ReadPos, FrameQueueSize);
    return entry;
}


AppType* App_Instantiate(void)
{
    AppType* app = calloc(1, sizeof(AppType));
    if (app == NULL)
    {
        return NULL;
    }

    // Initialize transmit and receive buffers and other node data
    for (NodeIdType i = 0; i < NumNodes; i++)
    {
        FMI3_LS_BUS_BUFFER_INFO_INIT(&app->Nodes[i].RxBufferInfo, app->Nodes[i].RxBuffer,
                                     sizeof(app->Nodes[i].RxBuffer));
        FMI3_LS_BUS_BUFFER_INFO_INIT(&app->Nodes[i].TxBufferInfo, app->Nodes[i].TxBuffer,
                                     sizeof(app->Nodes[i].TxBuffer));

        app->Nodes[i].TxClockQualifier = fmi3IntervalNotYetKnown;

        app->Nodes[i].ArbitrationLostBehavior =
            FMI3_LS_BUS_CAN_CONFIG_PARAM_ARBITRATION_LOST_BEHAVIOR_BUFFER_AND_RETRANSMIT;
    }

    return app;
}


void App_Free(AppType* instance)
{
    free(instance);
}


bool App_DoStep(FmuInstance* instance, fmi3Float64 currentTime, fmi3Float64 targetTime)
{
    return false;
}


/**
 * \brief Finds the next frame to be transmitted on the simulated CAN cluster.
 *
 * \param[in] instance The FMU instance.
 * \param[out] nextFrame The next frame to be transmitted.
 * \param[out] originator The ID of the node transmitting the frame.
 * \return Whether a frame to be transmitted could be found.
 */
static bool App_GetNextFrame(const FmuInstance* instance, const FrameQueueEntry** nextFrame, NodeIdType* originator)
{
    *nextFrame = NULL;
    for (NodeIdType i = 0; i < NumNodes; i++)
    {
        const FrameQueueEntry* queueTop = FrameQueue_Peek(&instance->App->Nodes[i].FrameQueue);
        if (queueTop != NULL && (*nextFrame == NULL || queueTop->Id < (*nextFrame)->Id))
        {
            *nextFrame = queueTop;
            *originator = i;
        }
    }
    return *nextFrame != NULL;
}


void App_EvaluateDiscreteStates(FmuInstance* instance)
{
    if (instance->App->DiscreteStatesEvaluated)
    {
        return;
    }

    // Process received frames for each node
    for (NodeIdType i = 0; i < NumNodes; i++)
    {
        if (instance->App->Nodes[i].RxClock == fmi3ClockActive)
        {
            // Read all bus operations from the RX buffer
            fmi3LsBusOperationHeader* operation = NULL;
            while (FMI3_LS_BUS_READ_NEXT_OPERATION(&instance->App->Nodes[i].RxBufferInfo, operation))
            {
                if (operation->type == FMI3_LS_BUS_CAN_OP_CAN_TRANSMIT)
                {
                    const fmi3LsBusCanOperationCanTransmit* transmitOp = (fmi3LsBusCanOperationCanTransmit*)operation;
                    LogFmuMessage(instance, fmi3OK, "Info", "Received CAN frame with ID %u and length %u from node %u",
                                  transmitOp->id, transmitOp->dataLength, i + 1);
                    FrameQueue_Enqueue(&instance->App->Nodes[i].FrameQueue, transmitOp);
                }
                else if (operation->type == FMI3_LS_BUS_CAN_OP_CONFIGURATION)
                {
                    const fmi3LsBusCanOperationConfiguration* configOp = (fmi3LsBusCanOperationConfiguration*)operation;
                    if (configOp->parameterType == FMI3_LS_BUS_CAN_CONFIG_PARAM_TYPE_CAN_BAUDRATE)
                    {
                        LogFmuMessage(instance, fmi3OK, "Info",
                                      "Node %u configured baud rate %u", i, configOp->baudrate);
                        instance->App->Nodes[i].BaudRate = configOp->baudrate;
                    }
                    else if (configOp->parameterType == FMI3_LS_BUS_CAN_CONFIG_PARAM_TYPE_ARBITRATION_LOST_BEHAVIOR)
                    {
                        LogFmuMessage(instance, fmi3OK, "Info", "Node %u configured arbitration lost behavior to %s",
                                      i + 1,
                                      configOp->arbitrationLostBehavior ==
                                      FMI3_LS_BUS_CAN_CONFIG_PARAM_ARBITRATION_LOST_BEHAVIOR_BUFFER_AND_RETRANSMIT
                                          ? "BUFFER_AND_RETRANSMIT"
                                          : configOp->arbitrationLostBehavior ==
                                          FMI3_LS_BUS_CAN_CONFIG_PARAM_ARBITRATION_LOST_BEHAVIOR_DISCARD_AND_NOTIFY
                                          ? "DISCARD_AND_NOTIFY"
                                          : "INVALID");
                        instance->App->Nodes[i].ArbitrationLostBehavior = configOp->arbitrationLostBehavior;
                    }
                }
            }

            // Deactivate RX clock and clear RX buffer since all operations should have been processed
            instance->App->Nodes[i].RxClock = fmi3ClockInactive;
            FMI3_LS_BUS_BUFFER_INFO_RESET(&instance->App->Nodes[i].RxBufferInfo);
        }
    }

    // Check baud rate configured by nodes and enable bus communication when configured correctly
    if (instance->App->Nodes[0].BaudRate > 0 && instance->App->Nodes[1].BaudRate > 0)
    {
        if (instance->App->Nodes[0].BaudRate == instance->App->Nodes[1].BaudRate)
        {
            instance->App->BusBaudRate = instance->App->Nodes[0].BaudRate;
        }
        else
        {
            LogFmuMessage(instance, fmi3Warning, "Warning",
                          "Nodes have configured differing baud rates, bus communication is disabled.");
            instance->App->BusBaudRate = 0;
        }
    }
    else
    {
        LogFmuMessage(instance, fmi3OK, "Info",
                      "Not all nodes have configured a baud rate yet, bus communication is disabled.");
        instance->App->BusBaudRate = 0;
    }

    // Transmit frames
    if (instance->App->Nodes[0].TxClock == fmi3ClockActive && instance->App->Nodes[1].TxClock == fmi3ClockActive)
    {
        const FrameQueueEntry* nextFrame;
        NodeIdType nextFrameOriginator;
        if (App_GetNextFrame(instance, &nextFrame, &nextFrameOriginator))
        {
            FrameQueue_Pop(&instance->App->Nodes[nextFrameOriginator].FrameQueue);

            // Randomly decide whether we want to simulate a bus error for this frame
            // Note: rand() is used here for simplicity's sake but not recommended to achieve reproducible simulations
            const fmi3Float64 busErrorRandom =
                (fmi3Float64)rand() /  // NOLINT(clang-diagnostic-bad-function-cast, concurrency-mt-unsafe)
                (fmi3Float64)RAND_MAX;
            const bool simulateError = busErrorRandom < instance->App->BusErrorProbability;

            for (NodeIdType i = 0; i < NumNodes; i++)
            {
                if (!simulateError)
                {
                    // The originator of the frame receives a confirm, everyone else receives the frame
                    if (i == nextFrameOriginator)
                    {
                        LogFmuMessage(instance, fmi3OK, "Trace",
                                      "Sending confirmation for CAN frame with ID %u to node %u", nextFrame->Id, i + 1);
                        FMI3_LS_BUS_CAN_CREATE_OP_CONFIRM(&instance->App->Nodes[i].TxBufferInfo, nextFrame->Id);
                    }
                    else
                    {
                        FMI3_LS_BUS_CAN_CREATE_OP_CAN_TRANSMIT(&instance->App->Nodes[i].TxBufferInfo, nextFrame->Id,
                                                               nextFrame->Ide, nextFrame->Rtr, nextFrame->DataLength,
                                                               nextFrame->Data);
                        LogFmuMessage(instance, fmi3OK, "Trace", "Sending CAN frame with ID %u to node %u", nextFrame->Id,
                                      i + 1);
                    }
                }
                else
                {
                    LogFmuMessage(instance, fmi3OK, "Trace", "Sending error notification for CAN frame with ID %u to node %u",
                                  nextFrame->Id, i + 1);
                    FMI3_LS_BUS_CAN_CREATE_OP_BUS_ERROR(&instance->App->Nodes[i].TxBufferInfo, nextFrame->Id,
                        FMI3_LS_BUS_CAN_BUSERROR_PARAM_ERROR_CODE_CRC_ERROR,
                        FMI3_LS_BUS_CAN_BUSERROR_PARAM_ERROR_FLAG_PRIMARY_ERROR_FLAG,
                        i == nextFrameOriginator ? FMI3_LS_BUS_TRUE : FMI3_LS_BUS_FALSE);
                }

                // If the node still has frames to transmit but configured an arbitration behavior of DISCARD_AND_NOTIFY,
                // discard its frames and notify it since it has lost arbitration.
                while (FrameQueue_Peek(&instance->App->Nodes[i].FrameQueue) &&
                    instance->App->Nodes[i].ArbitrationLostBehavior ==
                    FMI3_LS_BUS_CAN_CONFIG_PARAM_ARBITRATION_LOST_BEHAVIOR_DISCARD_AND_NOTIFY)
                {
                    const FrameQueueEntry* droppedFrame = FrameQueue_Pop(&instance->App->Nodes[i].FrameQueue);
                    FMI3_LS_BUS_CAN_CREATE_OP_ARBITRATION_LOST(&instance->App->Nodes[i].TxBufferInfo, droppedFrame->Id);
                    LogFmuMessage(instance, fmi3OK, "Trace",
                                  "Sending arbitration lost notification for CAN frame with ID %u to node %u",
                                  droppedFrame->Id, i + 1);
                }
            }
        }
        else
        {
            LogFmuMessage(instance, fmi3Warning, "Warning",
                          "TX clock has been triggered but no frames to transmit.");
        }

        // Reset the TX clock state
        instance->App->Nodes[0].TxClockQualifier = fmi3IntervalNotYetKnown;
        instance->App->Nodes[1].TxClockQualifier = fmi3IntervalNotYetKnown;
        instance->App->TxClockSet = false;
    }
    else if (instance->App->Nodes[0].TxClock == fmi3ClockActive || instance->App->Nodes[1].TxClock == fmi3ClockActive)
    {
        // Since both TX countdown clocks are reporting the same intervals, they should trigger in the same instant
        LogFmuMessage(instance, fmi3Error, "Error",
                      "TX clocks for nodes ticked independently, this is not supported by this FMU.");
    }

    // Check that the next transmit interval has not already been set, otherwise we may postpone it by accident.
    if (!instance->App->TxClockSet)
    {
        // Schedule next transmission
        const FrameQueueEntry* nextFrame;
        NodeIdType nextFrameOriginator;
        if (App_GetNextFrame(instance, &nextFrame, &nextFrameOriginator))
        {
            instance->App->Nodes[0].TxClockQualifier = fmi3IntervalChanged;
            instance->App->Nodes[1].TxClockQualifier = fmi3IntervalChanged;
            instance->App->TxClockCounter = 44 + nextFrame->DataLength;
            instance->App->TxClockResolution = instance->App->BusBaudRate;
            instance->App->TxClockSet = true;
        }
    }

    instance->App->DiscreteStatesEvaluated = true;
}


void App_UpdateDiscreteStates(FmuInstance* instance)
{
    if ((instance->App->Nodes[0].TxClock == fmi3ClockActive || instance->App->Nodes[1].TxClock == fmi3ClockActive) &&
        !instance->App->DiscreteStatesEvaluated)
    {
        LogFmuMessage(instance, fmi3Warning, "Warning",
                      "Discrete states should have been evaluated as part of fmi3EvaluateDiscreteStates or fmi3GetBinary");
    }
    else if (!instance->App->DiscreteStatesEvaluated)
    {
        App_EvaluateDiscreteStates(instance);
    }

    // Deactivate all TX clocks and clear TX buffers
    instance->App->Nodes[0].TxClock = fmi3ClockInactive;
    FMI3_LS_BUS_BUFFER_INFO_RESET(&instance->App->Nodes[0].TxBufferInfo);
    instance->App->Nodes[1].TxClock = fmi3ClockInactive;
    FMI3_LS_BUS_BUFFER_INFO_RESET(&instance->App->Nodes[1].TxBufferInfo);

    instance->App->DiscreteStatesEvaluated = false;
}


bool App_SetBoolean(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Boolean value)
{
    return false;
}


bool App_SetFloat64(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Float64 value)
{
    if (valueReference == FMU_VAR_BUS_ERROR_PROBABILITY)
    {
        if (instance->App->BusErrorProbability != value)  // NOLINT(clang-diagnostic-float-equal)
        {
            LogFmuMessage(instance, fmi3OK, "Info",
                          "Probability for simulated bus errors has been set to %.4e", value);
            instance->App->BusErrorProbability = value;
        }
        return true;
    }

    return false;
}


bool App_SetBinary(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Binary value, size_t valueLength)
{
    if (instance->App->DiscreteStatesEvaluated)
    {
        LogFmuMessage(instance, fmi3Error, "Error",
                      "Discrete states have already been evaluated, cannot set input variable");
        return false;
    }

    if (valueReference == FMU_VAR_NODE1_RX_DATA)
    {
        LogFmuMessage(instance, fmi3OK, "Trace", "Set node 1 RX buffer of %llu bytes", valueLength);
        FMI3_LS_BUS_BUFFER_WRITE(&instance->App->Nodes[0].RxBufferInfo, value, valueLength);
        return true;
    }

    if (valueReference == FMU_VAR_NODE2_RX_DATA)
    {
        LogFmuMessage(instance, fmi3OK, "Trace", "Set node 2 RX buffer of %llu bytes", valueLength);
        FMI3_LS_BUS_BUFFER_WRITE(&instance->App->Nodes[1].RxBufferInfo, value, valueLength);
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
        *value = FMI3_LS_BUS_BUFFER_START(&instance->App->Nodes[0].TxBufferInfo);
        *valueLength = FMI3_LS_BUS_BUFFER_LENGTH(&instance->App->Nodes[0].TxBufferInfo);
        LogFmuMessage(instance, fmi3OK, "Trace", "Get node 1 TX buffer of %llu bytes", *valueLength);
        return true;
    }

    if (valueReference == FMU_VAR_NODE2_TX_DATA)
    {
        *value = FMI3_LS_BUS_BUFFER_START(&instance->App->Nodes[1].TxBufferInfo);
        *valueLength = FMI3_LS_BUS_BUFFER_LENGTH(&instance->App->Nodes[1].TxBufferInfo);
        LogFmuMessage(instance, fmi3OK, "Trace", "Get node 2 TX buffer of %llu bytes", *valueLength);
        return true;
    }

    return false;
}


bool App_SetClock(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Clock value)
{
    if (instance->App->DiscreteStatesEvaluated)
    {
        LogFmuMessage(instance, fmi3Error, "Error",
                      "Discrete states have already been evaluated, cannot set input variable");
        return false;
    }

    if (valueReference == FMU_VAR_NODE1_RX_CLOCK)
    {
        LogFmuMessage(instance, fmi3OK, "Trace", "Set node 1 RX clock to %u", value);
        instance->App->Nodes[0].RxClock = value;
        return true;
    }

    if (valueReference == FMU_VAR_NODE1_TX_CLOCK)
    {
        LogFmuMessage(instance, fmi3OK, "Trace", "Set node 1 TX clock to %u", value);
        instance->App->Nodes[0].TxClock = value;
        return true;
    }

    if (valueReference == FMU_VAR_NODE2_RX_CLOCK)
    {
        LogFmuMessage(instance, fmi3OK, "Trace", "Set node 2 RX clock to %u", value);
        instance->App->Nodes[1].RxClock = value;
        return true;
    }

    if (valueReference == FMU_VAR_NODE2_TX_CLOCK)
    {
        LogFmuMessage(instance, fmi3OK, "Trace", "Set node 2 TX clock to %u", value);
        instance->App->Nodes[1].TxClock = value;
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
    if (valueReference == FMU_VAR_NODE1_TX_CLOCK || valueReference == FMU_VAR_NODE2_TX_CLOCK)
    {
        const NodeIdType node = valueReference == FMU_VAR_NODE1_TX_CLOCK ? 0 : 1;

        *qualifier = instance->App->Nodes[node].TxClockQualifier;
        if (instance->App->Nodes[node].TxClockQualifier == fmi3IntervalChanged)
        {
            *counter = instance->App->TxClockCounter;
            *resolution = instance->App->TxClockResolution;
            instance->App->Nodes[node].TxClockQualifier = fmi3IntervalUnchanged;
        }
        return true;
    }

    return false;
}
