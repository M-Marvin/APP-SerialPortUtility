
#include "vcomcontrol.h"
#include "dbgprint.h"
#include "public.h"

NTSTATUS CreateIOQueue(DEVICE_CONTEXT* deviceContext)
{

	dbgprintf("[i] VCOM CreateIOQueue called\n");

	NTSTATUS status;
	WDFDEVICE deviceHandle = deviceContext->Device;
	WDF_OBJECT_ATTRIBUTES attributes;
	WDF_IO_QUEUE_CONFIG config;
	WDFQUEUE defaultQueueHandle;
	WDFQUEUE readQueueHandle;
	WDFQUEUE waitQueueHandle;
	QUEUE_CONTEXT* context;

	// configure default IO queue
	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&config, WdfIoQueueDispatchParallel);
	config.EvtIoRead = IORead;
	config.EvtIoWrite = IOWrite;
	config.EvtIoDeviceControl = IODeviceControl;
	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, QUEUE_CONTEXT);

	// create default IO queue
	status = WdfIoQueueCreate(deviceHandle, &config, &attributes, &defaultQueueHandle);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM WdfIoQueueCreate for default queue failed: NTSTATUS 0x%x\n", status);
		return status;
	}

	context = GetQueueContext(defaultQueueHandle);
	context->Queue = defaultQueueHandle;
	context->DeviceContext = deviceContext;

	// configure read IO queue
	WDF_IO_QUEUE_CONFIG_INIT(&config, WdfIoQueueDispatchManual);

	// create read IO queue
	status = WdfIoQueueCreate(deviceHandle, &config, WDF_NO_OBJECT_ATTRIBUTES, &readQueueHandle);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM WdfIoQueueCreate for read queue failed: NTSTATUS 0x%x\n", status);
		return status;
	}

	context->ReadQueue = readQueueHandle;

	// configure wait IO queue
	WDF_IO_QUEUE_CONFIG_INIT(&config, WdfIoQueueDispatchManual);

	// create wait IO queue
	status = WdfIoQueueCreate(deviceHandle, &config, WDF_NO_OBJECT_ATTRIBUTES, &waitQueueHandle);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM WdfIoQueueCreate for wait queue failed: NTSTATUS 0x%x\n", status);
		return status;
	}

	context->WaitMaskQueue = waitQueueHandle;

	dbgprintf("[i] VCOM CreateIOQueue completed\n");

	return STATUS_SUCCESS;

}

NTSTATUS CopyFromRequest(WDFREQUEST requestHandle, void* buffer, size_t bytesToCopy)
{

	NTSTATUS status;
	WDFMEMORY memoryHandle;

	status = WdfRequestRetrieveInputMemory(requestHandle, &memoryHandle);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM WdfRequestRetrieveInputMemory failed: NTSTATUS 0x%x\n", status);
		return status;
	}

	status = WdfMemoryCopyToBuffer(memoryHandle, 0, buffer, bytesToCopy);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM WdfMemoryCopyToBuffer failed: NTSTATUS 0x%x\n", status);
		return status;
	}

	return STATUS_SUCCESS;

}

NTSTATUS CopyToRequest(WDFREQUEST requestHandle, void* buffer, size_t bytesToCopy)
{

	NTSTATUS status;
	WDFMEMORY memoryHandle;

	status = WdfRequestRetrieveOutputMemory(requestHandle, &memoryHandle);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM WdfRequestRetrieveOutputMemory failed: NTSTATUS 0x%x\n", status);
		return status;
	}

	status = WdfMemoryCopyFromBuffer(memoryHandle, 0, buffer, bytesToCopy);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM WdfMemoryCopyFromBuffer failed: NTSTATUS 0x%x\n", status);
		return status;
	}

	return STATUS_SUCCESS;

}

void IODeviceControl(WDFQUEUE queueHandle, WDFREQUEST requestHandle, size_t outputBufferLength, size_t inputBufferLength, ULONG controlCode)
{

	UNREFERENCED_PARAMETER(inputBufferLength);
	UNREFERENCED_PARAMETER(outputBufferLength);

	dbgprintf("[i] VCOM IODeviceControl called: IOCTL_CODE: %lu\n", controlCode);

	NTSTATUS status;
	QUEUE_CONTEXT* queueContext = GetQueueContext(queueHandle);
	DEVICE_CONTEXT* deviceContext = queueContext->DeviceContext;
	BUFFER_CONTEXT* bufferContext = &deviceContext->BufferContext;

	switch (controlCode) {

		/* port configuration control (set baud, timeouts, flow control, etc ) */

	case IOCTL_SERIAL_SET_BAUD_RATE: // the baud rate of the port as ULONG inside a struct
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_SET_BAUD_RATE called\n");

		SERIAL_BAUD_RATE baudRateStruct = { 0 };

		// read baud rate and apply to context
		status = CopyFromRequest(requestHandle, &baudRateStruct, sizeof(SERIAL_BAUD_RATE));
		if (status == STATUS_SUCCESS) {
			deviceContext->BaudRate = baudRateStruct.BaudRate;
		}

		break;
	}
	case IOCTL_SERIAL_GET_BAUD_RATE:
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_GET_BAUD_RATE called\n");

		SERIAL_BAUD_RATE baudRateStruct = {
			.BaudRate = deviceContext->BaudRate
		};

		// write baud rate to request
		status = CopyToRequest(requestHandle, &baudRateStruct, sizeof(SERIAL_BAUD_RATE));
		
		break;
	}


	case IOCTL_SERIAL_GET_TIMEOUTS: // transmission timeout for read and write as an struct
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_GET_TIMEOUTS called\n");

		// write timeouts to request
		status = CopyToRequest(requestHandle, &deviceContext->Timeouts, sizeof(SERIAL_TIMEOUTS));

		break;
	}
	case IOCTL_SERIAL_SET_TIMEOUTS:
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_SET_TIMEOUTS called\n");

		// read timeouts and apply to context
		status = CopyFromRequest(requestHandle, &deviceContext->Timeouts, sizeof(SERIAL_TIMEOUTS));

		break;
	}

	case IOCTL_SERIAL_GET_LINE_CONTROL: // number of data and stop bits and parity configuration as an struct 
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_GET_LINE_CONTROL called\n");

		// write line control to request
		status = CopyToRequest(requestHandle, &deviceContext->LineControl, sizeof(SERIAL_LINE_CONTROL));

		break;
	}
	case IOCTL_SERIAL_SET_LINE_CONTROL:
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_SET_LINE_CONTROL called\n");

		// read líne control and apply to context
		status = CopyFromRequest(requestHandle, &deviceContext->LineControl, sizeof(SERIAL_LINE_CONTROL));

		break;
	}

	case IOCTL_SERIAL_GET_CHARS: // the characters used for XON/XOFF flow control in an struct
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_GET_CHARS called\n");

		// write XON/XOFF chars to request
		status = CopyToRequest(requestHandle, &deviceContext->FlowChars, sizeof(SERIAL_CHARS));

		break;
	}

	case IOCTL_SERIAL_SET_CHARS:
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_SET_CHARS called\n");

		// read XON/XOFF chars and apply to context
		status = CopyFromRequest(requestHandle, &deviceContext->FlowChars, sizeof(SERIAL_CHARS));

		break;
	}

		/* controls the serial port (flow control lines and buffers) */

	case IOCTL_SERIAL_WAIT_ON_MASK:
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_WAIT_ON_MASK called\n");

		WDFREQUEST previousWaitRequest;

		// abort the previous wait request, if there is one
		status = WdfIoQueueRetrieveNextRequest(queueContext->WaitMaskQueue, &previousWaitRequest);
		if (status == STATUS_SUCCESS) {
			WdfRequestComplete(previousWaitRequest, STATUS_UNSUCCESSFUL);
		}

		// put the new request into the wait queue
		status = WdfRequestForwardToIoQueue(requestHandle, queueContext->WaitMaskQueue);
		if (status != STATUS_SUCCESS) {
			dbgerrprintf("[!] VCOM WdfRequestForwardToIoQueue failed: NTSTATUS 0x%x\n", status);
			break;
		}

		return; // do not continue, don't complete the request
	}

	case IOCTL_SERIAL_SET_WAIT_MASK:
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_SET_WAIT_MASK called\n");

		// read wait mask and apply to context
		status = CopyFromRequest(requestHandle, &queueContext->WaitMask, sizeof(ULONG));

		WDFREQUEST previousWaitRequest;

		// abort pendinjg wait request, if there is one
		status = WdfIoQueueRetrieveNextRequest(queueContext->WaitMaskQueue, &previousWaitRequest);
		if (status == STATUS_SUCCESS) {

			// zero indicates that no event was actualy triggered, but the wait ended because of this set mask request
			ULONG eventMask = 0;
			status = CopyToRequest(
				previousWaitRequest,
				&eventMask,
				sizeof(eventMask));

			WdfRequestComplete(previousWaitRequest, STATUS_SUCCESS);

		}

		break;
	}
	case IOCTL_SERIAL_GET_WAIT_MASK:
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_GET_WAIT_MASK called\n");

		// write wait mask to request
		status = CopyToRequest(requestHandle, &queueContext->WaitMask, sizeof(ULONG));

		break;
	}

	case  IOCTL_SERIAL_SET_DTR: // set the DTR line if not controled by autmatic flow control
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_SET_DTR called\n");

		// set DTR bit
		deviceContext->FlowControlState |= SERIAL_DTR_STATE;
		status = STATUS_SUCCESS;

		break;
	}
	case  IOCTL_SERIAL_CLR_DTR:
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_CLR_DTR called\n");

		// clear DTR bit
		deviceContext->FlowControlState &= ~SERIAL_DTR_STATE;
		status = STATUS_SUCCESS;

		break;
	}

	case  IOCTL_SERIAL_SET_RTS: // set the RTS line if not controled by autmatic flow control
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_SET_RTS called\n");

		// set RTS bit
		deviceContext->FlowControlState |= SERIAL_RTS_STATE;
		status = STATUS_SUCCESS;

		break;
	}

	case  IOCTL_SERIAL_CLR_RTS:
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_CLR_RTS called\n");

		// clear RTS bit
		deviceContext->FlowControlState &= ~SERIAL_RTS_STATE;
		status = STATUS_SUCCESS;

		break;
	}

	case  IOCTL_SERIAL_GET_DTRRTS: // status of DTR and RTS as bit wise or ULONG
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_GET_DTRRTS called\n");

		// write status value to request
		status = CopyToRequest(requestHandle, &deviceContext->FlowControlState, sizeof(ULONG));

		break;
	}

	case IOCTL_SERIAL_RESET_DEVICE: // resets all buffers
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_RESET_DEVICE called\n");

		status = STATUS_SUCCESS;

		break;
	}

		/* controls the serial port from the application (access to internal buffers) */

	case IOCTL_APPLINK_SET_BUFFER_SIZES:
	{

		dbgprintf("[i] VCOM IOCTL_APPLINK_SET_BUFFER_SIZES called\n");

		// read and apply new buffer size configuration
		status = CopyFromRequest(requestHandle, &bufferContext->BufferSizes, sizeof(BUFFER_SIZES));
		if (status == STATUS_SUCCESS) {
			status = ReallocateBuffers(bufferContext);
		}

		break;
	}
	case IOCTL_APPLINK_GET_BUFFER_SIZES:
	{

		dbgprintf("[i] VCOM IOCTL_APPLINK_GET_BUFFER_SIZES called\n");

		// write buffer sizes to request
		status = CopyToRequest(requestHandle, &bufferContext->BufferSizes, sizeof(BUFFER_SIZES));
		
		break;
	}

	case IOCTL_APPLINK_WRITE_BUFFER:
	{

		dbgprintf("[i] VCOM IOCTL_APPLINK_WRITE_BUFFER called\n");

		ULONG bytesWritten;

		// put data from request into buffer
		status = WriteReception(bufferContext, requestHandle, (ULONG) inputBufferLength, &bytesWritten);
		if (status == STATUS_SUCCESS) {
			WdfRequestCompleteWithInformation(requestHandle, STATUS_SUCCESS, bytesWritten);
			return;
		}

		break;
	}
	case IOCTL_APPLINK_READ_BUFFER:
	{

		dbgprintf("[i] VCOM IOCTL_APPLINK_READ_BUFFER called\n");

		ULONG bytesRead;

		// write data from buffer to request
		status = ReadTransmittion(bufferContext, requestHandle, (ULONG) outputBufferLength, &bytesRead);
		if (status == STATUS_SUCCESS) {
			WdfRequestCompleteWithInformation(requestHandle, STATUS_SUCCESS, bytesRead);
			return;
		}

		break;
	}

		/* not supported control codes (these mostly don't make much sense for an virtual port) */

	case IOCTL_SERIAL_SET_QUEUE_SIZE:
	case IOCTL_SERIAL_GET_HANDFLOW:
	case IOCTL_SERIAL_SET_HANDFLOW:
	case IOCTL_SERIAL_SET_BREAK_ON:
	case IOCTL_SERIAL_SET_BREAK_OFF:
	case IOCTL_SERIAL_XOFF_COUNTER:
	case IOCTL_SERIAL_IMMEDIATE_CHAR:
	case IOCTL_SERIAL_PURGE:
	case IOCTL_SERIAL_GET_COMMSTATUS:
	case IOCTL_SERIAL_GET_PROPERTIES:
	case IOCTL_SERIAL_SET_XOFF:
	case IOCTL_SERIAL_SET_XON:
	case IOCTL_SERIAL_GET_MODEMSTATUS:
	case IOCTL_SERIAL_SET_MODEM_CONTROL:
	case IOCTL_SERIAL_GET_MODEM_CONTROL:
	case IOCTL_SERIAL_SET_FIFO_CONTROL:
		status = STATUS_SUCCESS; // we still return SUCCESS, since the system expects these to be supported
		break;

	default:
		status = STATUS_INVALID_PARAMETER; // everything else is not expected for an serial port to work, return INVALID error
		break;

	}

	dbgprintf("[i] VCOM IODeviceControl completed: NTSTATUS: 0x%x\n", status);

	WdfRequestComplete(requestHandle, status);

}

void IORead(WDFQUEUE queueHandle, WDFREQUEST requestHandle, size_t length)
{

	//dbgprintf("[i] VCOM IORead called: buflen: %u\n", length);

	NTSTATUS status;
	QUEUE_CONTEXT* queueContext = GetQueueContext(queueHandle);
	DEVICE_CONTEXT* deviceContext = queueContext->DeviceContext;
	BUFFER_CONTEXT* bufferContext = &deviceContext->BufferContext;
	ULONG bytesRead;

	status = ReadReception(bufferContext, requestHandle, (ULONG) length, &bytesRead);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM IORead:ReadReception failed: NTSTATUS 0x%x\n", status);
		WdfRequestComplete(requestHandle, status);
		return;
	}

	WdfRequestCompleteWithInformation(requestHandle, status, bytesRead);




//	QueueContext* queueContext = GetQueueContext(queueHandle);

	// no data to read yet, queue for later
//	status = WdfRequestForwardToIoQueue(requestHandle, queueContext->ReadQueue);
//	if (status != STATUS_SUCCESS) {
//		dbgerrprintf("[!] VCOM WdfRequestForwardToIoQueue failed: NTSTATUS 0x%x\n", status);
//		WdfRequestComplete(requestHandle, status);
//	}

	// return F every time
//	char buffer[256];
//	size_t toCopy = min(256, length);
//	for (size_t i = 0; i < toCopy; i++)
//		buffer[i] = 'F';
//
//	status = CopyToRequest(requestHandle, buffer, toCopy);
//	if (status != STATUS_SUCCESS) {
//		dbgerrprintf("[!] VCOM IOWrite:CopyToRequest failed: NTSTATUS 0x%x\n", status);
//		WdfRequestComplete(requestHandle, status);
//		return;
//	}

	//WdfRequestCompleteWithInformation(requestHandle, status, toCopy);

}

void IOWrite(WDFQUEUE queueHandle, WDFREQUEST requestHandle, size_t length)
{

	dbgprintf("[i] VCOM IOWrite called: buflen: %u\n", length);

	NTSTATUS status;
	QUEUE_CONTEXT* queueContext = GetQueueContext(queueHandle);
	DEVICE_CONTEXT* deviceContext = queueContext->DeviceContext;
	BUFFER_CONTEXT* bufferContext = &deviceContext->BufferContext;
	ULONG bytesWritten;

	status = WriteTransmittion(bufferContext, requestHandle, (ULONG) length, &bytesWritten);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM IOWrite:WriteTransmittion failed: NTSTATUS 0x%x\n", status);
		WdfRequestComplete(requestHandle, status);
		return;
	}

	WdfRequestCompleteWithInformation(requestHandle, status, bytesWritten);

//	size_t toCopy = min(256, length);
//	char buffer[256] = { 0 };
//	CopyFromRequest(requestHandle, buffer, toCopy);
	//
//	dbgprintf("[i] >>> %.*s\n", toCopy, buffer);
	
	// nothing accepts data yet, just complete request
	//WdfRequestCompleteWithInformation(requestHandle, STATUS_SUCCESS, toCopy);

}
