/**
 * \file OperationBuffer.c
 * \brief Ring buffer for bus operations.
 */

#include "OperationBuffer.h"

#include <stddef.h>


void OperationBuffer_Init(OperationBuffer* buffer)
{
    buffer->ReadIndex = 0;
    buffer->WriteIndex = 0;
    buffer->IsFull = fmi3False;
}

bool OperationBuffer_Push(OperationBuffer* buffer, const fmi3LsBusOperationHeader* operation)
{
    if (buffer->IsFull == fmi3True)
    {
        return false;
    }

    if (operation->length > OPERATION_BUFFER_ELEMENT_SIZE)
    {
        return false;
    }

    fmi3Byte* writePos = buffer->Buffer + OPERATION_BUFFER_ELEMENT_SIZE * (ptrdiff_t)buffer->WriteIndex;
    for (fmi3UInt32 i = 0; i < operation->length; i++)
    {
        writePos[i] = ((const fmi3UInt8*)operation)[i];
    }
    buffer->WriteIndex = (buffer->WriteIndex + 1) % OPERATION_BUFFER_ELEMENT_COUNT;

    if (buffer->WriteIndex == buffer->ReadIndex)
    {
        buffer->IsFull = fmi3True;
    }

    return true;
}

const fmi3LsBusOperationHeader* OperationBuffer_Peek(const OperationBuffer* buffer)
{
    if (buffer->WriteIndex == buffer->ReadIndex && buffer->IsFull == fmi3False)
    {
        return NULL;
    }

    return (const fmi3LsBusOperationHeader*)(buffer->Buffer +
                                             (ptrdiff_t)buffer->ReadIndex * OPERATION_BUFFER_ELEMENT_SIZE);
}

bool OperationBuffer_Pop(OperationBuffer* state)
{
    if (state->WriteIndex == state->ReadIndex && state->IsFull == fmi3False)
    {
        return false;
    }

    state->ReadIndex = (state->ReadIndex + 1) % OPERATION_BUFFER_ELEMENT_COUNT;
    state->IsFull = false;

    return true;
}
