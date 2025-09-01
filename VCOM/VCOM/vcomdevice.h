#pragma once

#include <Windows.h>
#include <wdf.h>
#include "serial.h"
#include "public.h"
#include "vcombuffers.h"

typedef struct
{
	
    WDFDEVICE           Device;

    ULONG               BaudRate;           // The baud rate currently configured
    SERIAL_LINE_CONTROL LineControl;        // The line control register currently configured (stop, data, parity)
    SERIAL_HANDFLOW     FlowControl;        // The flow control currently configured
    SERIAL_TIMEOUTS     Timeouts;           // The Tx/Rx timeouts currently configured
    SERIAL_CHARS        FlowChars;          // The XON/XOFF characters currently configured
    ULONG               FlowControlState;   // The manual flow control state currently configured (DTS, RTS)

    BUFFER_CONTEXT      BufferContext;


} DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext);

#define REG_VALUENAME_PORNAME L"PortName"

/// <summary>
/// Creates a new driver object for an new virtual serial port
/// </summary>
/// <param name="driverHandle">A handle for the driver object</param>
/// <param name="deviceInit">A opaque struct from the framework</param>
/// <param name="deviceContext">A pointer to store the address of the newly created device context</param>
/// <returns>STATUS_SUCCESS if no error occured</returns>
NTSTATUS DeviceCreate(WDFDRIVER driverHandle, PWDFDEVICE_INIT deviceInit, DEVICE_CONTEXT** deviceContext);

/// <summary>
/// Cleanup for the driver object to free all alocated resources
/// </summary>
/// <param name="deviceHandle">A handle for the device object</param>
/// <returns></returns>
VOID DeviceCleanup(WDFDEVICE deviceHandle);

/// <summary>
/// Configures the new virtual serial port device
/// </summary>
/// <param name="context">The objects device context</param>
/// <returns>STATUS_SUCCESS if no error occured</returns>
NTSTATUS DeviceConfigure(DEVICE_CONTEXT* context);
