#pragma once

#include <Windows.h>
#include <wdf.h>

/// <summary>
/// First method called when the driver is loaded the first time for an device.
/// </summary>
/// <param name="driverObject">Reference to the driver object</param>
/// <param name="registryPath">Path for the drivers registry entry</param>
/// <returns>STATUS_SUCCESS if no errors occured</returns>
NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING registryPath);

/// <summary>
/// Called when the driver is invoked for a nwe device, initializes everything required for the device to get operational.
/// </summary>
/// <param name="Driver">An handle for the driver</param>
/// <param name="DeviceInit">An opaque struct from the framework</param>
/// <returns>STATUS_SUCCESS if no errors occured</returns>
NTSTATUS DriverDeviceAdd(WDFDRIVER driverHandle, PWDFDEVICE_INIT deviceInit);
