/* Implements IOCTL handling for the application side of the port device
*/

#include "vcomport.h"
#include "serial.h"
#include "dbgprint.h"

void PortDeviceIOCTLApp(WDFQUEUE queueHandle, WDFREQUEST requestHandle, SIZE_T outputBufferLength, SIZE_T inputBufferLength, ULONG controlCode) {

	NTSTATUS status;
	QUEUE_CONTEXT* queueContext = GetQueueContext(queueHandle);
	DEVICE_CONTEXT* deviceContext = queueContext->DeviceContext;
	ULONG bytesReturned = 0;

	switch (controlCode) {

		/* controls the serial port from the application (access to internal buffers) */

	case IOCTL_APPLINK_GET_COMSTATUS:
	{

		dbgprintf("[i] VCOM IOCTL_APPLINK_GET_COMSTATUS called\n");

		// write status value to request
		status = PortDeviceCopyToRequest(requestHandle, 0, &deviceContext->FlowControlState, bytesReturned = sizeof(ULONG));

		break;

	}
	case IOCTL_APPLINK_SET_COMSTATUS:
	{

		dbgprintf("[i] VCOM IOCTL_APPLINK_SET_COMSTATUS called\n");

		// write status value to request
		ULONG state = deviceContext->FlowControlState;
		status = PortDeviceCopyToRequest(requestHandle, 0, &deviceContext->FlowControlState, bytesReturned = sizeof(ULONG));

		// notify wait requests about changed
		state ^= deviceContext->FlowControlState;
		ULONG mask = 0;
		if (state & SERIAL_CTS_STATE) mask |= SERIAL_EV_CTS;
		if (state & SERIAL_DSR_STATE) mask |= SERIAL_EV_DSR;
		if (state & SERIAL_RI_STATE) mask |= SERIAL_EV_RING;

		PortDeviceTriggerSerialWait(queueContext, mask);

		break;
	}

	case IOCTL_APPLINK_GET_BAUD:
	{

		dbgprintf("[i] VCOM IOCTL_APPLINK_GET_BAUD called\n");

		SERIAL_BAUD_RATE baudRateStruct = {
			.BaudRate = deviceContext->BaudRate
		};

		// write baud rate to request
		status = PortDeviceCopyToRequest(requestHandle, 0, &baudRateStruct, bytesReturned = sizeof(SERIAL_BAUD_RATE));

		break;
	}

	case IOCTL_APPLINK_GET_TIMEOUTS: // app io timeout for read and write as an struct
	{

		dbgprintf("[i] VCOM IOCTL_APPLINK_GET_TIMEOUTS called\n");

		// write timeouts to request
		status = PortDeviceCopyToRequest(requestHandle, 0, &deviceContext->AppTimeouts, bytesReturned = sizeof(SERIAL_TIMEOUTS));

		break;
	}

	case IOCTL_APPLINK_SET_TIMEOUTS:
	{

		dbgprintf("[i] VCOM IOCTL_APPLINK_SET_TIMEOUTS called\n");

		// read timeouts and apply to context
		status = PortDeviceCopyFromRequest(requestHandle, 0, &deviceContext->AppTimeouts, sizeof(SERIAL_TIMEOUTS));

		break;
	}

	case IOCTL_APPLINK_GET_LINE_CONTROL: // number of data and stop bits and parity configuration as an struct 
	{

		dbgprintf("[i] VCOM IOCTL_APPLINK_GET_LINE_CONTROL called\n");

		// write line control to request
		status = PortDeviceCopyToRequest(requestHandle, 0, &deviceContext->LineControl, bytesReturned = sizeof(SERIAL_LINE_CONTROL));

		break;
	}

	case IOCTL_APPLINK_GET_FLOW_CONTROL: // number of data and stop bits and parity configuration as an struct 
	{

		dbgprintf("[i] VCOM IOCTL_APPLINK_GET_FLOW_CONTROL called\n");

		// write line control to request
		status = PortDeviceCopyToRequest(requestHandle, 0, &deviceContext->FlowControl, bytesReturned = sizeof(SERIAL_HANDFLOW));

		break;
	}

	case IOCTL_APPLINK_GET_CHARS: // the characters used for XON/XOFF flow control in an struct
	{

		dbgprintf("[i] VCOM IOCTL_APPLINK_GET_CHARS called\n");

		// write XON/XOFF chars to request
		status = PortDeviceCopyToRequest(requestHandle, 0, &deviceContext->FlowChars, bytesReturned = sizeof(SERIAL_CHARS));

		break;
	}

	case IOCTL_APPLINK_WAIT_ON_MASK:
	{

		dbgprintf("[i] VCOM IOCTL_APPLINK_WAIT_ON_MASK called\n");

		// abort the previous wait request, if there is one
		WDFREQUEST previousWaitRequest;
		status = WdfIoQueueRetrieveNextRequest(queueContext->AppWaitQueue, &previousWaitRequest);
		if (status == STATUS_SUCCESS) {
			dbgerrprintf("[!] VCOM WdfIoQueueRetrieveNextRequest failed: NTSTATUS 0x%x\n", status);
			WdfRequestComplete(previousWaitRequest, STATUS_UNSUCCESSFUL);
		}

		// if the mask is not set, return with unsuccesfull
		if (queueContext->DeviceContext->AppWaitMask == 0) {
			dbgprintf("[i] VCOM wait mask not set, abort wait\n");
			status = STATUS_UNSUCCESSFUL;
			break;
		}

		// put the new request into the wait queue
		status = WdfRequestForwardToIoQueue(requestHandle, queueContext->AppWaitQueue);
		if (status != STATUS_SUCCESS) {
			dbgerrprintf("[!] VCOM WdfRequestForwardToIoQueue failed: NTSTATUS 0x%x\n", status);
			break;
		}

		dbgprintf("[i] VCOM IOCTL_APPLINK_WAIT_ON_MASK pending\n");
		return; // do not continue, don't complete the request

	}

	case IOCTL_APPLINK_SET_WAIT_MASK:
	{

		dbgprintf("[i] VCOM IOCTL_APPLINK_SET_WAIT_MASK called\n");

		// read wait mask and apply to context
		status = PortDeviceCopyFromRequest(requestHandle, 0, &queueContext->DeviceContext->AppWaitMask, sizeof(ULONG));

		// abort pendinjg wait request, if there is one
		WDFREQUEST previousWaitRequest;
		status = WdfIoQueueRetrieveNextRequest(queueContext->AppWaitQueue, &previousWaitRequest);
		if (status == STATUS_SUCCESS) {

			// zero indicates that no event was actualy triggered, but the wait ended because of this set mask request
			ULONG eventMask = 0;
			status = PortDeviceCopyToRequest(previousWaitRequest, 0, &eventMask, bytesReturned = sizeof(eventMask));

			WdfRequestComplete(previousWaitRequest, STATUS_SUCCESS);

		}

		status = STATUS_SUCCESS;
		break;
	}

	case IOCTL_APPLINK_GET_WAIT_MASK:
	{

		dbgprintf("[i] VCOM IOCTL_APPLINK_GET_WAIT_MASK called\n");

		// write wait mask to request
		status = PortDeviceCopyToRequest(requestHandle, 0, &queueContext->DeviceContext->AppWaitMask, bytesReturned = sizeof(ULONG));

		break;
	}
	
	case IOCTL_APPLINK_SET_BUFFER_SIZE:
	{

		dbgprintf("[i] VCOM IOCTL_APPLINK_SET_BUFFER_SIZE called\n");

		// read and apply new buffer size configuration
		BufferSize bufferSize;
		status = PortDeviceCopyFromRequest(requestHandle, 0, &bufferSize, sizeof(BufferSize));
		if (status == STATUS_SUCCESS) {
			deviceContext->SerialTxBuffer.Capacity = bufferSize.TransmitSize;
			status = RingbufferReallocate(&deviceContext->SerialTxBuffer);
			if (status != STATUS_SUCCESS) break;
			deviceContext->SerialRxBuffer.Capacity = bufferSize.ReceiveSize;
			status = RingbufferReallocate(&deviceContext->SerialRxBuffer);
		}

		break;
	}

	case IOCTL_APPLINK_GET_BUFFER_SIZE:
	{

		dbgprintf("[i] VCOM IOCTL_APPLINK_GET_BUFFER_SIZE called\n");

		// write buffer sizes to request
		BufferSize bufferSize = {
			.ReceiveSize = deviceContext->SerialRxBuffer.Capacity,
			.TransmitSize = deviceContext->SerialTxBuffer.Capacity
		};
		status = PortDeviceCopyToRequest(requestHandle, 0, &bufferSize, bytesReturned = sizeof(BufferSize));

		break;
	}

	case IOCTL_APPLINK_WRITE_BUFFER:

		dbgprintf("[i] VCOM IOCTL_APPLINK_WRITE_BUFFER called\n");

		PortDeviceIOWriteApp(queueHandle, requestHandle, inputBufferLength);
		return;
	case IOCTL_APPLINK_READ_BUFFER:

		dbgprintf("[i] VCOM IOCTL_APPLINK_READ_BUFFER called\n");

		PortDeviceIOReadApp(queueHandle, requestHandle, outputBufferLength);
		return;

	default:

		dbgprintf("[i] VCOM PortDeviceIOCTLApp called: invalid IOCTL_CODE: 0x%x\n", controlCode);

		status = STATUS_INVALID_PARAMETER;
		break;

	}

	WdfRequestCompleteWithInformation(requestHandle, status, bytesReturned);

}
