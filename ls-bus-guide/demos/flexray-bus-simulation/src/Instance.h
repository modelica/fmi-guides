#ifndef INSTANCE_H
#define INSTANCE_H

#include "fmi3FunctionTypes.h"


struct AppType;

/**
 * \brief Instance-specific data of this FMU.
 */
typedef struct AppType AppType;


/**
 * \brief Describes the current state of an FMU instance.
 */
typedef enum
{
    FMU_STATE_INSTANTIATED,
    FMU_STATE_INITIALIZATION_MODE,
    FMU_STATE_CONFIGURATION_MODE,
    FMU_STATE_STEP_MODE,
    FMU_STATE_EVENT_MODE,
    FMU_STATE_TERMINATED
} FmuState;


/**
 * \brief Instance-specific data of any FMU.
 */
typedef struct
{
    char* InstanceName;
    void* InstanceEnvironment;
    fmi3LogMessageCallback LogMessageCallback;
    FmuState State;
    fmi3Float64 SimulationTime;

    AppType* App;
} FmuInstance;


#endif
