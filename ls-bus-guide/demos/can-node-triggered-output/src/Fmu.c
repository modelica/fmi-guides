#include <string.h>

#include "FmuIdentifier.h"
#include "fmi3Functions.h"

#include "App.h"
#include "Instance.h"
#include "Logging.h"


/**
 * Helper macros
 */

/**
 * Returns fmi3OK when the given value is zero.
 * Returns fmi3Error and terminates with an error if the given value is nonzero.
 * \param x The value to check.
 * \param instance The FMU instance.
 */
#define INVALID_CALL_IF_NONZERO(x, instance) \
    (((x) == 0) ? fmi3OK : TerminateWithError((instance), "%s: Invalid call", __func__))

/**
 * Returns fmi3Error and terminates with an error stating that the current function is not supported.
 * \param instance The FMU instance.
 */
#define ERROR_NOT_SUPPORTED(instance) (TerminateWithError((instance), "%s: Not supported", __func__))

/**
 * Returns fmiDiscard and emits a warning stating that the current function is not supported.
 * \param instance The FMU instance.
 */
#define DISCARD_NOT_SUPPORTED(instance) (LogFmuMessage((instance), fmi3Discard, "logStatusDiscard", "%s: Not supported", __func__), fmi3Discard)

/**
 * \brief Asserts that the FMU is in the given state.
 * \param instance The FMU instance.
 * \param state The state.
 */
#define ASSERT_FMU_STATE(instance, state)                                                     \
    do                                                                                        \
    {                                                                                         \
        if (((FmuInstance*)(instance))->State != (state))                                     \
        {                                                                                     \
            TerminateWithError(instance, "Must be in state %s to call %s", #state, __func__); \
            return fmi3Error;                                                                 \
        }                                                                                     \
    }                                                                                         \
    while (0)

/**
 * FMI 3.0 functions - Inquire version numbers and set debug logging
 */

FMI3_Export const char* fmi3GetVersion(void)
{
    return fmi3Version;
}


/**
 * FMI 3.0 functions - Creation and destruction of FMU instances
 */

FMI3_Export fmi3Instance fmi3InstantiateCoSimulation(fmi3String instanceName,
                                                     fmi3String instantiationToken,
                                                     fmi3String resourcePath,
                                                     fmi3Boolean visible,
                                                     fmi3Boolean loggingOn,
                                                     fmi3Boolean eventModeUsed,
                                                     fmi3Boolean earlyReturnAllowed,
                                                     const fmi3ValueReference requiredIntermediateVariables[],
                                                     size_t nRequiredIntermediateVariables,
                                                     fmi3InstanceEnvironment instanceEnvironment,
                                                     fmi3LogMessageCallback logMessage,
                                                     fmi3IntermediateUpdateCallback intermediateUpdate)
{
    (void)instantiationToken;
    (void)resourcePath;
    (void)visible;
    (void)earlyReturnAllowed;
    (void)requiredIntermediateVariables;
    (void)nRequiredIntermediateVariables;
    (void)intermediateUpdate;

    FmuInstance* fmuInstance = malloc(sizeof(FmuInstance));
    if (fmuInstance == NULL)
    {
        return NULL;
    }

    fmuInstance->InstanceName = _strdup(instanceName);
    fmuInstance->InstanceEnvironment = instanceEnvironment;
    fmuInstance->LogMessageCallback = NULL;
    fmuInstance->State = FMU_STATE_INSTANTIATED;

    if (loggingOn)
    {
        fmuInstance->LogMessageCallback = logMessage;
    }

    if (!eventModeUsed)
    {
        LogFmuMessage(fmuInstance, fmi3Error, "logStatusError",
                      "Event mode is must be supported by the importer to use this FMU.");
        free(fmuInstance);
        return NULL;
    }

    fmuInstance->App = App_Instantiate();
    if (fmuInstance->App == NULL)
    {
        LogFmuMessage(fmuInstance, fmi3Error, "logStatusError", "Instantiation failed.");
        free(fmuInstance);
        return NULL;
    }

    LogFmuMessage(fmuInstance, fmi3OK, "Trace", "Instance %s instantiated.", fmuInstance->InstanceName);

    return fmuInstance;
}

FMI3_Export void fmi3FreeInstance(fmi3Instance instance)
{
    FmuInstance* fmuInstance = instance;

    LogFmuMessage(instance, fmi3OK, "Trace", "Instance %s freed.", fmuInstance->InstanceName);

    App_Free(fmuInstance->App);

    free(fmuInstance->InstanceName);
    free(fmuInstance);
}


/**
 * FMI 3.0 Functions - Enter and exit initialization mode, terminate and reset
 */

FMI3_Export fmi3Status fmi3EnterInitializationMode(fmi3Instance instance,
                                                   fmi3Boolean toleranceDefined,
                                                   fmi3Float64 tolerance,
                                                   fmi3Float64 startTime,
                                                   fmi3Boolean stopTimeDefined,
                                                   fmi3Float64 stopTime)
{
    (void)toleranceDefined;
    (void)tolerance;
    (void)startTime;
    (void)stopTimeDefined;
    (void)stopTime;

    ASSERT_FMU_STATE(instance, FMU_STATE_INSTANTIATED);

    LogFmuMessage(instance, fmi3OK, "Trace", "Entering initialization mode.");
    FmuInstance* fmuInstance = instance;
    fmuInstance->State = FMU_STATE_INITIALIZATION_MODE;

    return fmi3OK;
}

FMI3_Export fmi3Status fmi3ExitInitializationMode(fmi3Instance instance)
{
    ASSERT_FMU_STATE(instance, FMU_STATE_INITIALIZATION_MODE);

    LogFmuMessage(instance, fmi3OK, "Trace", "Entering event mode.");
    FmuInstance* fmuInstance = instance;
    fmuInstance->State = FMU_STATE_EVENT_MODE;

    return fmi3OK;
}

FMI3_Export fmi3Status fmi3EnterEventMode(fmi3Instance instance)
{
    ASSERT_FMU_STATE(instance, FMU_STATE_STEP_MODE);

    LogFmuMessage(instance, fmi3OK, "Trace", "Entering event mode.");
    FmuInstance* fmuInstance = instance;
    fmuInstance->State = FMU_STATE_EVENT_MODE;

    return fmi3OK;
}

FMI3_Export fmi3Status fmi3Terminate(fmi3Instance instance)
{
    FmuInstance* fmuInstance = instance;
    fmuInstance->State = FMU_STATE_TERMINATED;
    LogFmuMessage(instance, fmi3OK, "Trace", "Terminating.");
    return fmi3OK;
}


/**
 * FMI 3.0 functions - Getting and setting variable values
 */

FMI3_Export fmi3Status fmi3GetBinary(fmi3Instance instance,
                                     const fmi3ValueReference valueReferences[],
                                     size_t nValueReferences,
                                     size_t valueSizes[],
                                     fmi3Binary values[],
                                     size_t nValues)
{
    FmuInstance* fmuInstance = instance;

    if (nValueReferences != nValues)
    {
        TerminateWithError(instance, "fmi3GetBinary: Invalid call");
        return fmi3Error;
    }

    for (size_t i = 0; i < nValueReferences; i++)
    {
        if (!App_GetBinary(fmuInstance, valueReferences[i], &values[i], &valueSizes[i]))
        {
            TerminateWithError(instance, "fmi3GetBinary: Invalid call with value reference %u", valueReferences[i]);
            return fmi3Error;
        }
    }

    return fmi3OK;
}

FMI3_Export fmi3Status fmi3GetClock(fmi3Instance instance,
                                    const fmi3ValueReference valueReferences[],
                                    size_t nValueReferences,
                                    fmi3Clock values[])
{
    FmuInstance* fmuInstance = instance;

    for (size_t i = 0; i < nValueReferences; i++)
    {
        if (!App_GetClock(fmuInstance, valueReferences[i], &values[i]))
        {
            TerminateWithError(instance, "fmi3GetClock: Invalid call with value reference %u", valueReferences[i]);
            return fmi3Error;
        }
    }

    return fmi3OK;
}

FMI3_Export fmi3Status fmi3SetFloat64(fmi3Instance instance,
                                      const fmi3ValueReference valueReferences[],
                                      size_t nValueReferences,
                                      const fmi3Float64 values[],
                                      size_t nValues)
{
    FmuInstance* fmuInstance = instance;

    if (nValueReferences != nValues)
    {
        TerminateWithError(instance, "fmi3SetFloat64: Invalid call");
        return fmi3Error;
    }

    for (size_t i = 0; i < nValueReferences; i++)
    {
        if (!App_SetFloat64(fmuInstance, valueReferences[i], values[i]))
        {
            TerminateWithError(instance, "fmi3SetFloat64: Invalid call with value reference %u and value %f",
                               valueReferences[i], values[i]);
            return fmi3Error;
        }
    }

    return fmi3OK;
}

FMI3_Export fmi3Status fmi3SetBoolean(fmi3Instance instance,
                                      const fmi3ValueReference valueReferences[],
                                      size_t nValueReferences,
                                      const fmi3Boolean values[],
                                      size_t nValues)
{
    FmuInstance* fmuInstance = instance;

    if (nValueReferences != nValues)
    {
        TerminateWithError(instance, "fmi3SetBoolean: Invalid call");
        return fmi3Error;
    }

    for (size_t i = 0; i < nValueReferences; i++)
    {
        if (!App_SetBoolean(fmuInstance, valueReferences[i], values[i]))
        {
            TerminateWithError(instance, "fmi3SetBoolean: Invalid call with value reference %u and value %u",
                               valueReferences[i], values[i]);
            return fmi3Error;
        }
    }

    return fmi3OK;
}

FMI3_Export fmi3Status fmi3SetBinary(fmi3Instance instance,
                                     const fmi3ValueReference valueReferences[],
                                     size_t nValueReferences,
                                     const size_t valueSizes[],
                                     const fmi3Binary values[],
                                     size_t nValues)
{
    FmuInstance* fmuInstance = instance;

    if (nValueReferences != nValues)
    {
        TerminateWithError(instance, "fmi3SetBinary: Invalid call");
        return fmi3Error;
    }

    for (size_t i = 0; i < nValueReferences; i++)
    {
        if (!App_SetBinary(fmuInstance, valueReferences[i], values[i], valueSizes[i]))
        {
            TerminateWithError(instance, "fmi3SetBinary: Invalid call with value reference %u", valueReferences[i]);
            return fmi3Error;
        }
    }

    return fmi3OK;
}

FMI3_Export fmi3Status fmi3SetClock(fmi3Instance instance,
                                    const fmi3ValueReference valueReferences[],
                                    size_t nValueReferences,
                                    const fmi3Clock values[])
{
    FmuInstance* fmuInstance = instance;

    for (size_t i = 0; i < nValueReferences; i++)
    {
        if (!App_SetClock(fmuInstance, valueReferences[i], values[i]))
        {
            TerminateWithError(instance, "fmi3SetClock: Invalid call with value reference %u and value %u",
                               valueReferences[i], values[i]);
            return fmi3Error;
        }
    }

    return fmi3OK;
}


/**
 * FMI 3.0 functions - Entering and exiting the Configuration or Reconfiguration Mode
 */

FMI3_Export fmi3Status fmi3EnterConfigurationMode(fmi3Instance instance)
{
    ASSERT_FMU_STATE(instance, FMU_STATE_INSTANTIATED);

    LogFmuMessage(instance, fmi3OK, "Trace", "Entering configuration mode.");
    FmuInstance* fmuInstance = instance;
    fmuInstance->State = FMU_STATE_CONFIGURATION_MODE;

    return fmi3OK;
}

FMI3_Export fmi3Status fmi3ExitConfigurationMode(fmi3Instance instance)
{
    ASSERT_FMU_STATE(instance, FMU_STATE_CONFIGURATION_MODE);

    LogFmuMessage(instance, fmi3OK, "Trace", "Exiting configuration mode.");
    FmuInstance* fmuInstance = instance;
    fmuInstance->State = FMU_STATE_INSTANTIATED;

    return fmi3OK;
}


/**
 * FMI 3.0 functions - Clock related functions
 */

FMI3_Export fmi3Status fmi3GetIntervalDecimal(fmi3Instance instance,
                                              const fmi3ValueReference valueReferences[],
                                              size_t nValueReferences,
                                              fmi3Float64 intervals[],
                                              fmi3IntervalQualifier qualifiers[])
{
    FmuInstance* fmuInstance = instance;

    for (size_t i = 0; i < nValueReferences; i++)
    {
        fmi3UInt64 counter;
        fmi3UInt64 resolution;
        if (!App_GetIntervalFraction(fmuInstance, valueReferences[i], &counter, &resolution, &qualifiers[i]))
        {
            TerminateWithError(instance, "fmi3GetIntervalFraction: Invalid call with value reference %u",
                               valueReferences[i]);
            return fmi3Error;
        }
        intervals[i] = (fmi3Float64)counter / (fmi3Float64)resolution;
    }

    return fmi3OK;
}

FMI3_Export fmi3Status fmi3GetIntervalFraction(fmi3Instance instance,
                                               const fmi3ValueReference valueReferences[],
                                               size_t nValueReferences,
                                               fmi3UInt64 counters[],
                                               fmi3UInt64 resolutions[],
                                               fmi3IntervalQualifier qualifiers[])
{
    FmuInstance* fmuInstance = instance;

    for (size_t i = 0; i < nValueReferences; i++)
    {
        if (!App_GetIntervalFraction(fmuInstance, valueReferences[i], &counters[i], &resolutions[i], &qualifiers[i]))
        {
            TerminateWithError(instance, "fmi3GetIntervalFraction: Invalid call with value reference %u",
                               valueReferences[i]);
            return fmi3Error;
        }
    }

    return fmi3OK;
}

FMI3_Export fmi3Status fmi3EvaluateDiscreteStates(fmi3Instance instance)
{
    App_EvaluateDiscreteStates(instance);

    return fmi3OK;
}

FMI3_Export fmi3Status fmi3UpdateDiscreteStates(fmi3Instance instance,
                                                fmi3Boolean* discreteStatesNeedUpdate,
                                                fmi3Boolean* terminateSimulation,
                                                fmi3Boolean* nominalsOfContinuousStatesChanged,
                                                fmi3Boolean* valuesOfContinuousStatesChanged,
                                                fmi3Boolean* nextEventTimeDefined,
                                                fmi3Float64* nextEventTime)
{
    FmuInstance* fmuInstance = instance;

    LogFmuMessage(instance, fmi3OK, "Trace", "Ending super dense time step.");

    App_UpdateDiscreteStates(fmuInstance);

    *discreteStatesNeedUpdate = fmi3False;
    *terminateSimulation = fmi3False;
    *nominalsOfContinuousStatesChanged = fmi3False;
    *valuesOfContinuousStatesChanged = fmi3False;
    *nextEventTimeDefined = fmi3False;
    *nextEventTime = 0.0;

    return fmi3OK;
}


/**
 * FMI 3.0 functions for co-simulation - Simulating the FMU
 */

FMI3_Export fmi3Status fmi3EnterStepMode(fmi3Instance instance)
{
    FmuInstance* fmuInstance = instance;
    if (fmuInstance->State != FMU_STATE_EVENT_MODE)
    {
        TerminateWithError(instance, "Must be in state EVENT_MODE to enter step mode.");
        return fmi3Error;
    }

    fmuInstance->State = FMU_STATE_STEP_MODE;
    LogFmuMessage(instance, fmi3OK, "Trace", "Entering step mode.");

    return fmi3OK;
}

FMI3_Export fmi3Status fmi3DoStep(fmi3Instance instance,
                                  fmi3Float64 currentCommunicationPoint,
                                  fmi3Float64 communicationStepSize,
                                  fmi3Boolean noSetFMUStatePriorToCurrentPoint,
                                  fmi3Boolean* eventHandlingNeeded,
                                  fmi3Boolean* terminateSimulation,
                                  fmi3Boolean* earlyReturn,
                                  fmi3Float64* lastSuccessfulTime)
{
    (void)noSetFMUStatePriorToCurrentPoint;

    FmuInstance* fmuInstance = instance;

    LogFmuMessage(instance, fmi3OK, "Trace", "Doing step at %f of %f seconds.", currentCommunicationPoint,
                  communicationStepSize);

    const fmi3Float64 targetTime = currentCommunicationPoint + communicationStepSize;
    *eventHandlingNeeded = App_DoStep(fmuInstance, currentCommunicationPoint, targetTime);
    *lastSuccessfulTime = targetTime;

    *terminateSimulation = fmi3False;
    *earlyReturn = fmi3False;

    return fmi3OK;
}


/**
 * Unused FMI 3.0 functions - Inquire version numbers and set debug logging
 */

FMI3_Export fmi3Status fmi3SetDebugLogging(fmi3Instance instance,
                                           fmi3Boolean loggingOn,
                                           size_t nCategories,
                                           const fmi3String categories[])
{
    return DISCARD_NOT_SUPPORTED(instance);
}


/**
 * Unused FMI 3.0 functions - Creation and destruction of FMU instances
 */

FMI3_Export fmi3Instance fmi3InstantiateModelExchange(fmi3String instanceName,
                                                      fmi3String instantiationToken,
                                                      fmi3String resourcePath,
                                                      fmi3Boolean visible,
                                                      fmi3Boolean loggingOn,
                                                      fmi3InstanceEnvironment instanceEnvironment,
                                                      fmi3LogMessageCallback logMessage)
{
    return NULL;
}

FMI3_Export fmi3Instance fmi3InstantiateScheduledExecution(fmi3String instanceName,
                                                           fmi3String instantiationToken,
                                                           fmi3String resourcePath,
                                                           fmi3Boolean visible,
                                                           fmi3Boolean loggingOn,
                                                           fmi3InstanceEnvironment instanceEnvironment,
                                                           fmi3LogMessageCallback logMessage,
                                                           fmi3ClockUpdateCallback clockUpdate,
                                                           fmi3LockPreemptionCallback lockPreemption,
                                                           fmi3UnlockPreemptionCallback unlockPreemption)
{
    return NULL;
}


/**
 * Unused FMI 3.0 functions - Enter and exit initialization mode, terminate and reset
 */

FMI3_Export fmi3Status fmi3Reset(fmi3Instance instance)
{
    return DISCARD_NOT_SUPPORTED(instance);
}


/**
 * Unused FMI 3.0 functions - Getting and setting variable values
 */

FMI3_Export fmi3Status fmi3GetFloat32(fmi3Instance instance,
                                      const fmi3ValueReference valueReferences[],
                                      size_t nValueReferences,
                                      fmi3Float32 values[],
                                      size_t nValues)
{
    return INVALID_CALL_IF_NONZERO(nValueReferences, instance);
}

FMI3_Export fmi3Status fmi3GetFloat64(fmi3Instance instance,
                                      const fmi3ValueReference valueReferences[],
                                      size_t nValueReferences,
                                      fmi3Float64 values[],
                                      size_t nValues)
{
    return INVALID_CALL_IF_NONZERO(nValueReferences, instance);
}

FMI3_Export fmi3Status fmi3GetInt8(fmi3Instance instance,
                                   const fmi3ValueReference valueReferences[],
                                   size_t nValueReferences,
                                   fmi3Int8 values[],
                                   size_t nValues)
{
    return INVALID_CALL_IF_NONZERO(nValueReferences, instance);
}

FMI3_Export fmi3Status fmi3GetUInt8(fmi3Instance instance,
                                    const fmi3ValueReference valueReferences[],
                                    size_t nValueReferences,
                                    fmi3UInt8 values[],
                                    size_t nValues)
{
    return INVALID_CALL_IF_NONZERO(nValueReferences, instance);
}

FMI3_Export fmi3Status fmi3GetInt16(fmi3Instance instance,
                                    const fmi3ValueReference valueReferences[],
                                    size_t nValueReferences,
                                    fmi3Int16 values[],
                                    size_t nValues)
{
    return INVALID_CALL_IF_NONZERO(nValueReferences, instance);
}

FMI3_Export fmi3Status fmi3GetUInt16(fmi3Instance instance,
                                     const fmi3ValueReference valueReferences[],
                                     size_t nValueReferences,
                                     fmi3UInt16 values[],
                                     size_t nValues)
{
    return INVALID_CALL_IF_NONZERO(nValueReferences, instance);
}

FMI3_Export fmi3Status fmi3GetInt32(fmi3Instance instance,
                                    const fmi3ValueReference valueReferences[],
                                    size_t nValueReferences,
                                    fmi3Int32 values[],
                                    size_t nValues)
{
    return INVALID_CALL_IF_NONZERO(nValueReferences, instance);
}

FMI3_Export fmi3Status fmi3GetUInt32(fmi3Instance instance,
                                     const fmi3ValueReference valueReferences[],
                                     size_t nValueReferences,
                                     fmi3UInt32 values[],
                                     size_t nValues)
{
    return INVALID_CALL_IF_NONZERO(nValueReferences, instance);
}

FMI3_Export fmi3Status fmi3GetInt64(fmi3Instance instance,
                                    const fmi3ValueReference valueReferences[],
                                    size_t nValueReferences,
                                    fmi3Int64 values[],
                                    size_t nValues)
{
    return INVALID_CALL_IF_NONZERO(nValueReferences, instance);
}

FMI3_Export fmi3Status fmi3GetUInt64(fmi3Instance instance,
                                     const fmi3ValueReference valueReferences[],
                                     size_t nValueReferences,
                                     fmi3UInt64 values[],
                                     size_t nValues)
{
    return INVALID_CALL_IF_NONZERO(nValueReferences, instance);
}

FMI3_Export fmi3Status fmi3GetBoolean(fmi3Instance instance,
                                      const fmi3ValueReference valueReferences[],
                                      size_t nValueReferences,
                                      fmi3Boolean values[],
                                      size_t nValues)
{
    return INVALID_CALL_IF_NONZERO(nValueReferences, instance);
}

FMI3_Export fmi3Status fmi3GetString(fmi3Instance instance,
                                     const fmi3ValueReference valueReferences[],
                                     size_t nValueReferences,
                                     fmi3String values[],
                                     size_t nValues)
{
    return INVALID_CALL_IF_NONZERO(nValueReferences, instance);
}

FMI3_Export fmi3Status fmi3SetFloat32(fmi3Instance instance,
                                      const fmi3ValueReference valueReferences[],
                                      size_t nValueReferences,
                                      const fmi3Float32 values[],
                                      size_t nValues)
{
    return INVALID_CALL_IF_NONZERO(nValueReferences, instance);
}

FMI3_Export fmi3Status fmi3SetInt8(fmi3Instance instance,
                                   const fmi3ValueReference valueReferences[],
                                   size_t nValueReferences,
                                   const fmi3Int8 values[],
                                   size_t nValues)
{
    return INVALID_CALL_IF_NONZERO(nValueReferences, instance);
}

FMI3_Export fmi3Status fmi3SetUInt8(fmi3Instance instance,
                                    const fmi3ValueReference valueReferences[],
                                    size_t nValueReferences,
                                    const fmi3UInt8 values[],
                                    size_t nValues)
{
    return INVALID_CALL_IF_NONZERO(nValueReferences, instance);
}

FMI3_Export fmi3Status fmi3SetInt16(fmi3Instance instance,
                                    const fmi3ValueReference valueReferences[],
                                    size_t nValueReferences,
                                    const fmi3Int16 values[],
                                    size_t nValues)
{
    return INVALID_CALL_IF_NONZERO(nValueReferences, instance);
}

FMI3_Export fmi3Status fmi3SetUInt16(fmi3Instance instance,
                                     const fmi3ValueReference valueReferences[],
                                     size_t nValueReferences,
                                     const fmi3UInt16 values[],
                                     size_t nValues)
{
    return INVALID_CALL_IF_NONZERO(nValueReferences, instance);
}

FMI3_Export fmi3Status fmi3SetInt32(fmi3Instance instance,
                                    const fmi3ValueReference valueReferences[],
                                    size_t nValueReferences,
                                    const fmi3Int32 values[],
                                    size_t nValues)
{
    return INVALID_CALL_IF_NONZERO(nValueReferences, instance);
}

FMI3_Export fmi3Status fmi3SetUInt32(fmi3Instance instance,
                                     const fmi3ValueReference valueReferences[],
                                     size_t nValueReferences,
                                     const fmi3UInt32 values[],
                                     size_t nValues)
{
    return INVALID_CALL_IF_NONZERO(nValueReferences, instance);
}

FMI3_Export fmi3Status fmi3SetInt64(fmi3Instance instance,
                                    const fmi3ValueReference valueReferences[],
                                    size_t nValueReferences,
                                    const fmi3Int64 values[],
                                    size_t nValues)
{
    return INVALID_CALL_IF_NONZERO(nValueReferences, instance);
}

FMI3_Export fmi3Status fmi3SetUInt64(fmi3Instance instance,
                                     const fmi3ValueReference valueReferences[],
                                     size_t nValueReferences,
                                     const fmi3UInt64 values[],
                                     size_t nValues)
{
    return INVALID_CALL_IF_NONZERO(nValueReferences, instance);
}

FMI3_Export fmi3Status fmi3SetString(fmi3Instance instance,
                                     const fmi3ValueReference valueReferences[],
                                     size_t nValueReferences,
                                     const fmi3String values[],
                                     size_t nValues)
{
    return INVALID_CALL_IF_NONZERO(nValueReferences, instance);
}


/**
 * Unused FMI 3.0 functions - Getting Variable Dependency Information
 */

FMI3_Export fmi3Status fmi3GetNumberOfVariableDependencies(fmi3Instance instance,
                                                           fmi3ValueReference valueReference,
                                                           size_t* nDependencies)
{
    return ERROR_NOT_SUPPORTED(instance);
}

FMI3_Export fmi3Status fmi3GetVariableDependencies(fmi3Instance instance,
                                                   fmi3ValueReference dependent,
                                                   size_t elementIndicesOfDependent[],
                                                   fmi3ValueReference independents[],
                                                   size_t elementIndicesOfIndependents[],
                                                   fmi3DependencyKind dependencyKinds[],
                                                   size_t nDependencies)
{
    return ERROR_NOT_SUPPORTED(instance);
}


/**
 * Unused FMI 3.0 functions - Getting and setting the internal FMU state
 */

FMI3_Export fmi3Status fmi3GetFMUState(fmi3Instance instance, fmi3FMUState* FMUState)
{
    return ERROR_NOT_SUPPORTED(instance);
}

FMI3_Export fmi3Status fmi3SetFMUState(fmi3Instance instance, fmi3FMUState FMUState)
{
    return ERROR_NOT_SUPPORTED(instance);
}

FMI3_Export fmi3Status fmi3FreeFMUState(fmi3Instance instance, fmi3FMUState* FMUState)
{
    return ERROR_NOT_SUPPORTED(instance);
}

FMI3_Export fmi3Status fmi3SerializedFMUStateSize(fmi3Instance instance, fmi3FMUState FMUState, size_t* size)
{
    return ERROR_NOT_SUPPORTED(instance);
}

FMI3_Export fmi3Status fmi3SerializeFMUState(fmi3Instance instance,
                                             fmi3FMUState FMUState,
                                             fmi3Byte serializedState[],
                                             size_t size)
{
    return ERROR_NOT_SUPPORTED(instance);
}

FMI3_Export fmi3Status fmi3DeserializeFMUState(fmi3Instance instance,
                                               const fmi3Byte serializedState[],
                                               size_t size,
                                               fmi3FMUState* FMUState)
{
    return ERROR_NOT_SUPPORTED(instance);
}


/**
 * Unused FMI 3.0 functions - Getting partial derivatives
 */

FMI3_Export fmi3Status fmi3GetDirectionalDerivative(fmi3Instance instance,
                                                    const fmi3ValueReference unknowns[],
                                                    size_t nUnknowns,
                                                    const fmi3ValueReference knowns[],
                                                    size_t nKnowns,
                                                    const fmi3Float64 seed[],
                                                    size_t nSeed,
                                                    fmi3Float64 sensitivity[],
                                                    size_t nSensitivity)
{
    return ERROR_NOT_SUPPORTED(instance);
}

FMI3_Export fmi3Status fmi3GetAdjointDerivative(fmi3Instance instance,
                                                const fmi3ValueReference unknowns[],
                                                size_t nUnknowns,
                                                const fmi3ValueReference knowns[],
                                                size_t nKnowns,
                                                const fmi3Float64 seed[],
                                                size_t nSeed,
                                                fmi3Float64 sensitivity[],
                                                size_t nSensitivity)
{
    return ERROR_NOT_SUPPORTED(instance);
}


/**
 * Unused FMI 3.0 functions - Clock related functions
 */

FMI3_Export fmi3Status fmi3GetShiftDecimal(fmi3Instance instance,
                                           const fmi3ValueReference valueReferences[],
                                           size_t nValueReferences,
                                           fmi3Float64 shifts[])
{
    return INVALID_CALL_IF_NONZERO(nValueReferences, instance);
}

FMI3_Export fmi3Status fmi3GetShiftFraction(fmi3Instance instance,
                                            const fmi3ValueReference valueReferences[],
                                            size_t nValueReferences,
                                            fmi3UInt64 counters[],
                                            fmi3UInt64 resolutions[])
{
    return INVALID_CALL_IF_NONZERO(nValueReferences, instance);
}

FMI3_Export fmi3Status fmi3SetIntervalDecimal(fmi3Instance instance,
                                              const fmi3ValueReference valueReferences[],
                                              size_t nValueReferences,
                                              const fmi3Float64 intervals[])
{
    return INVALID_CALL_IF_NONZERO(nValueReferences, instance);
}

FMI3_Export fmi3Status fmi3SetIntervalFraction(fmi3Instance instance,
                                               const fmi3ValueReference valueReferences[],
                                               size_t nValueReferences,
                                               const fmi3UInt64 counters[],
                                               const fmi3UInt64 resolutions[])
{
    return INVALID_CALL_IF_NONZERO(nValueReferences, instance);
}

FMI3_Export fmi3Status fmi3SetShiftDecimal(fmi3Instance instance,
                                           const fmi3ValueReference valueReferences[],
                                           size_t nValueReferences,
                                           const fmi3Float64 shifts[])
{
    return INVALID_CALL_IF_NONZERO(nValueReferences, instance);
}

FMI3_Export fmi3Status fmi3SetShiftFraction(fmi3Instance instance,
                                            const fmi3ValueReference valueReferences[],
                                            size_t nValueReferences,
                                            const fmi3UInt64 counters[],
                                            const fmi3UInt64 resolutions[])
{
    return INVALID_CALL_IF_NONZERO(nValueReferences, instance);
}


/**
 * Unused FMI 3.0 functions - Functions for Model Exchange
 */

FMI3_Export fmi3Status fmi3EnterContinuousTimeMode(fmi3Instance instance)
{
    return ERROR_NOT_SUPPORTED(instance);
}

FMI3_Export fmi3Status fmi3CompletedIntegratorStep(fmi3Instance instance,
                                                   fmi3Boolean noSetFMUStatePriorToCurrentPoint,
                                                   fmi3Boolean* enterEventMode,
                                                   fmi3Boolean* terminateSimulation)
{
    return ERROR_NOT_SUPPORTED(instance);
}

FMI3_Export fmi3Status fmi3SetTime(fmi3Instance instance, fmi3Float64 time)
{
    return ERROR_NOT_SUPPORTED(instance);
}

FMI3_Export fmi3Status fmi3SetContinuousStates(fmi3Instance instance,
                                               const fmi3Float64 continuousStates[],
                                               size_t nContinuousStates)
{
    return ERROR_NOT_SUPPORTED(instance);
}

FMI3_Export fmi3Status fmi3GetContinuousStateDerivatives(fmi3Instance instance,
                                                         fmi3Float64 derivatives[],
                                                         size_t nContinuousStates)
{
    return ERROR_NOT_SUPPORTED(instance);
}

FMI3_Export fmi3Status fmi3GetEventIndicators(fmi3Instance instance,
                                              fmi3Float64 eventIndicators[],
                                              size_t nEventIndicators)
{
    return ERROR_NOT_SUPPORTED(instance);
}

FMI3_Export fmi3Status fmi3GetContinuousStates(fmi3Instance instance,
                                               fmi3Float64 continuousStates[],
                                               size_t nContinuousStates)
{
    return ERROR_NOT_SUPPORTED(instance);
}

FMI3_Export fmi3Status fmi3GetNominalsOfContinuousStates(fmi3Instance instance,
                                                         fmi3Float64 nominals[],
                                                         size_t nContinuousStates)
{
    return ERROR_NOT_SUPPORTED(instance);
}

FMI3_Export fmi3Status fmi3GetNumberOfEventIndicators(fmi3Instance instance, size_t* nEventIndicators)
{
    return ERROR_NOT_SUPPORTED(instance);
}

FMI3_Export fmi3Status fmi3GetNumberOfContinuousStates(fmi3Instance instance, size_t* nContinuousStates)
{
    return ERROR_NOT_SUPPORTED(instance);
}


/**
 * Unused FMI 3.0 functions - Functions for Co-Simulation
 */

FMI3_Export fmi3Status fmi3GetOutputDerivatives(fmi3Instance instance,
                                                const fmi3ValueReference valueReferences[],
                                                size_t nValueReferences,
                                                const fmi3Int32 orders[],
                                                fmi3Float64 values[],
                                                size_t nValues)
{
    return INVALID_CALL_IF_NONZERO(nValueReferences, instance);
}


/**
 * Unused FMI 3.0 functions - Functions for Scheduled Execution
 */

FMI3_Export fmi3Status fmi3ActivateModelPartition(fmi3Instance instance,
                                                  fmi3ValueReference clockReference,
                                                  fmi3Float64 activationTime)
{
    return ERROR_NOT_SUPPORTED(instance);
}
