/**
 * \file OperationBuffer.h
 * \brief Ring buffer for bus operations.
 */

#ifndef OPERATION_BUFFER_H
#define OPERATION_BUFFER_H

#include <stdbool.h>

#include "fmi3PlatformTypes.h"

#include "fmi3LsBus.h"


/**
 * \brief Size of elements in a bus operation buffer.
 */
#define OPERATION_BUFFER_ELEMENT_SIZE (128)

/**
 * \brief Number of elements in a bus operation buffer.
 */
#define OPERATION_BUFFER_ELEMENT_COUNT (1024)

/**
 * \brief Ring buffer for bus operations.
 */
typedef struct
{
    fmi3Byte Buffer[OPERATION_BUFFER_ELEMENT_SIZE * OPERATION_BUFFER_ELEMENT_COUNT]; /**< The underlying data buffer. */

    fmi3UInt16 ReadIndex;  /**< The current read position (in multiples of elements). */
    fmi3UInt16 WriteIndex; /**< The current write position (in multiples of elements). */
    fmi3Boolean IsFull;    /**< Whether the buffer is full (to interpret ReadPos == WritePos correctly). */
} OperationBuffer;

/**
 * Initializes an operation buffer.
 *
 * \param buffer The operation buffer.
 */
void OperationBuffer_Init(OperationBuffer* buffer);

/**
 * Inserts a bus operations into an operation buffer.
 *
 * \param buffer The operation buffer.
 * \param operation The bus operation.
 * \return Whether the operation could be successfully pushed.
 */
bool OperationBuffer_Push(OperationBuffer* buffer, const fmi3LsBusOperationHeader* operation);

/**
 * Accesses the first element of an operation buffer.
 *
 * \param buffer The operation buffer.
 * \return The first element of the buffer or NULL, if it is empty.
 */
const fmi3LsBusOperationHeader* OperationBuffer_Peek(const OperationBuffer* buffer);
/**
 * Removes the first element of an operation buffer.
 *
 * \param buffer The operation buffer.
 * \return Whether the operation could be successfully removed.
 */
bool OperationBuffer_Pop(OperationBuffer* buffer);

#endif
