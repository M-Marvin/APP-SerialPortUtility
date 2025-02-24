/*
 * virtualport.cpp
 *
 *  Created on: 10.02.2025
 *      Author: marvi
 */

#include "virtualport.h"

NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING registryPath) {

	// https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/writing-a-simple-kmdf-driver

	WDF_DRIVER_CONFIG driverConfig;
	WDF_DRIVER_CONFIG_INIT(&driverConfig, 0);
	driverConfig.EvtDriverUnload = &EvtWdfDriverUnload;
	driverConfig.DriverInitFlags |= WdfDriverInitNonPnpDriver;

	NTSTATUS status =  WdfDriverCreate(driverObject, registryPath, NULL, &driverConfig, WDF_NO_HANDLE);

	if (!NT_SUCCESS(status)) {
		DbgPrint("VirtualSerialPortDriver: WdfDriverCreate failed with status code: %ld\n", status);
	}

	return status;

}

void EvtWdfDriverUnload(WDFDRIVER driver) {

	DbgPrint("VirtualSerialPortDriver: Unload driver\n");

}
