/* Implements common IO and device management functions for the port device.
 * Among other things, handles creation of the symbolic links and registry entries for each port.
 */

#include "vcomport.h"
#include "dbgprint.h"

NTSTATUS PortDeviceCreate(WDFDRIVER driverHandle, PWDFDEVICE_INIT deviceInit, DEVICE_CONTEXT** deviceContext)
{

	UNREFERENCED_PARAMETER(driverHandle);

	dbgprintf("[i] VCOM PortDeviceCreate called\n");

	NTSTATUS status;
	WDFDEVICE deviceHandle;
	WDF_OBJECT_ATTRIBUTES attributes;
	WDF_OBJECT_ATTRIBUTES requestAttributes;

	// configure device object
	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_CONTEXT);
	attributes.EvtCleanupCallback = PortDeviceCleanup;

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

	*deviceContext = GetPortDeviceContext(deviceHandle);
	(*deviceContext)->Device = deviceHandle;

	// create data rx/tx buffers
	status = RingbufferCreate(&(*deviceContext)->SerialTxBuffer);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM RingbufferCreate(SerialTxBuffer) failed: NTSTATUS 0x%x\n", status);
		return status;
	}
	status = RingbufferCreate(&(*deviceContext)->SerialRxBuffer);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM RingbufferCreate(SerialRxBuffer) failed: NTSTATUS 0x%x\n", status);
		return status;
	}

	dbgprintf("[i] VCOM PortDeviceCreate completed\n");

	return STATUS_SUCCESS;

}

void PortDeviceCleanup(WDFDEVICE deviceHandle)
{

	dbgprintf("[i] VCOM PortDeviceCleanup called\n");

	DEVICE_CONTEXT* deviceContext = GetPortDeviceContext(deviceHandle);
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
	RingbufferCleanup(&deviceContext->SerialTxBuffer);
	RingbufferCleanup(&deviceContext->SerialRxBuffer);

	dbgprintf("[i] VCOM PortDeviceCleanup completed\n");

}

NTSTATUS PortDeviceConfigure(DEVICE_CONTEXT* queueContext)
{

	dbgprintf("[i] VCOM PortDeviceConfigure called\n");

	NTSTATUS status;
	WDFDEVICE deviceHandle = queueContext->Device;
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

	// Lagacy Hardware Key
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
		queueContext->PdoName = WdfMemoryGetBuffer(memory, NULL);

		dbgprintf("[i] VCOM port pdo name: %ws\n", queueContext->PdoName);

		// open device map registry key
		status = WdfDeviceOpenDevicemapKey(deviceHandle, &deviceSubkey, KEY_SET_VALUE, WDF_NO_OBJECT_ATTRIBUTES, &deviceMapKey);
		if (status != STATUS_SUCCESS) {
			dbgerrprintf("[!] VCOM WdfDeviceOpenDevicemapKey failed: NTSTATUS 0x%x\n", status);
			return status;
		}

		// set lagacy hardware key
		RtlInitUnicodeString(&pdoString, queueContext->PdoName);
		status = WdfRegistryAssignUnicodeString(deviceMapKey, &pdoString, &comPortName);
		if (status != STATUS_SUCCESS) {
			dbgerrprintf("[!] VCOM WdfRegistryAssignUnicodeString failed: NTSTATUS 0x%x\n", status);
			WdfRegistryClose(deviceMapKey);
			return status;
		}

		WdfRegistryClose(deviceMapKey);
	}

	// create new IO queues
	status = PortDeviceCreateIOQueues(queueContext);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM PortDeviceCreateIOQueues failed: NTSTATUS 0x%x\n", status);
		return status;
	}

	dbgprintf("[i] VCOM PortDeviceConfigure completed\n");

	return STATUS_SUCCESS;

}

/* Helper method for creating a new IO queue */
static NTSTATUS CreateRequestQueue(WDFDEVICE deviceHandle, WDFQUEUE* queueHandle)
{
	WDF_IO_QUEUE_CONFIG queueConfig;
	NTSTATUS status;

	// configure serial write IO queue (pending serial write requests are redirected here)
	WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);
	status = WdfIoQueueCreate(deviceHandle, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, queueHandle);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM WdfIoQueueCreate for write queue failed: NTSTATUS 0x%x\n", status);
		return status;
	}

	return status;
}

/* Helper method for creating a new IO queue timer */
static NTSTATUS CreateQueueTimer(PWDF_OBJECT_ATTRIBUTES attributes, PFN_WDF_TIMER eventFunc, WDFTIMER* timerHandle)
{
	WDF_TIMER_CONFIG timerConfig;
	NTSTATUS status;

	// configure queue timeout timer
	WDF_TIMER_CONFIG_INIT(&timerConfig, eventFunc);
	timerConfig.AutomaticSerialization = FALSE;
	status = WdfTimerCreate(&timerConfig, attributes, timerHandle);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM WdfTimerCreate for IO queue timeout timer failed: NTSTATUS 0x%x\n", status);
		return status;
	}

	return status;
}

NTSTATUS PortDeviceCreateIOQueues(DEVICE_CONTEXT* deviceContext)
{

	dbgprintf("[i] VCOM PortDeviceCreateIOQueues called\n");

	NTSTATUS status;
	WDFDEVICE deviceHandle = deviceContext->Device;
	WDF_OBJECT_ATTRIBUTES attributes;
	WDF_IO_QUEUE_CONFIG queueConfig;
	QUEUE_CONTEXT* queueContext;
	WDFQUEUE defaultQueueHandle;

	// configure default IO queue (incomming requests end up here)
	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);
	queueConfig.EvtIoRead = PortDeviceIOReadSerial;
	queueConfig.EvtIoWrite = PortDeviceIOWriteSerial;
	queueConfig.EvtIoDeviceControl = PortDeviceIOControl;

	// create default IO queue
	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, QUEUE_CONTEXT);
	status = WdfIoQueueCreate(deviceHandle, &queueConfig, &attributes, &defaultQueueHandle);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM PortDeviceCreateIOQueues:WdfIoQueueCreate for default queue failed: NTSTATUS 0x%x\n", status);
		return status;
	}
	queueContext = GetQueueContext(defaultQueueHandle);
	queueContext->Queue = defaultQueueHandle;
	queueContext->DeviceContext = deviceContext;

	// configure serial and app read/write request queues (pending requests are parked here)
	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
	attributes.ParentObject = queueContext->Queue;
	if ((status = CreateQueueTimer(&attributes, &PortDeviceEvtReadSerialTimedOut, &queueContext->SerialReadTimer)) != STATUS_SUCCESS) return status;
	if ((status = CreateQueueTimer(&attributes, &PortDeviceEvtReadSerialTimedOut, &queueContext->SerialReadIntervalTimer)) != STATUS_SUCCESS) return status;
	if ((status = CreateQueueTimer(&attributes, &PortDeviceEvtWriteSerialTimedOut, &queueContext->SerialWriteTimer)) != STATUS_SUCCESS) return status;
	if ((status = CreateQueueTimer(&attributes, &PortDeviceEvtReadAppTimedOut, &queueContext->AppReadTimer)) != STATUS_SUCCESS) return status;
	if ((status = CreateQueueTimer(&attributes, &PortDeviceEvtReadAppTimedOut, &queueContext->AppReadIntervalTimer)) != STATUS_SUCCESS) return status;
	if ((status = CreateQueueTimer(&attributes, &PortDeviceEvtWriteAppTimedOut, &queueContext->AppWriteTimer)) != STATUS_SUCCESS) return status;

	// configure serial and app read/write queue timers (used for request timeouts) 
	if ((status = CreateRequestQueue(deviceHandle, &queueContext->SerialReadQueue)) != STATUS_SUCCESS) return status;
	if ((status = CreateRequestQueue(deviceHandle, &queueContext->SerialWriteQueue)) != STATUS_SUCCESS) return status;
	if ((status = CreateRequestQueue(deviceHandle, &queueContext->SerialWaitQueue)) != STATUS_SUCCESS) return status;
	if ((status = CreateRequestQueue(deviceHandle, &queueContext->AppReadQueue)) != STATUS_SUCCESS) return status;
	if ((status = CreateRequestQueue(deviceHandle, &queueContext->AppWriteQueue)) != STATUS_SUCCESS) return status;
	if ((status = CreateRequestQueue(deviceHandle, &queueContext->AppWaitQueue)) != STATUS_SUCCESS) return status;
	
	dbgprintf("[i] VCOM PortDeviceCreateIOQueues completed\n");

	return STATUS_SUCCESS;

}

void PortDeviceIOControl(WDFQUEUE queueHandle, WDFREQUEST requestHandle, SIZE_T outputBufferLength, SIZE_T inputBufferLength, ULONG controlCode)
{
	UNREFERENCED_PARAMETER(inputBufferLength);
	UNREFERENCED_PARAMETER(outputBufferLength);

	if ((controlCode & 0x3FFC) >> 2 < 2048)
	{
		PortDeviceIOCTLSerial(queueHandle, requestHandle, outputBufferLength, inputBufferLength, controlCode);
	}
	else
	{
		PortDeviceIOCTLApp(queueHandle, requestHandle, outputBufferLength, inputBufferLength, controlCode);
	}
}

NTSTATUS PortDeviceCopyFromRequest(WDFREQUEST requestHandle, SIZE_T requestBufferOffset, void* buffer, SIZE_T bytesToCopy)
{
	NTSTATUS status;
	WDFMEMORY memoryHandle;

	status = WdfRequestRetrieveInputMemory(requestHandle, &memoryHandle);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM PortDeviceCopyFromRequest:WdfRequestRetrieveInputMemory failed: NTSTATUS 0x%x\n", status);
		return status;
	}

	status = WdfMemoryCopyToBuffer(memoryHandle, requestBufferOffset, buffer, bytesToCopy);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM PortDeviceCopyFromRequest:WdfMemoryCopyToBuffer failed: NTSTATUS 0x%x\n", status);
		return status;
	}

	return STATUS_SUCCESS;
}

NTSTATUS PortDeviceCopyToRequest(WDFREQUEST requestHandle, SIZE_T requestBufferOffset, void* buffer, SIZE_T bytesToCopy)
{
	NTSTATUS status;
	WDFMEMORY memoryHandle;

	status = WdfRequestRetrieveOutputMemory(requestHandle, &memoryHandle);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM PortDeviceCopyToRequest:WdfRequestRetrieveOutputMemory failed: NTSTATUS 0x%x\n", status);
		return status;
	}

	status = WdfMemoryCopyFromBuffer(memoryHandle, requestBufferOffset, buffer, bytesToCopy);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM PortDeviceCopyToRequest:WdfMemoryCopyFromBuffer failed: NTSTATUS 0x%x\n", status);
		return status;
	}

	return STATUS_SUCCESS;
}

void PortDeviceReInvokeRequests(QUEUE_CONTEXT* queueContext, WDFQUEUE queue) {
	NTSTATUS status;
	WDFREQUEST request;

	for (; ; ) {

		status = WdfIoQueueRetrieveNextRequest(queue, &request);
		if (status != STATUS_SUCCESS) break;

		status = WdfRequestForwardToIoQueue(request, queueContext->Queue);
		if (status != STATUS_SUCCESS) {
			dbgerrprintf("[!] VCOM PortDeviceReInvokeRequests:WdfRequestForwardToIoQueue failed: NTSTATUS 0x%x\n", status);
			WdfRequestComplete(request, status);
		}

	}
}

void PortDeviceTimeoutRequests(WDFQUEUE queue) {
	NTSTATUS status;
	WDFREQUEST request;

	for (; ; ) {

		status = WdfIoQueueRetrieveNextRequest(queue, &request);
		if (status != STATUS_SUCCESS) break;

		REQUEST_CONTEXT* requestContext = GetRequestContext(request);
		WdfRequestCompleteWithInformation(request, STATUS_TIMEOUT, requestContext->ReadWrite.BytesTransfered);

	}
}

void PortDeviceTriggerAppWait(QUEUE_CONTEXT* queueContext, ULONG triggerMask)
{
	if ((queueContext->DeviceContext->AppWaitMask & triggerMask) == 0) return;

	WDFREQUEST request;
	NTSTATUS status;
	do {
		status = WdfIoQueueRetrieveNextRequest(queueContext->AppWaitQueue, &request);
		if (status == STATUS_SUCCESS) {

			dbgprintf("[i] VCOM PortDeviceTriggerAppWait complete wait: %x\n", triggerMask);

			WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, triggerMask);
		}
	} while (status == STATUS_SUCCESS);
}

void PortDeviceTriggerSerialWait(QUEUE_CONTEXT* queueContext, ULONG triggerMask)
{
	if ((queueContext->DeviceContext->SerialWaitMask & triggerMask) == 0) return;

	WDFREQUEST request;
	NTSTATUS status;
	do {
		status = WdfIoQueueRetrieveNextRequest(queueContext->SerialWaitQueue, &request);
		if (status == STATUS_SUCCESS) {

			dbgprintf("[i] VCOM PortDeviceTriggerSerialWait complete wait: %x\n", triggerMask);
			
			WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, triggerMask);
		}
	} while (status == STATUS_SUCCESS);
}
