
#include "vcomdriver.h"
#include "vcomdevice.h"
#include "dbgprint.h"

NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING registryPath)
{
	
	dbgprintf("[i] VCOM DriverEntry called: regpath: %.*ls\n", registryPath->Length, registryPath->Buffer);

	NTSTATUS status;
	WDFDRIVER driverHandle;
	WDF_OBJECT_ATTRIBUTES attributes;
	WDF_DRIVER_CONFIG config;

	// configure driver object
	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
	attributes.SynchronizationScope = WdfSynchronizationScopeDevice;
	WDF_DRIVER_CONFIG_INIT(&config, DriverDeviceAdd);

	// create driver object
	status = WdfDriverCreate(driverObject, registryPath, &attributes, &config, &driverHandle);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM WpfDriverCreate failed: NTSTATUS 0x%x\n", status);
		return status;
	}

	dbgprintf("[i] VCOM DriverEntry completed\n");

	return STATUS_SUCCESS;

}

NTSTATUS DriverDeviceAdd(WDFDRIVER driverHandle, PWDFDEVICE_INIT deviceInit)
{

	dbgprintf("[i] VCOM DriverDeviceAdd called\n");

	NTSTATUS status;
	DEVICE_CONTEXT* deviceContext;

	// create device
	status = DeviceCreate(driverHandle, deviceInit, &deviceContext);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM DeviceCreate failed: NTSTATUS 0x%x\n", status);
		return status;
	}

	// configure device
	status = DeviceConfigure(deviceContext);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM DeviceConfigure failed: NTSTATUS 0x%x\n", status);
		return status;
	}

	dbgprintf("[i] VCOM DriverDeviceAdd completed\n");

	return STATUS_SUCCESS;

}