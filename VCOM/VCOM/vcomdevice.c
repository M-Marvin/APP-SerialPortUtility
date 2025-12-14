
#include "vcomdevice.h"
#include "vcomcontrol.h"
#include "dbgprint.h"
#include <wdfdevice.h>

NTSTATUS DeviceCreate(WDFDRIVER driverHandle, PWDFDEVICE_INIT deviceInit, DEVICE_CONTEXT** deviceContext)
{

	UNREFERENCED_PARAMETER(driverHandle);

	dbgprintf("[i] VCOM DeviceCreate called\n");

	NTSTATUS status;
	WDFDEVICE deviceHandle;
	WDF_OBJECT_ATTRIBUTES attributes;
	WDF_OBJECT_ATTRIBUTES requestAttributes;
	
	// configure device object
	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_CONTEXT);
	attributes.EvtCleanupCallback = DeviceCleanup;

	//WdfDeviceInitSetExclusive(&deviceInit, TRUE);

	// configure request context
	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&requestAttributes, REQUEST_CONTEXT);
	WdfDeviceInitSetRequestAttributes(deviceInit, &requestAttributes);

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
	NTSTATUS                status;
	WDFKEY                  deviceMapKey = NULL;
	UNICODE_STRING          pdoString = { 0 };

	DECLARE_CONST_UNICODE_STRING(deviceSubkey, SERIAL_DEVICE_MAP);

	// open device map key
	status = WdfDeviceOpenDevicemapKey(deviceHandle, &deviceSubkey, KEY_SET_VALUE, WDF_NO_OBJECT_ATTRIBUTES, &deviceMapKey);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM WdfDeviceOpenDevicemapKey failed: NTSTATUS 0x%x\n", status);
		goto end;
	}
	
	// remove lagacy hardware key
	RtlInitUnicodeString(&pdoString, deviceContext->PdoName);
	status = WdfRegistryRemoveValue(deviceMapKey, &pdoString);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM WdfRegistryRemoveValue failed: NTSTATUS 0x%x\n", status);
		WdfRegistryClose(deviceMapKey);
		goto end;
	}

	WdfRegistryClose(deviceMapKey);
	end:

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
	{

		WDF_OBJECT_ATTRIBUTES   memoryAttributes;
		WDFMEMORY               memory;
		WDFKEY                  deviceMapKey = NULL;
		UNICODE_STRING          pdoString = { 0 };

		DECLARE_CONST_UNICODE_STRING(deviceSubkey, SERIAL_DEVICE_MAP);
		
		// configure attributes for pdo name memory object
		WDF_OBJECT_ATTRIBUTES_INIT(&memoryAttributes);
		memoryAttributes.ParentObject = deviceHandle;

		// get physical device object (pdo) name
		status = WdfDeviceAllocAndQueryProperty(deviceHandle, DevicePropertyPhysicalDeviceObjectName, NonPagedPoolNx, &memoryAttributes, &memory);
		if (status != STATUS_SUCCESS) {
			dbgerrprintf("[!] VCOM WdfDeviceAllocAndQueryProperty failed: NTSTATUS 0x%x\n", status);
			return status;
		}
		context->PdoName = WdfMemoryGetBuffer(memory, NULL);

		dbgprintf("[i] VCOM port pdo name: %ws\n", context->PdoName);
		
		// open device map registry key
		status = WdfDeviceOpenDevicemapKey(deviceHandle, &deviceSubkey, KEY_SET_VALUE, WDF_NO_OBJECT_ATTRIBUTES, &deviceMapKey);
		if (status != STATUS_SUCCESS) {
			dbgerrprintf("[!] VCOM WdfDeviceOpenDevicemapKey failed: NTSTATUS 0x%x\n", status);
			return status;
		}

		// set lagacy hardware key
		RtlInitUnicodeString(&pdoString, context->PdoName);
		status = WdfRegistryAssignUnicodeString(deviceMapKey, &pdoString, &comPortName);
		if (status != STATUS_SUCCESS) {
			dbgerrprintf("[!] VCOM WdfRegistryAssignUnicodeString failed: NTSTATUS 0x%x\n", status);
			WdfRegistryClose(deviceMapKey);
			return status;
		}

		WdfRegistryClose(deviceMapKey);
	}

	// create new IO queue
	status = CreateIOQueue(context);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM DeviceConfigure failed: NTSTATUS 0x%x\n", status);
		return status;
	}

	return STATUS_SUCCESS;

}