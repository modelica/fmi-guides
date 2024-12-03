#ifndef APP_H
#define APP_H

#include <stdbool.h>

#include "Instance.h"


/**
 * \brief Instantiates the FMU application.
 * \note Called from fmi3InstantiateCoSimulation.
 * \return The instance data of the application.
 */
AppType* App_Instantiate(void);

/**
 * \brief Frees the FMU application.
 * \note Called from fmi3FreeInstance.
 * \param[in] instance The instance data of the application.
 */
void App_Free(AppType* instance);

/**
 * \brief Advances the simulation time of the FMU application in step mode.
 * \note Called from fmi3DoStep.
 * \param[in] instance The instance data of the application.
 * \param[in] currentTime The time at the start of the step.
 * \param[in] targetTime The time at the end of the step.
 * \return Determines whether the event mode should be activated on return
 */
bool App_DoStep(FmuInstance* instance, fmi3Float64 currentTime, fmi3Float64 targetTime);

/**
 * \brief Evaluates the discrete states of the FMU application in event mode.
 *      This is used to calculate outputs based on inputs which changed in this super-dense time step.
 * \note Called either explicitly from fmi3EvaluateDiscreteStates, or as part of any App_Get* function or
 *      App_UpdateDiscreteStates, if the required states have not been evaluated yet.
 * \param[in] instance The instance data of the application.
 */
void App_EvaluateDiscreteStates(FmuInstance* instance);

/**
 * \brief Signals the end of the current super-dense time step, disabling all clocks.
 * \note Called as part of fmi3UpdateDiscreteStates.
 * \param[in] instance The instance data of the application.
 */
void App_UpdateDiscreteStates(FmuInstance* instance);

/**
 * \brief Sets the value of a boolean input variable.
 * \note Called as part of fmi3SetBoolean.
 * \param[in] instance The instance data of the application.
 * \param[in] valueReference The value reference of the variable.
 * \param[in] value The new value of the variable.
 */
bool App_SetBoolean(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Boolean value);

/**
 * \brief Sets the value of a float64 input variable.
 * \note Called as part of fmi3SetFloat64.
 * \param[in] instance The instance data of the application.
 * \param[in] valueReference The value reference of the variable.
 * \param[in] value The new value of the variable.
 */
bool App_SetFloat64(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Float64 value);

/**
 * \brief Gets the value of a float64 output variable.
 * \note Called as part of fmi3GetFloat64.
 * \param[in] instance The instance data of the application.
 * \param[in] valueReference The value reference of the variable.
 * \param[out] value The new value of the variable.
 */
bool App_GetFloat64(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Float64* value);

/**
 * \brief Sets the value of a binary input variable.
 * \note Called as part of fmi3SetBinary.
 * \param[in] instance The instance data of the application.
 * \param[in] valueReference The value reference of the variable.
 * \param[in] value The new value of the variable.
 * \param[in] valueLength The new length of the variable.
 */
bool App_SetBinary(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Binary value, size_t valueLength);

/**
 * \brief Gets the value of a binary output variable.
 * \note Called as part of fmi3GetBinary.
 * \param[in] instance The instance data of the application.
 * \param[in] valueReference The value reference of the variable.
 * \param[out] value The new value of the variable.
 * \param[out] valueLength The new length of the variable.
 */
bool App_GetBinary(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Binary* value, size_t* valueLength);

/**
 * \brief Sets the value of an input clock.
 * \note Called as part of fmi3SetClock.
 * \param[in] instance The instance data of the application.
 * \param[in] valueReference The value reference of the clock.
 * \param[in] value The new value of the clock.
 */
bool App_SetClock(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Clock value);

/**
 * \brief Gets the value of a triggered output clock.
 * \note Called as part of fmi3GetClock.
 * \param[in] instance The instance data of the application.
 * \param[in] valueReference The value reference of the clock.
 * \param[out] value The new value of the clock.
 */
bool App_GetClock(FmuInstance* instance, fmi3ValueReference valueReference, fmi3Clock* value);

/**
 * \brief Gets the current interval of a countdown clock.
 * \note Called as part of fmi3GetIntervalFraction and fmi3GetIntervalDecimal.
 * \param[in] instance The instance data of the application.
 * \param[in] valueReference The value reference of the clock.
 * \param[out] counter The numerator of the new clock interval.
 * \param[out] resolution The denominator of the new clock interval.
 * \param[out] qualifier The qualifier of the indicated clock interval.
 */
bool App_GetIntervalFraction(FmuInstance* instance,
                             fmi3ValueReference valueReference,
                             fmi3UInt64* counter,
                             fmi3UInt64* resolution,
                             fmi3IntervalQualifier* qualifier);

#endif
