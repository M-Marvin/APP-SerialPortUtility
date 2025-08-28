
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
	WDFQUEUE writeQueueHandle;
	WDFQUEUE waitMaskQueueHandle;
	WDFQUEUE waitChangeQueueHandle;
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

	// configure write IO queue
	WDF_IO_QUEUE_CONFIG_INIT(&config, WdfIoQueueDispatchManual);

	// create write IO queue
	status = WdfIoQueueCreate(deviceHandle, &config, WDF_NO_OBJECT_ATTRIBUTES, &writeQueueHandle);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM WdfIoQueueCreate for write queue failed: NTSTATUS 0x%x\n", status);
		return status;
	}

	context->WriteQueue = writeQueueHandle;

	// configure wait IO queue
	WDF_IO_QUEUE_CONFIG_INIT(&config, WdfIoQueueDispatchManual);

	// create wait IO queue
	status = WdfIoQueueCreate(deviceHandle, &config, WDF_NO_OBJECT_ATTRIBUTES, &waitMaskQueueHandle);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM WdfIoQueueCreate for wait mask queue failed: NTSTATUS 0x%x\n", status);
		return status;
	}

	context->WaitMaskQueue = waitMaskQueueHandle;

	// configure wait IO queue for applink ioctl
	WDF_IO_QUEUE_CONFIG_INIT(&config, WdfIoQueueDispatchManual);

	// create wait IO queue for applink ioctl
	status = WdfIoQueueCreate(deviceHandle, &config, WDF_NO_OBJECT_ATTRIBUTES, &waitChangeQueueHandle);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM WdfIoQueueCreate for wait change queue failed: NTSTATUS 0x%x\n", status);
		return status;
	}

	context->WaitChangeQueue = waitChangeQueueHandle;

	dbgprintf("[i] VCOM CreateIOQueue completed\n");

	return STATUS_SUCCESS;

}

NTSTATUS CopyFromRequest(WDFREQUEST requestHandle, ULONG requestBufferOffset, void* buffer, size_t bytesToCopy)
{

	NTSTATUS status;
	WDFMEMORY memoryHandle;

	status = WdfRequestRetrieveInputMemory(requestHandle, &memoryHandle);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM WdfRequestRetrieveInputMemory failed: NTSTATUS 0x%x\n", status);
		return status;
	}

	status = WdfMemoryCopyToBuffer(memoryHandle, requestBufferOffset, buffer, bytesToCopy);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM WdfMemoryCopyToBuffer failed: NTSTATUS 0x%x\n", status);
		return status;
	}

	return STATUS_SUCCESS;

}

NTSTATUS CopyToRequest(WDFREQUEST requestHandle, ULONG requestBufferOffset, void* buffer, size_t bytesToCopy)
{

	NTSTATUS status;
	WDFMEMORY memoryHandle;

	status = WdfRequestRetrieveOutputMemory(requestHandle, &memoryHandle);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM WdfRequestRetrieveOutputMemory failed: NTSTATUS 0x%x\n", status);
		return status;
	}

	status = WdfMemoryCopyFromBuffer(memoryHandle, requestBufferOffset, buffer, bytesToCopy);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM WdfMemoryCopyFromBuffer failed: NTSTATUS 0x%x\n", status);
		return status;
	}

	return STATUS_SUCCESS;

}

static void ReInvokeRequests(QUEUE_CONTEXT* queueContext, WDFQUEUE queue) {

	NTSTATUS status;
	WDFREQUEST request;

	for (; ; ) {

		status = WdfIoQueueRetrieveNextRequest(queue, &request);

		if (status != STATUS_SUCCESS) break;

		status = WdfRequestForwardToIoQueue(request, queueContext->Queue);
		if (status != STATUS_SUCCESS) {
			dbgerrprintf("[!] VCOM ReInvokeRequests:WdfRequestForwardToIoQueue failed: NTSTATUS 0x%x\n", status);
			WdfRequestComplete(request, status);
		}

	}

}

static void TriggerMaskWait(QUEUE_CONTEXT* queueContext, ULONG triggerMask)
{
	if ((queueContext->WaitMask & triggerMask) == 0) return;

	WDFREQUEST request;
	NTSTATUS status = WdfIoQueueRetrieveNextRequest(queueContext->WaitMaskQueue, &request);
	if (status == STATUS_SUCCESS) {
		WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, triggerMask);
	}

}

static void TriggerChangeWait(QUEUE_CONTEXT* queueContext, ULONG triggerMask)
{
	if ((queueContext->ChangeMask & triggerMask) == 0) return;

	WDFREQUEST request;
	NTSTATUS status;
	do {
		status = WdfIoQueueRetrieveNextRequest(queueContext->WaitChangeQueue, &request);
		if (status == STATUS_SUCCESS) {

			dbgprintf("[i] VCOM IOCTL_APPLINK_WAIT_FOR_CHANGE complete wait: %x\n", triggerMask);

			NTSTATUS status2 = CopyToRequest(request, 0, &triggerMask, sizeof(ULONG));
			if (status2 != STATUS_SUCCESS) {
				dbgprintf("[!] VCOM TriggerChangeWait failed: NTSTATUS 0x%x\n", status2);
				WdfRequestComplete(request, status2);
			}

			WdfRequestCompleteWithInformation(request, STATUS_SUCCESS, sizeof(ULONG));
		}
	} while (status == STATUS_SUCCESS);

}

void IODeviceControl(WDFQUEUE queueHandle, WDFREQUEST requestHandle, size_t outputBufferLength, size_t inputBufferLength, ULONG controlCode)
{

	UNREFERENCED_PARAMETER(inputBufferLength);
	UNREFERENCED_PARAMETER(outputBufferLength);

	NTSTATUS status;
	QUEUE_CONTEXT* queueContext = GetQueueContext(queueHandle);
	DEVICE_CONTEXT* deviceContext = queueContext->DeviceContext;
	BUFFER_CONTEXT* bufferContext = &deviceContext->BufferContext;
	REQUEST_CONTEXT* requestContext = GetRequestContext(requestHandle);
	ULONG bytesReturned = 0;

	switch (controlCode) {

		/* port configuration control (set baud, timeouts, flow control, etc ) */

	case IOCTL_SERIAL_SET_BAUD_RATE: // the baud rate of the port as ULONG inside a struct
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_SET_BAUD_RATE called\n");

		SERIAL_BAUD_RATE baudRateStruct = { 0 };

		// read baud rate and apply to context
		status = CopyFromRequest(requestHandle, 0, &baudRateStruct, sizeof(SERIAL_BAUD_RATE));
		if (status == STATUS_SUCCESS) {
			deviceContext->BaudRate = baudRateStruct.BaudRate;
		}

		TriggerChangeWait(queueContext, APPLINK_EVENT_CONFIG);

		break;
	}
	case IOCTL_SERIAL_GET_BAUD_RATE:
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_GET_BAUD_RATE called\n");

		SERIAL_BAUD_RATE baudRateStruct = {
			.BaudRate = deviceContext->BaudRate
		};

		// write baud rate to request
		status = CopyToRequest(requestHandle, 0, &baudRateStruct, bytesReturned = sizeof(SERIAL_BAUD_RATE));
		
		break;
	}


	case IOCTL_SERIAL_GET_TIMEOUTS: // transmission timeout for read and write as an struct
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_GET_TIMEOUTS called\n");

		// write timeouts to request
		status = CopyToRequest(requestHandle, 0, &deviceContext->Timeouts, bytesReturned = sizeof(SERIAL_TIMEOUTS));

		break;
	}
	case IOCTL_SERIAL_SET_TIMEOUTS:
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_SET_TIMEOUTS called\n");

		// read timeouts and apply to context
		status = CopyFromRequest(requestHandle, 0, &deviceContext->Timeouts, sizeof(SERIAL_TIMEOUTS));

		TriggerChangeWait(queueContext, APPLINK_EVENT_TIMEOUTS);

		break;
	}

	case IOCTL_SERIAL_GET_LINE_CONTROL: // number of data and stop bits and parity configuration as an struct 
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_GET_LINE_CONTROL called\n");

		// write line control to request
		status = CopyToRequest(requestHandle, 0, &deviceContext->LineControl, bytesReturned = sizeof(SERIAL_LINE_CONTROL));

		break;
	}
	case IOCTL_SERIAL_SET_LINE_CONTROL:
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_SET_LINE_CONTROL called\n");

		// read líne control and apply to context
		status = CopyFromRequest(requestHandle, 0, &deviceContext->LineControl, sizeof(SERIAL_LINE_CONTROL));

		TriggerChangeWait(queueContext, APPLINK_EVENT_CONFIG);

		break;
	}

	case IOCTL_SERIAL_GET_HANDFLOW: // flow control configuration as an struct 
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_GET_HANDFLOW called\n");

		// write line control to request
		status = CopyToRequest(requestHandle, 0, &deviceContext->FlowControl, bytesReturned = sizeof(SERIAL_HANDFLOW));

		break;
	}
	case IOCTL_SERIAL_SET_HANDFLOW:
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_SET_HANDFLOW called\n");

		// read líne control and apply to context
		status = CopyFromRequest(requestHandle, 0, &deviceContext->FlowControl, sizeof(SERIAL_HANDFLOW));

		TriggerChangeWait(queueContext, APPLINK_EVENT_CONFIG);

		break;
	}
	
	case IOCTL_SERIAL_GET_CHARS: // the characters used for XON/XOFF flow control in an struct
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_GET_CHARS called\n");

		// write XON/XOFF chars to request
		status = CopyToRequest(requestHandle, 0, &deviceContext->FlowChars, bytesReturned = sizeof(SERIAL_CHARS));

		break;
	}

	case IOCTL_SERIAL_SET_CHARS:
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_SET_CHARS called\n");

		// read XON/XOFF chars and apply to context
		status = CopyFromRequest(requestHandle, 0, &deviceContext->FlowChars, sizeof(SERIAL_CHARS));

		TriggerChangeWait(queueContext, APPLINK_EVENT_CONFIG);

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
		status = CopyFromRequest(requestHandle, 0, &queueContext->WaitMask, sizeof(ULONG));

		WDFREQUEST previousWaitRequest;

		// abort pendinjg wait request, if there is one
		status = WdfIoQueueRetrieveNextRequest(queueContext->WaitMaskQueue, &previousWaitRequest);
		if (status == STATUS_SUCCESS) {

			// zero indicates that no event was actualy triggered, but the wait ended because of this set mask request
			ULONG eventMask = 0;
			status = CopyToRequest(previousWaitRequest, 0, &eventMask, bytesReturned = sizeof(eventMask));

			WdfRequestComplete(previousWaitRequest, STATUS_SUCCESS);

		}

		status = STATUS_SUCCESS;
		break;
	}
	case IOCTL_SERIAL_GET_WAIT_MASK:
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_GET_WAIT_MASK called\n");

		// write wait mask to request
		status = CopyToRequest(requestHandle, 0, &queueContext->WaitMask, bytesReturned = sizeof(ULONG));

		break;
	}

	case  IOCTL_SERIAL_SET_DTR: // set the DTR line if not controled by autmatic flow control
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_SET_DTR called\n");

		// set DTR bit
		deviceContext->FlowControlState |= SERIAL_DTR_STATE;
		status = STATUS_SUCCESS;

		TriggerChangeWait(queueContext, APPLINK_EVENT_COMSTATE);

		break;
	}
	case  IOCTL_SERIAL_CLR_DTR:
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_CLR_DTR called\n");

		// clear DTR bit
		deviceContext->FlowControlState &= ~SERIAL_DTR_STATE;
		status = STATUS_SUCCESS;

		TriggerChangeWait(queueContext, APPLINK_EVENT_COMSTATE);

		break;
	}

	case  IOCTL_SERIAL_SET_RTS: // set the RTS line if not controled by autmatic flow control
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_SET_RTS called\n");

		// set RTS bit
		deviceContext->FlowControlState |= SERIAL_RTS_STATE;
		status = STATUS_SUCCESS;

		TriggerChangeWait(queueContext, APPLINK_EVENT_COMSTATE);

		break;
	}
	case  IOCTL_SERIAL_CLR_RTS:
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_CLR_RTS called\n");

		// clear RTS bit
		deviceContext->FlowControlState &= ~SERIAL_RTS_STATE;
		status = STATUS_SUCCESS;

		TriggerChangeWait(queueContext, APPLINK_EVENT_COMSTATE);

		break;
	}

	case  IOCTL_SERIAL_GET_DTRRTS: // status of DTR and RTS as bit wise or ULONG
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_GET_DTRRTS called\n");

		// write status value to request
		ULONG dtrrtsState = deviceContext->FlowControlState & (SERIAL_RTS_STATE | SERIAL_DTR_STATE);
		status = CopyToRequest(requestHandle, 0, &dtrrtsState, bytesReturned = sizeof(ULONG));

		break;
	}
	case IOCTL_SERIAL_GET_MODEMSTATUS: // alternate way of reading DTR and RTS signals plus all the other signals
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_GET_DTRRTS called\n");

		// write status value to request
		status = CopyToRequest(requestHandle, 0, &deviceContext->FlowControlState, bytesReturned = sizeof(ULONG));

		break;
	}

	case IOCTL_SERIAL_RESET_DEVICE: // resets all buffers
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_RESET_DEVICE called\n");

		// reset buffers
		bufferContext->ReceiveReadPtr = bufferContext->ReceiveWritePtr = 0;
		bufferContext->TransmitReadPtr = bufferContext->TransmitWritePtr = 0;
		status = STATUS_SUCCESS;

		break;
	}

		/* controls the serial port from the application (access to internal buffers) */

	case IOCTL_APPLINK_GET_COMSTATUS:
	{

		dbgprintf("[i] VCOM IOCTL_APPLINK_GET_COMSTATUS called\n");

		// write status value to request
		status = CopyToRequest(requestHandle, 0, &deviceContext->FlowControlState, bytesReturned = sizeof(ULONG));

		break;

	}
	case IOCTL_APPLINK_SET_COMSTATUS:
	{

		dbgprintf("[i] VCOM IOCTL_APPLINK_SET_COMSTATUS called\n");

		// write status value to request
		ULONG state = deviceContext->FlowControlState;
		status = CopyToRequest(requestHandle, 0, &deviceContext->FlowControlState, bytesReturned = sizeof(ULONG));

		// notify wait requests about changed
		state ^= deviceContext->FlowControlState;
		ULONG mask = 0;
		if (state & SERIAL_CTS_STATE) mask |= SERIAL_EV_CTS;
		if (state & SERIAL_DSR_STATE) mask |= SERIAL_EV_DSR;
		if (state & SERIAL_RI_STATE) mask |= SERIAL_EV_RING;
		
		TriggerMaskWait(queueContext, mask);
		
		break;
	}

	case IOCTL_APPLINK_GET_BAUD:
	{

		dbgprintf("[i] VCOM IOCTL_APPLINK_GET_BAUD called\n");

		SERIAL_BAUD_RATE baudRateStruct = {
			.BaudRate = deviceContext->BaudRate
		};

		// write baud rate to request
		status = CopyToRequest(requestHandle, 0, &baudRateStruct, bytesReturned = sizeof(SERIAL_BAUD_RATE));

		break;
	}

	case IOCTL_APPLINK_GET_TIMEOUTS: // transmission timeout for read and write as an struct
	{

		dbgprintf("[i] VCOM IOCTL_APPLINK_GET_TIMEOUTS called\n");

		// write timeouts to request
		status = CopyToRequest(requestHandle, 0, &deviceContext->Timeouts, bytesReturned = sizeof(SERIAL_TIMEOUTS));

		break;
	}

	case IOCTL_APPLINK_GET_LINE_CONTROL: // number of data and stop bits and parity configuration as an struct 
	{

		dbgprintf("[i] VCOM IOCTL_APPLINK_GET_LINE_CONTROL called\n");

		// write line control to request
		status = CopyToRequest(requestHandle, 0, &deviceContext->LineControl, bytesReturned = sizeof(SERIAL_LINE_CONTROL));

		break;
	}

	case IOCTL_APPLINK_GET_FLOW_CONTROL: // number of data and stop bits and parity configuration as an struct 
	{

		dbgprintf("[i] VCOM IOCTL_APPLINK_GET_FLOW_CONTROL called\n");

		// write line control to request
		status = CopyToRequest(requestHandle, 0, &deviceContext->FlowControl, bytesReturned = sizeof(SERIAL_HANDFLOW));

		break;
	}

	case IOCTL_APPLINK_GET_CHARS: // the characters used for XON/XOFF flow control in an struct
	{

		dbgprintf("[i] VCOM IOCTL_APPLINK_GET_CHARS called\n");

		// write XON/XOFF chars to request
		status = CopyToRequest(requestHandle, 0, &deviceContext->FlowChars, bytesReturned = sizeof(SERIAL_CHARS));

		break;
	}

	case IOCTL_APPLINK_WAIT_FOR_CHANGE:
	{

		dbgprintf("[i] VCOM IOCTL_APPLINK_WAIT_FOR_CHANGE called\n");

		// TODO this causes the current request to be completed with the same error code for some reason
		//WDFREQUEST previousWaitRequest;
		// abort the previous wait request, if there is one
		//status = WdfIoQueueRetrieveNextRequest(queueContext->WaitChangeQueue, &previousWaitRequest);
		//if (status == STATUS_SUCCESS) {
		//	dbgprintf("[i] VCOM IOCTL_APPLINK_WAIT_FOR_CHANGE abort previous wait\n");
		//	WdfRequestComplete(previousWaitRequest, STATUS_UNSUCCESSFUL);
		//}

		// read the event mask
		status = CopyFromRequest(requestHandle, 0, &queueContext->ChangeMask, sizeof(ULONG));
		if (status != STATUS_SUCCESS) {
			dbgerrprintf("[!] VCOM CopyFromRequest failed: NTSTATUS 0x%x\n", status);
			break;
		}

		// put the new request into the wait queue
		status = WdfRequestForwardToIoQueue(requestHandle, queueContext->WaitChangeQueue);
		if (status != STATUS_SUCCESS) {
			dbgerrprintf("[!] VCOM WdfRequestForwardToIoQueue failed: NTSTATUS 0x%x\n", status);
			break;
		}


		dbgprintf("[i] VCOM IOCTL_APPLINK_WAIT_FOR_CHANGE pending\n");
		return; // do not continue, don't complete the request

	}

	case IOCTL_APPLINK_SET_BUFFER_SIZES:
	{

		dbgprintf("[i] VCOM IOCTL_APPLINK_SET_BUFFER_SIZES called\n");

		// read and apply new buffer size configuration
		status = CopyFromRequest(requestHandle, 0, &bufferContext->BufferSizes, sizeof(BUFFER_SIZES));
		if (status == STATUS_SUCCESS) {
			status = ReallocateBuffers(bufferContext);
		}

		break;
	}
	case IOCTL_APPLINK_GET_BUFFER_SIZES:
	{

		dbgprintf("[i] VCOM IOCTL_APPLINK_GET_BUFFER_SIZES called\n");

		// write buffer sizes to request
		status = CopyToRequest(requestHandle, 0, &bufferContext->BufferSizes, bytesReturned = sizeof(BUFFER_SIZES));
		
		break;
	}

	case IOCTL_APPLINK_WRITE_BUFFER:
	{

		dbgprintf("[i] VCOM IOCTL_APPLINK_WRITE_BUFFER called\n");

		ULONG bytesWritten;
		ULONG bufferContent;

		// attempt to write the full buffer length to the reception buffer
		status = WriteReception(bufferContext, requestHandle, (ULONG) inputBufferLength, &bytesWritten, &bufferContent);
		if (status != STATUS_SUCCESS) {
			dbgerrprintf("[!] VCOM IOCTL_APPLINK_WRITE_BUFFER:WriteReception failed: NTSTATUS 0x%x\n", status);
			break;
		}

		// update the number of bytes transfered in the context
		requestContext->ReadWrite.BytesTransfered += bytesWritten;

		// complete or re-queue request if not all bytes have ben written yet
		if (requestContext->ReadWrite.BytesTransfered < inputBufferLength) {
			WdfRequestForwardToIoQueue(requestHandle, queueContext->WriteQueue);
		}
		else {
			WdfRequestCompleteWithInformation(requestHandle, STATUS_SUCCESS, requestContext->ReadWrite.BytesTransfered);
		}

		// invoke pending read requests, let them attempt to use the new data in the buffer
		ReInvokeRequests(queueContext, queueContext->ReadQueue);

		TriggerMaskWait(queueContext, SERIAL_EV_RXCHAR | (bufferContent > (bufferContext->BufferSizes.ReceiveSize * 0.8) ? SERIAL_EV_RX80FULL : 0));

		return;
	}
	case IOCTL_APPLINK_READ_BUFFER:
	{

		dbgprintf("[i] VCOM IOCTL_APPLINK_READ_BUFFER called\n");

		ULONG bytesRead;
		ULONG bufferContent;

		// attempt to read the full buffer length from the transmission buffer
		status = ReadTransmittion(bufferContext, requestHandle, (ULONG) outputBufferLength, &bytesRead, &bufferContent);
		if (status != STATUS_SUCCESS) {
			dbgerrprintf("[!] VCOM IOCTL_APPLINK_READ_BUFFER:ReadTransmittion failed: NTSTATUS 0x%x\n", status);
			break;
		}

		// update the number of bytes transfered in the context
		requestContext->ReadWrite.BytesTransfered += bytesRead;

		// complete or re-queue request if not all bytes have ben read yet
		if (requestContext->ReadWrite.BytesTransfered < inputBufferLength) {
			WdfRequestForwardToIoQueue(requestHandle, queueContext->WriteQueue);
		}
		else {
			WdfRequestCompleteWithInformation(requestHandle, STATUS_SUCCESS, requestContext->ReadWrite.BytesTransfered);
		}

		// invoke pending write requests, let them attempt to use the freed space in the buffer
		ReInvokeRequests(queueContext, queueContext->WriteQueue);

		if (bufferContent == 0) {
			TriggerMaskWait(queueContext, SERIAL_EV_TXEMPTY);
		}

		return;
	}

		/* not supported control codes (these mostly don't make much sense for an virtual port) */

	case IOCTL_SERIAL_SET_QUEUE_SIZE:
	case IOCTL_SERIAL_SET_XON:
	case IOCTL_SERIAL_SET_XOFF:

		dbgprintf("[i] VCOM IODeviceControl called: not implemented IOCTL_CODE: 0x%x\n", controlCode);

		status = STATUS_SUCCESS;
		break;
	
	case IOCTL_SERIAL_SET_BREAK_ON:
	case IOCTL_SERIAL_SET_BREAK_OFF:
	case IOCTL_SERIAL_XOFF_COUNTER:
	case IOCTL_SERIAL_IMMEDIATE_CHAR:
	case IOCTL_SERIAL_PURGE:
	case IOCTL_SERIAL_GET_PROPERTIES:
	case IOCTL_SERIAL_SET_MODEM_CONTROL:
	case IOCTL_SERIAL_GET_MODEM_CONTROL:
	case IOCTL_SERIAL_SET_FIFO_CONTROL:
	case IOCTL_SERIAL_GET_COMMSTATUS:

		dbgprintf("[i] VCOM IODeviceControl called: not supported IOCTL_CODE: 0x%x\n", controlCode);

		status = STATUS_NOT_CAPABLE;
		break;

	default:

		dbgprintf("[i] VCOM IODeviceControl called: invalid IOCTL_CODE: 0x%x\n", controlCode);

		status = STATUS_INVALID_PARAMETER;
		break;

	}

	WdfRequestCompleteWithInformation(requestHandle, status, bytesReturned);

}

void IORead(WDFQUEUE queueHandle, WDFREQUEST requestHandle, size_t length)
{

	dbgprintf("[i] VCOM IORead called: buflen: %u\n", length);

	NTSTATUS status;
	QUEUE_CONTEXT* queueContext = GetQueueContext(queueHandle);
	DEVICE_CONTEXT* deviceContext = queueContext->DeviceContext;
	BUFFER_CONTEXT* bufferContext = &deviceContext->BufferContext;
	REQUEST_CONTEXT* requestContext = GetRequestContext(requestHandle);
	ULONG bytesRead;
	ULONG bufferContent;

	// attempt to read the full buffer length from the reception buffer
	status = ReadReception(bufferContext, requestHandle, (ULONG) length, &bytesRead, &bufferContent);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM IORead:ReadReception failed: NTSTATUS 0x%x\n", status);
		WdfRequestComplete(requestHandle, status);
		return;
	}

	// update the number of bytes transfered in the context
	requestContext->ReadWrite.BytesTransfered += bytesRead;

	if (bufferContent == 0)
		TriggerChangeWait(queueContext, APPLINK_EVENT_TXEMPTY);

	// complete or re-queue request if not all bytes have ben read yet
	if (requestContext->ReadWrite.BytesTransfered < length) {
		WdfRequestForwardToIoQueue(requestHandle, queueContext->ReadQueue);
	}
	else {
		WdfRequestCompleteWithInformation(requestHandle, status, requestContext->ReadWrite.BytesTransfered);
	}

	// invoke pending write requests, let them attempt to use the freed space in the buffer
	ReInvokeRequests(queueContext, queueContext->WriteQueue);

}

void IOWrite(WDFQUEUE queueHandle, WDFREQUEST requestHandle, size_t length)
{

	dbgprintf("[i] VCOM IOWrite called: buflen: %llu\n", length);

	NTSTATUS status;
	QUEUE_CONTEXT* queueContext = GetQueueContext(queueHandle);
	DEVICE_CONTEXT* deviceContext = queueContext->DeviceContext;
	BUFFER_CONTEXT* bufferContext = &deviceContext->BufferContext;
	REQUEST_CONTEXT* requestContext = GetRequestContext(requestHandle);
	ULONG bytesWritten;
	ULONG bufferContent;

	// attempt to write the full buffer length to the transmission buffer
	status = WriteTransmittion(bufferContext, requestHandle, (ULONG) length, &bytesWritten, &bufferContent);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM IOWrite:WriteTransmittion failed: NTSTATUS 0x%x\n", status);
		WdfRequestComplete(requestHandle, status);
		return;
	}

	// update the number of bytes transfered in the context
	requestContext->ReadWrite.BytesTransfered += bytesWritten;

	if (bufferContent > 0)
		TriggerChangeWait(queueContext, APPLINK_EVENT_RXCHAR);

	// complete or re-queue request if not all bytes have ben written yet
	if (requestContext->ReadWrite.BytesTransfered < length) {
		WdfRequestForwardToIoQueue(requestHandle, queueContext->WriteQueue);
	}
	else {
		WdfRequestCompleteWithInformation(requestHandle, status, requestContext->ReadWrite.BytesTransfered);
	}

	// invoke pending read requests, let them attempt to use the new data in the buffer
	ReInvokeRequests(queueContext, queueContext->ReadQueue);

}
