
#include "vcomdevice.h"
#include "vcomcontrol.h"
#include "dbgprint.h"

NTSTATUS DeviceCreate(WDFDRIVER driverHandle, PWDFDEVICE_INIT deviceInit, DEVICE_CONTEXT** deviceContext)
{

	UNREFERENCED_PARAMETER(driverHandle);

	dbgprintf("[i] VCOM DeviceCreate called\n");

	NTSTATUS status;
	WDFDEVICE deviceHandle;
	WDF_OBJECT_ATTRIBUTES attributes;
	
	// configure device object
	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_CONTEXT);
	attributes.EvtCleanupCallback = DeviceCleanup;

	// create device object
	status = WdfDeviceCreate(&deviceInit, &attributes, &deviceHandle);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM WdfDeviceCreate failed: NTSTATUS 0x%x\n", status);
		return status;
	}

	*deviceContext = GetDeviceContext(deviceHandle);
	(*deviceContext)->Device = deviceHandle;

	// create buffers
	status = CreateBuffers(&(*deviceContext)->BufferContext);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM CreateBuffers failed: NTSTATUS 0x%x\n", status);
		return status;
	}

	dbgprintf("[i] VCOM DeviceCreate completed\n");

	return STATUS_SUCCESS;

}

void DeviceCleanup(WDFDEVICE deviceHandle)
{

	DEVICE_CONTEXT* deviceContext = GetDeviceContext(deviceHandle);

	// cleanup buffers
	CleanupBuffers(&deviceContext->BufferContext);

	dbgprintf("[i] VCOM DeviceCleanup called\n");

}

NTSTATUS DeviceConfigure(DEVICE_CONTEXT* context)
{

	dbgprintf("[i] VCOM DeviceConfigure called\n");

	NTSTATUS status;
	WDFDEVICE deviceHandle = context->Device;
	GUID interfaceGUID = GUID_DEVINTERFACE_COMPORT;
	WDFKEY deviceRegKeyHandle;
	WDFSTRING interfaceNameHandle;
	WDF_OBJECT_ATTRIBUTES interfaceNameAttributes;
	DECLARE_UNICODE_STRING_SIZE(interfaceName, 32);

	DECLARE_CONST_UNICODE_STRING(portNameKey, REG_VALUENAME_PORNAME);
	DECLARE_CONST_UNICODE_STRING(portPathPrefix, L"\\DosDevices\\Global\\"); // equivalent to the \\\\.\\ prefix when accessing the port later
	DECLARE_UNICODE_STRING_SIZE(comPortName, 10);
	DECLARE_UNICODE_STRING_SIZE(comPortLink, 32);

	// get COM port device registry key entry handle
	status = WdfDeviceOpenRegistryKey(deviceHandle, PLUGPLAY_REGKEY_DEVICE, KEY_QUERY_VALUE, WDF_NO_OBJECT_ATTRIBUTES, &deviceRegKeyHandle);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM WdfDeviceOpenRegistryKey failed: NTSTATUS 0x%x\n", status);
		return status;
	}

	// read auto configured com port name from registry
	status = WdfRegistryQueryUnicodeString(deviceRegKeyHandle, &portNameKey, NULL, &comPortName);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM WdfRegistryQueryUnicodeString failed: NTSTATUS 0x%x\n", status);
		return status;
	}

	// close registry handle
	WdfRegistryClose(deviceRegKeyHandle);

	dbgprintf("[i] VCOM configuring port with registry name '%.*ls'\n", comPortName.Length, comPortName.Buffer);

	// create COM port device interface
	status = WdfDeviceCreateDeviceInterface(deviceHandle, &interfaceGUID, NULL);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM WdfDeviceCreateDeviceInterface failed: NTSTATUS 0x%x\n", status);
		return status;
	}

	// print COM device interface name
	WDF_OBJECT_ATTRIBUTES_INIT(&interfaceNameAttributes);
	interfaceNameAttributes.ParentObject = deviceHandle;
	status = WdfStringCreate(NULL, &interfaceNameAttributes, &interfaceNameHandle);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM WdfStringCreate failed: NTSTATUS 0x%x\n", status);
		return status;
	}
	status = WdfDeviceRetrieveDeviceInterfaceString(deviceHandle, &interfaceGUID, NULL, interfaceNameHandle);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM WdfDeviceRetrieveDeviceInterfaceString failed: NTSTATUS 0x%x\n", status);
		return status;
	}
	WdfStringGetUnicodeString(interfaceNameHandle, &interfaceName);
	
	dbgprintf("[1] VCOM created device interface: %.*ls\n", interfaceName.Length, interfaceName.Buffer);

	// join together port name and path prefix
	comPortLink.Length = portPathPrefix.Length + comPortName.Length;
	if (comPortLink.Length >= comPortLink.MaximumLength) {
		dbgerrprintf("[!] VCOM symbolic link path buffer overflow: %d > %d\n", comPortLink.Length, comPortLink.MaximumLength);
		return STATUS_BUFFER_OVERFLOW;
	}
	wcscpy_s(comPortLink.Buffer, comPortLink.MaximumLength, portPathPrefix.Buffer);
	wcscat_s(comPortLink.Buffer, comPortLink.MaximumLength, comPortName.Buffer);

	dbgprintf("[i] VCOM create device symbolic link: %.*ls\n", comPortLink.Length, comPortLink.Buffer);

	// create symbolic link for port
	status = WdfDeviceCreateSymbolicLink(deviceHandle, &comPortLink);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM WdfDeviceCreateSymbolicLink failed: NTSTATUS 0x%x\n", status);
		return status;
	}
	
	// TODO Lagacy Hardware Key

	// create new IO queue
	status = CreateIOQueue(context);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM DeviceConfigure failed: NTSTATUS 0x%x\n", status);
		return status;
	}

	return STATUS_SUCCESS;

}