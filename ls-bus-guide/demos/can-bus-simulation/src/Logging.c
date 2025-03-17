#include "Logging.h"

#include <stdarg.h>
#include <stdio.h>

#include "Instance.h"


enum { LOG_BUFFER_SIZE = 4096 };


void LogFmuMessageV(fmi3Instance instance,
                    fmi3Status status,
                    fmi3String category,
                    fmi3String message,
                    va_list args)
{
    const FmuInstance* fmuInstance = instance;
    if (!fmuInstance->LogMessageCallback)
    {
        return;
    }

    fmi3Char buffer[LOG_BUFFER_SIZE];
    vsnprintf(buffer, LOG_BUFFER_SIZE, message, args);

    fmuInstance->LogMessageCallback(fmuInstance->InstanceEnvironment, status, category, buffer);
}


void LogFmuMessage(fmi3Instance instance,
                   fmi3Status status,
                   fmi3String category,
                   fmi3String message,
                   ...)
{
    va_list args;
    va_start(args, message);
    LogFmuMessageV(instance, status, category, message, args);
    va_end(args);
}



fmi3Status TerminateWithError(fmi3Instance instance,
                              fmi3String message,
                              ...)
{
    FmuInstance* fmuInstance = instance;
    fmuInstance->State = FMU_STATE_TERMINATED;

    va_list args;
    va_start(args, message);
    LogFmuMessageV(instance, fmi3Error, "logStatusError", message, args);
    va_end(args);

    return fmi3Error;
}
