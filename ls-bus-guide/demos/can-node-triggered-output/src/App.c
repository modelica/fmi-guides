#include "App.h"

#include <stdlib.h>

#include "fmi3LsBusUtilCan.h"

#include "Logging.h"


/**
 * \brief Identifies variables of this FMU with their value references.
 */
typedef enum
{
    FMU_VAR_RX_DATA = 0,
    FMU_VAR_TX_DATA = 1,
    FMU_VAR_RX_CLOCK = 2,
    FMU_VAR_TX_CLOCK = 3,

    FMU_VAR_CAN_BUS_NOTIFICATIONS = 128,
    FMU_VAR_SIMULATION_TIME = 1024
} FmuVariables;


static const fmi3Float64 TransmitInterval = 0.3;


/**
 * \brief Instance-specific data of this FMU.
 */
struct AppType
{
    fmi3Byte RxBuffer[2048];
    fmi3LsBusUtilBufferInfo RxBufferInfo;
    fmi3Clock RxClock;

    fmi3Byte TxBuffer[2048];
    fmi3LsBusUtilBufferInfo TxBufferInfo;
    fmi3Clock TxClock;

    fmi3Float64 NextTransmitTime;
    fmi3Float64 SimulationTime;
};


AppType* App_Instantiate(void)
{
    AppType* app = calloc(1, sizeof(AppType));
    if (app == NULL)
    {
        return NULL;
    }

    // Initialize transmit and receive buffers
    FMI3_LS_BUS_BUFFER_INFO_INIT(&app->RxBufferInfo, app->RxBuffer, sizeof(app->RxBuffer));
    FMI3_LS_BUS_BUFFER_INFO_INIT(&app->TxBufferInfo, app->TxBuffer, sizeof(app->TxBuffer));

    // Create bus configuration operations
    FMI3_LS_BUS_CAN_CREATE_OP_CONFIGURATION_CAN_BAUDRATE(&app->TxBufferInfo, 100000);
    FMI3_LS_BUS_CAN_CREATE_OP_CONFIGURATION_ARBITRATION_LOST_BEHAVIOR(
        &app->TxBufferInfo,
        FMI3_LS_BUS_CAN_CONFIG_PARAM_ARBITRATION_LOST_BEHAVIOR_BUFFER_AND_RETRANSMIT);
    app->TxClock = fmi3ClockActive;

    // Schedule next transmission
    app->NextTransmitTime = TransmitInterval;
    app->SimulationTime = 0.0;
    return app;
}


void App_Free(AppType* instance)
{
    free(instance);
}


bool App_DoStep(FmuInstance* instance, fmi3Float64 currentTime, fmi3Float64 targetTime)
{
    instance->App->SimulationTime = currentTime;

    // Send transmit operations with the given interval until we reach the target time
    while (instance->App->NextTransmitTime <= targetTime)
    {
        instance->App->SimulationTime = instance->App->NextTransmitTime;

        // We are transmitting with a constant CAN ID and payload
        const fmi3LsBusCanId id = 0x1;
        const fmi3Byte data[4] = {1, 2, 3, 4};

        LogFmuMessage(instance, fmi3OK, "Info", "Transmitting CAN frame with ID %u at internal time %f", id,
                      instance->App->NextTransmitTime);

        // Create a CAN transmit operation
        FMI3_LS_BUS_CAN_CREATE_OP_CAN_TRANSMIT(&instance->App->TxBufferInfo, id, FMI3_LS_BUS_FALSE, FMI3_LS_BUS_FALSE, sizeof data, data);

        // Check that operation was created successfully
        if (!instance->App->TxBufferInfo.status)
        {
            LogFmuMessage(instance, fmi3Warning, "Warning", "Failed to transmit CAN frame: Insufficient buffer space");
            break;
        }

        // Schedule next transmission
        instance->App->NextTransmitTime = instance->App->NextTransmitTime + TransmitInterval;
    }

     instance->App->SimulationTime = targetTime;

    // If we create bus transmit operations, we have to enter event mode and set the TX clock after this step
    if (FMI3_LS_BUS_BUFFER_LENGTH(&instance->App->TxBufferInfo) > 0)
    {
        instance->App->TxClock = fmi3ClockActive;
        return true;
    }

    return false;
}


void App_EvaluateDiscreteStates(FmuInstance* instance)
{
    (void)instance;
}


void App_UpdateDiscreteStates(FmuInstance* instance)
{
    // We only process bus operations when the RX clock is set, otherwise the contents of the buffer are not well-defined
    if (instance->App->RxClock == fmi3ClockActive)
    {
        // Read all bus operations from the RX buffer
        fmi3LsBusOperationHeader* operation = NULL;
        while (FMI3_LS_BUS_READ_NEXT_OPERATION(&instance->App->RxBufferInfo, operation))
        {
            if (operation->opCode == FMI3_LS_BUS_CAN_OP_CAN_TRANSMIT)
            {
                const fmi3LsBusCanOperationCanTransmit* transmitOp = (fmi3LsBusCanOperationCanTransmit*)operation;
                LogFmuMessage(instance, fmi3OK, "Info", "Received CAN frame with ID %u and length %u", transmitOp->id,
                              transmitOp->dataLength);
            }
            else if (operation->opCode == FMI3_LS_BUS_CAN_OP_CONFIGURATION ||
                     operation->opCode == FMI3_LS_BUS_CAN_OP_STATUS ||
                     operation->opCode == FMI3_LS_BUS_CAN_OP_CONFIRM ||
                     operation->opCode == FMI3_LS_BUS_CAN_OP_BUS_ERROR ||
                     operation->opCode == FMI3_LS_BUS_CAN_OP_ARBITRATION_LOST)
            {
                // Ignore
            }
            else
            {
                LogFmuMessage(instance, fmi3OK, "Warning", "Received unknown bus operation");
            }
        }
    }

    // Deactivate RX clock and clear RX buffer since all operations should have been processed
    instance->App->RxClock = fmi3ClockInactive;
    FMI3_LS_BUS_BUFFER_INFO_RESET(&instance->App->RxBufferInfo);

    // Deactivate TX clock and clear TX buffer since both should have been retrieved by this time
    instance->App->TxClock = fmi3ClockInactive;
    FMI3_LS_BUS_BUFFER_INFO_RESET(&instance->App->TxBufferInfo);
}


bool App_SetBoolean(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Boolean value)
{
    if (valueReference == FMU_VAR_CAN_BUS_NOTIFICATIONS)
    {
        LogFmuMessage(instance, fmi3OK, "Info", "Set Can_BusNotifications to %u", value);
        return true;
    }

    return false;
}


bool App_SetFloat64(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Float64 value)
{
    (void)instance;
    (void)valueReference;
    (void)value;
    return false;
}


bool App_GetFloat64(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Float64* value)
{
    if (valueReference == FMU_VAR_SIMULATION_TIME)
    {
        *value = instance->App->SimulationTime;
        return true;
    }

    return false;
}


bool App_SetBinary(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Binary value, size_t valueLength)
{
    if (valueReference == FMU_VAR_RX_DATA)
    {
        LogFmuMessage(instance, fmi3OK, "Trace", "Set RX buffer of %llu bytes", valueLength);
        FMI3_LS_BUS_BUFFER_WRITE(&instance->App->RxBufferInfo, value, valueLength);
        return true;
    }

    return false;
}


bool App_GetBinary(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Binary* value, size_t* valueLength)
{
    if (valueReference == FMU_VAR_TX_DATA)
    {
        *value = FMI3_LS_BUS_BUFFER_START(&instance->App->TxBufferInfo);
        *valueLength = FMI3_LS_BUS_BUFFER_LENGTH(&instance->App->TxBufferInfo);
        LogFmuMessage(instance, fmi3OK, "Trace", "Get TX buffer of %llu bytes", *valueLength);
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

    return false;
}


bool App_GetClock(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Clock* value)
{
    if (valueReference == FMU_VAR_TX_CLOCK)
    {
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
    (void)instance;
    (void)valueReference;
    (void)counter;
    (void)resolution;
    (void)qualifier;
    return false;
}
