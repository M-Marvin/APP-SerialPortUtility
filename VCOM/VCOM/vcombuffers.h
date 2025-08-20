#pragma once

#include <Windows.h>
#include <wdf.h>
#include "public.h"

typedef struct {

    BUFFER_SIZES        BufferSizes;        // The currently configured sizes for the Rx/Tx buffers

    char* TransmitBuffer;
    ULONG               TransmitWritePtr;
    ULONG               TransmitReadPtr;
    char* ReceiveBuffer;
    ULONG               ReceiveWritePtr;
    ULONG               ReceiveReadPtr;
    
} BUFFER_CONTEXT;

NTSTATUS CreateBuffers(BUFFER_CONTEXT* bufferContext);
void CleanupBuffers(BUFFER_CONTEXT* bufferContext);

NTSTATUS ReallocateBuffers(BUFFER_CONTEXT* bufferContext);
NTSTATUS WriteTransmittion(BUFFER_CONTEXT* bufferContext, WDFREQUEST requestHandle, ULONG length, ULONG* bytesCopied, ULONG* bufferSize);
NTSTATUS ReadTransmittion(BUFFER_CONTEXT* bufferContext, WDFREQUEST requestHandle, ULONG length, ULONG* bytesCopied, ULONG* bufferSize);
NTSTATUS WriteReception(BUFFER_CONTEXT* bufferContext, WDFREQUEST requestHandle, ULONG length, ULONG* bytesCopied, ULONG* bufferSize);
NTSTATUS ReadReception(BUFFER_CONTEXT* bufferContext, WDFREQUEST requestHandle, ULONG length, ULONG* bytesCopied, ULONG* bufferSize);
