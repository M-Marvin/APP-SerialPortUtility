#pragma once

#include <Windows.h>
#include <wdf.h>
#include "vcomioctl.h"

/* A ring buffer implementation used to buffer the data flowing between the virtual serial and the application */
typedef struct {
    char* Buffer;
    SIZE_T Capacity;
    SIZE_T WritePtr;
    SIZE_T ReadPtr;
} Ringbuffer;

/* Initializes a new ringbuffer struct, the buffer is set to default capacity */
NTSTATUS RingbufferCreate(Ringbuffer* bufferContext);
/* Frees the resources used by the ringbuffer */
void RingbufferCleanup(Ringbuffer* bufferContext);

/* Reallocates new memory to change the capacity of the buffer */
NTSTATUS RingbufferReallocate(Ringbuffer* bufferContext);
/* Clears the ringbuffer of all data */
void RingbufferClear(Ringbuffer* bufferContext);
/* Attempts to write data from an IO request to the ringbuffer */
NTSTATUS RingbufferWrite(Ringbuffer* bufferContext, WDFREQUEST requestHandle, SIZE_T length, SIZE_T* bytesCopied, SIZE_T* bufferContent);
/* Attempts to read data from the ringbuffer to the IO request*/
NTSTATUS RingbufferRead(Ringbuffer* bufferContext, WDFREQUEST requestHandle, SIZE_T length, SIZE_T* bytesCopied, SIZE_T* bufferContent);

