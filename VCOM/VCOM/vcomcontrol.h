#pragma once

#include "vcomdevice.h"

typedef struct 
{

    WDFQUEUE        Queue;              // Default parallel queue
    WDFQUEUE        WriteQueue;         // Manual queue for pending writes
    WDFQUEUE        ReadQueue;          // Manual queue for pending reads
    WDFQUEUE        WaitMaskQueue;      // Manual queue for pending ioctl wait-on-mask
    WDFQUEUE        WaitChangeQueue;    // Manual queue for pending ioctl wait-on-change
    ULONG           WaitMask;
    ULONG           ChangeMask;
    WDFTIMER        ReadTimeoutTimer;
    WDFTIMER        ReadIntervalTimeoutTimer;
    WDFTIMER        WriteTimeoutTimer;

    DEVICE_CONTEXT* DeviceContext;

} QUEUE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(QUEUE_CONTEXT, GetQueueContext);

typedef union
{
    struct {
        ULONG BytesTransfered;
    } ReadWrite;

} REQUEST_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(REQUEST_CONTEXT, GetRequestContext);

/// <summary>
/// Creates a new IO Event queue for the device
/// </summary>
/// <param name="deviceContext">The device context</param>
/// <returns>STATUS_SUCCESS if no error occured</returns>
NTSTATUS CreateIOQueue(DEVICE_CONTEXT* deviceContext);

NTSTATUS CopyFromRequest(WDFREQUEST requestHandle, ULONG requestBufferOffset, void* buffer, size_t bytesToCopy);
NTSTATUS CopyToRequest(WDFREQUEST requestHandle, ULONG requestBufferOffset, void* buffer, size_t bytesToCopy);

void IORead(WDFQUEUE queueHandle, WDFREQUEST requestHandle, size_t length);
void EvtReadTimedOut(WDFTIMER timer);

void IOWrite(WDFQUEUE queueHandle, WDFREQUEST requestHandle, size_t length);
void EvtWriteTimedOut(WDFTIMER timer);

void IODeviceControl(WDFQUEUE queueHandle, WDFREQUEST requestHandle, size_t outputBufferLength, size_t inputBufferLength, ULONG controlCode);
