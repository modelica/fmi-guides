#ifndef LOGGING_H
#define LOGGING_H

#include "fmi3FunctionTypes.h"


void LogFmuMessage(fmi3Instance instance, fmi3Status status, fmi3String category, fmi3String message, ...);

fmi3Status TerminateWithError(fmi3Instance instance, fmi3String message, ...);


#endif
