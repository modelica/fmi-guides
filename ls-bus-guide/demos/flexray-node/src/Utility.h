#ifndef UTILITY_H
#define UTILITY_H

#include <stdint.h>

#include "fmi3PlatformTypes.h"

/**
 * Converts a time from seconds to nanoseconds.
 *
 * \param time The time in seconds.
 * \return The time in nanoseconds.
 */
uint64_t ConvertTimeToNs(fmi3Float64 time);

#endif
