#include "Utility.h"

uint64_t ConvertTimeToNs(fmi3Float64 time)
{
    return (fmi3UInt64)(time * 1.0E9 + 0.5);
}
