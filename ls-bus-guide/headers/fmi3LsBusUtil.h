#ifndef fmi3LsBusUtil_h
#define fmi3LsBusUtil_h

/*

This header file contains utility macros to read and write fmi-ls-bus
bus operations from\to dedicated buffer variables.

This header can be used when creating Network FMUs.

Copyright (C) 2023 Modelica Association Project "FMI"
              All rights reserved.

This file is licensed by the copyright holders under the 2-Clause BSD License
(https://opensource.org/licenses/BSD-2-Clause):

----------------------------------------------------------------------------
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

- Redistributions of source code must retain the above copyright notice,
 this list of conditions and the following disclaimer.

- Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
----------------------------------------------------------------------------
*/

#include <string.h>

#include "fmi3LsBus.h"


#ifdef __cplusplus
extern "C"
{
#endif

/**
 * \brief This data type holds information to read and write bus operations to/from
 *  a buffer variable via utility macros such as \ref FMI3_LS_BUS_BUFFER_WRITE and
 *  \ref FMI3_LS_BUS_READ_NEXT_OPERATION.
 *
 * Variables of this type should be initialized using \ref FMI3_LS_BUS_BUFFER_INFO_INIT
 * and reset with \ref FMI3_LS_BUS_BUFFER_INFO_RESET.
 */
typedef struct
{
    fmi3UInt8* start;     /**< The start address of the buffer variable. */
    size_t size;          /**< The size of the buffer variable. */   
    fmi3UInt8* end;       /**< The end address of the buffer variable. */
    fmi3UInt8* writePos;  /**< The current write position. */
    fmi3UInt8* readPos;   /**< The current read position. */ 
    fmi3Boolean status;   /**< Holds the status (fmi3True\fmi3False) of the last macro call. */
} fmi3LsBusUtilBufferInfo;


/**
 * \brief Initializes a variable of type \ref fmi3LsBusUtilBufferInfo.
 *
 * This macro should be used to initialize variables of type \ref fmi3LsBusUtilBufferInfo.
 *
 * Example:
 * \code
 * fmi3UInt8 buffer[2048];
 * fmi3LsBusUtilBufferInfo bufferInfo;
 * FMI3_LS_BUS_BUFFER_INFO_INIT(&bufferInfo, buffer, sizeof(buffer));
 * \endcode
 *
 * \param[in] BufferInfo  Pointer to variable of type \ref fmi3LsBusUtilBufferInfo.
 * \param[in] Buffer      Pointer to buffer variable.
 * \param[in] Size        Size of the buffer variable.
 */
#define FMI3_LS_BUS_BUFFER_INFO_INIT(BufferInfo, Buffer, Size) \
    do                                                         \
    {                                                          \
        (BufferInfo)->start = (Buffer);                        \
        (BufferInfo)->size = (Size);                           \
        (BufferInfo)->end = (Buffer) + (Size);                 \
        (BufferInfo)->writePos = (BufferInfo)->start;          \
        (BufferInfo)->readPos = (BufferInfo)->start;           \
        (BufferInfo)->status = fmi3True;                       \
    }                                                          \
    while (0)

/**
 * \brief Resets a variable of type \ref fmi3LsBusUtilBufferInfo.
 *
 * This macro should be used to reset variables of type \ref fmi3LsBusUtilBufferInfo.
 * Read and write positions are set to the buffer start address.
 * The Variable must have been initialized using \ref FMI3_LS_BUS_BUFFER_INFO_INIT before.
 *
 * \param[in] BufferInfo  Pointer to variable of type \ref fmi3LsBusUtilBufferInfo.
 */
#define FMI3_LS_BUS_BUFFER_INFO_RESET(BufferInfo)     \
    do                                                \
    {                                                 \
        (BufferInfo)->writePos = (BufferInfo)->start; \
        (BufferInfo)->readPos = (BufferInfo)->start;  \
        (BufferInfo)->status = fmi3True;              \
    }                                                 \
    while (0)

/**
 * \brief Writes data to a buffer variable. Existing data will be overwritten.
 *
 * \param[in] BufferInfo  Pointer to variable of type \ref fmi3LsBusUtilBufferInfo.
 * \param[in] Data        Pointer to data to be written.
 * \param[in] DataLength  Size of the data to be written.
 */
#define FMI3_LS_BUS_BUFFER_WRITE(BufferInfo, Data, DataLength)           \
    do                                                                   \
    {                                                                    \
        if ((DataLength) <= (BufferInfo)->size)                          \
        {                                                                \
            memcpy((bufferInfo)->start, (Data), (DataLength));           \
            (bufferInfo)->writePos = (bufferInfo)->start + (DataLength); \
            (bufferInfo)->readPos = (bufferInfo)->start;                 \
            (bufferInfo)->status = fmi3True;                             \
        }                                                                \
        else                                                             \
        {                                                                \
            (bufferInfo)->status = fmi3False;                            \
        }                                                                \
    }                                                                    \
    while (0)

/**
 * \brief Reads the next bus operation from a buffer.
 *
 *  Example:
 *  \code
 *  fmi3LsBusOperationHeader* nextOperation;
 *  while (FMI3_LS_BUS_READ_NEXT_OPERATION(BufferInfo, nextOperation))
 *  {
 *    ...
 *  }
 *  \endcode
 * 
 * \param[in] BufferInfo  Pointer to variable of type \ref fmi3LsBusUtilBufferInfo.
 * \param[out] Operation  Pointer of type \ref fmi3LsBusOperationHeader* set by the macro
 *                        to the address where the next bus operation can be read from.
 * \return                fmi3True if a new operation is available, otherwise fmi3False.
 */
#define FMI3_LS_BUS_READ_NEXT_OPERATION(BufferInfo, Operation)                                                                    \
    ((fmi3UInt32)((BufferInfo)->writePos - (BufferInfo)->readPos) >= sizeof(fmi3LsBusOperationHeader) &&                          \
     (fmi3UInt32)((BufferInfo)->writePos - (BufferInfo)->readPos) >= ((fmi3LsBusOperationHeader*)(BufferInfo)->readPos)->length)  \
        ? ((Operation) = (fmi3LsBusOperationHeader*)(BufferInfo)->readPos, (BufferInfo)->readPos += (Operation)->length),         \
        fmi3True : fmi3False\

 /**
  * \brief Reads the next bus operation directly from a raw buffer.
  *
  *  Example:
  *  \code
  *  fmi3SetBinary(..., const size_t valueSizes[], const fmi3Binary values[], ...)
  *  {
  *      fmi3LsBusOperationHeader* nextOperation;
  *      size_t readPos = 0;
  *      ...
  *      while (FMI3_LS_BUS_READ_NEXT_OPERATION_DIRECT(values[i], valueSizes[i], readPos, nextOperation))
  *      {
  *          ...
  *      }
  *  }
  *  \endcode
  *
  * \param[in]     Buffer        Pointer to buffer variable of type fmi3Binary.
  * \param[in]     BufferLength  The length of the data in the buffer.
  * \param[in,out] ReadPos       Variable to hold the current read position.
  * \param[out]    Operation     Pointer of type \ref fmi3LsBusOperationHeader* set by the macro
  *                              to the address where the next bus operation can be read from.
  * \return                      fmi3True if a new operation is available, otherwise fmi3False.
  */
#define FMI3_LS_BUS_READ_NEXT_OPERATION_DIRECT(Buffer, BufferLength, ReadPos, Operation)                       \
     (((BufferLength) - (ReadPos)) >= sizeof(fmi3LsBusOperationHeader) &&                                      \
      ((BufferLength) - (ReadPos)) >= ((fmi3LsBusOperationHeader*)((Buffer) + (ReadPos)))->length)             \
        ? ((Operation) = (fmi3LsBusOperationHeader*)((Buffer) + (ReadPos)), (ReadPos) += (Operation)->length), \
        fmi3True : fmi3False\


#ifdef __cplusplus
} /* end of extern "C" { */
#endif

#endif /* fmi3LsBusUtil_h */
