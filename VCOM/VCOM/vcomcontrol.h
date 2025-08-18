#pragma once

#include "vcomdevice.h"

typedef struct 
{

    WDFQUEUE        Queue;              // Default parallel queue
    WDFQUEUE        ReadQueue;          // Manual queue for pending reads
    WDFQUEUE        WaitMaskQueue;      // Manual queue for pending ioctl wait-on-mask
    ULONG           WaitMask;

    DeviceContext* DeviceContext;

} QueueContext;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(QueueContext, GetQueueContext);

/// <summary>
/// Creates a new IO Event queue for the device
/// </summary>
/// <param name="deviceContext">The device context</param>
/// <returns>STATUS_SUCCESS if no error occured</returns>
NTSTATUS CreateIOQueue(DeviceContext* deviceContext);

NTSTATUS CopyFromRequest(WDFREQUEST requestHandle, void* buffer, size_t bytesToCopy);
NTSTATUS CopyToRequest(WDFREQUEST requestHandle, void* buffer, size_t bytesToCopy);

void IORead(WDFQUEUE queueHandle, WDFREQUEST requestHandle, size_t length);

void IOWrite(WDFQUEUE queueHandle, WDFREQUEST requestHandle, size_t length);

void IODeviceControl(WDFQUEUE queueHandle, WDFREQUEST requestHandle, size_t outputBufferLength, size_t inputBufferLength, ULONG controlCode);
