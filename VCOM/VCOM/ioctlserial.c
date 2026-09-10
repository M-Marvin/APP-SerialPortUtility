/* Implements IOCTL handling for the serial port side of the port device
*/

#include "vcomport.h"
#include "serial.h"
#include "dbgprint.h"

void PortDeviceIOCTLSerial(WDFQUEUE queueHandle, WDFREQUEST requestHandle, SIZE_T outputBufferLength, SIZE_T inputBufferLength, ULONG controlCode) {

	UNREFERENCED_PARAMETER(inputBufferLength);
	UNREFERENCED_PARAMETER(outputBufferLength);

	NTSTATUS status;
	QUEUE_CONTEXT* queueContext = GetQueueContext(queueHandle);
	DEVICE_CONTEXT* deviceContext = queueContext->DeviceContext;
	ULONG bytesReturned = 0;

	switch (controlCode) {

		/* port configuration control (set baud, timeouts, flow control, etc ) */

	case IOCTL_SERIAL_SET_BAUD_RATE: // the baud rate of the port as ULONG inside a struct
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_SET_BAUD_RATE called\n");

		SERIAL_BAUD_RATE baudRateStruct = { 0 };

		// read baud rate and apply to context
		status = PortDeviceCopyFromRequest(requestHandle, 0, &baudRateStruct, sizeof(SERIAL_BAUD_RATE));
		if (status == STATUS_SUCCESS) {
			deviceContext->BaudRate = baudRateStruct.BaudRate;
		}

		PortDeviceTriggerAppWait(queueContext, APPLINK_EVENT_CONFIG);

		break;
	}
	case IOCTL_SERIAL_GET_BAUD_RATE:
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_GET_BAUD_RATE called\n");

		SERIAL_BAUD_RATE baudRateStruct = {
			.BaudRate = deviceContext->BaudRate
		};

		// write baud rate to request
		status = PortDeviceCopyToRequest(requestHandle, 0, &baudRateStruct, bytesReturned = sizeof(SERIAL_BAUD_RATE));

		break;
	}


	case IOCTL_SERIAL_GET_TIMEOUTS: // transmission timeout for read and write as an struct
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_GET_TIMEOUTS called\n");

		// write timeouts to request
		status = PortDeviceCopyToRequest(requestHandle, 0, &deviceContext->Timeouts, bytesReturned = sizeof(SERIAL_TIMEOUTS));

		break;
	}
	case IOCTL_SERIAL_SET_TIMEOUTS:
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_SET_TIMEOUTS called\n");

		// read timeouts and apply to context
		status = PortDeviceCopyFromRequest(requestHandle, 0, &deviceContext->Timeouts, sizeof(SERIAL_TIMEOUTS));

		break;
	}

	case IOCTL_SERIAL_GET_LINE_CONTROL: // number of data and stop bits and parity configuration as an struct 
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_GET_LINE_CONTROL called\n");

		// write line control to request
		status = PortDeviceCopyToRequest(requestHandle, 0, &deviceContext->LineControl, bytesReturned = sizeof(SERIAL_LINE_CONTROL));

		break;
	}
	case IOCTL_SERIAL_SET_LINE_CONTROL:
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_SET_LINE_CONTROL called\n");

		// read líne control and apply to context
		status = PortDeviceCopyFromRequest(requestHandle, 0, &deviceContext->LineControl, sizeof(SERIAL_LINE_CONTROL));

		PortDeviceTriggerAppWait(queueContext, APPLINK_EVENT_CONFIG);

		break;
	}

	case IOCTL_SERIAL_GET_HANDFLOW: // flow control configuration as an struct 
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_GET_HANDFLOW called\n");

		// write line control to request
		status = PortDeviceCopyToRequest(requestHandle, 0, &deviceContext->FlowControl, bytesReturned = sizeof(SERIAL_HANDFLOW));

		break;
	}
	case IOCTL_SERIAL_SET_HANDFLOW:
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_SET_HANDFLOW called\n");

		// read líne control and apply to context
		status = PortDeviceCopyFromRequest(requestHandle, 0, &deviceContext->FlowControl, sizeof(SERIAL_HANDFLOW));

		PortDeviceTriggerAppWait(queueContext, APPLINK_EVENT_CONFIG);

		break;
	}

	case IOCTL_SERIAL_GET_CHARS: // the characters used for XON/XOFF flow control in an struct
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_GET_CHARS called\n");

		// write XON/XOFF chars to request
		status = PortDeviceCopyToRequest(requestHandle, 0, &deviceContext->FlowChars, bytesReturned = sizeof(SERIAL_CHARS));

		break;
	}

	case IOCTL_SERIAL_SET_CHARS:
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_SET_CHARS called\n");

		// read XON/XOFF chars and apply to context
		status = PortDeviceCopyFromRequest(requestHandle, 0, &deviceContext->FlowChars, sizeof(SERIAL_CHARS));

		PortDeviceTriggerAppWait(queueContext, APPLINK_EVENT_CONFIG);

		break;
	}

	/* controls the serial port (flow control lines and buffers) */

	case IOCTL_SERIAL_WAIT_ON_MASK:
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_WAIT_ON_MASK called\n");

		// abort the previous wait request, if there is one
		WDFREQUEST previousWaitRequest;
		status = WdfIoQueueRetrieveNextRequest(queueContext->SerialWaitQueue, &previousWaitRequest);
		if (status == STATUS_SUCCESS) {
			dbgerrprintf("[!] VCOM CopyFromRequest failed: NTSTATUS 0x%x\n", status);
			WdfRequestComplete(previousWaitRequest, STATUS_UNSUCCESSFUL);
		}

		// if the mask is not set, return with unsuccesfull
		if (queueContext->DeviceContext->SerialWaitMask == 0) {
			dbgprintf("[i] VCOM wait mask not set, abort wait\n");
			status = STATUS_UNSUCCESSFUL;
			break;
		}

		// put the new request into the wait queue
		status = WdfRequestForwardToIoQueue(requestHandle, queueContext->SerialWaitQueue);
		if (status != STATUS_SUCCESS) {
			dbgerrprintf("[!] VCOM WdfRequestForwardToIoQueue failed: NTSTATUS 0x%x\n", status);
			break;
		}

		dbgprintf("[i] VCOM IOCTL_SERIAL_WAIT_ON_MASK pending\n");
		return; // do not continue, don't complete the request
	}

	case IOCTL_SERIAL_SET_WAIT_MASK:
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_SET_WAIT_MASK called\n");

		// read wait mask and apply to context
		status = PortDeviceCopyFromRequest(requestHandle, 0, &queueContext->DeviceContext->SerialWaitMask, sizeof(ULONG));

		// abort pendinjg wait request, if there is one
		WDFREQUEST previousWaitRequest;
		status = WdfIoQueueRetrieveNextRequest(queueContext->SerialWaitQueue, &previousWaitRequest);
		if (status == STATUS_SUCCESS) {

			// zero indicates that no event was actualy triggered, but the wait ended because of this set mask request
			ULONG eventMask = 0;
			status = PortDeviceCopyToRequest(previousWaitRequest, 0, &eventMask, bytesReturned = sizeof(eventMask));

			WdfRequestComplete(previousWaitRequest, STATUS_SUCCESS);

		}

		status = STATUS_SUCCESS;
		break;
	}

	case IOCTL_SERIAL_GET_WAIT_MASK:
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_GET_WAIT_MASK called\n");

		// write wait mask to request
		status = PortDeviceCopyToRequest(requestHandle, 0, &queueContext->DeviceContext->SerialWaitMask, bytesReturned = sizeof(ULONG));

		break;
	}

	case  IOCTL_SERIAL_SET_DTR: // set the DTR line if not controled by autmatic flow control
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_SET_DTR called\n");

		// set DTR bit
		deviceContext->FlowControlState |= SERIAL_DTR_STATE;
		status = STATUS_SUCCESS;

		PortDeviceTriggerAppWait(queueContext, APPLINK_EVENT_COMSTATE);

		break;
	}
	case  IOCTL_SERIAL_CLR_DTR:
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_CLR_DTR called\n");

		// clear DTR bit
		deviceContext->FlowControlState &= ~SERIAL_DTR_STATE;
		status = STATUS_SUCCESS;

		PortDeviceTriggerAppWait(queueContext, APPLINK_EVENT_COMSTATE);

		break;
	}

	case  IOCTL_SERIAL_SET_RTS: // set the RTS line if not controled by autmatic flow control
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_SET_RTS called\n");

		// set RTS bit
		deviceContext->FlowControlState |= SERIAL_RTS_STATE;
		status = STATUS_SUCCESS;

		PortDeviceTriggerAppWait(queueContext, APPLINK_EVENT_COMSTATE);

		break;
	}
	case  IOCTL_SERIAL_CLR_RTS:
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_CLR_RTS called\n");

		// clear RTS bit
		deviceContext->FlowControlState &= ~SERIAL_RTS_STATE;
		status = STATUS_SUCCESS;

		PortDeviceTriggerAppWait(queueContext, APPLINK_EVENT_COMSTATE);

		break;
	}

	case  IOCTL_SERIAL_GET_DTRRTS: // status of DTR and RTS as bit wise or ULONG
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_GET_DTRRTS called\n");

		// write status value to request
		ULONG dtrrtsState = deviceContext->FlowControlState & (SERIAL_RTS_STATE | SERIAL_DTR_STATE);
		status = PortDeviceCopyToRequest(requestHandle, 0, &dtrrtsState, bytesReturned = sizeof(ULONG));

		break;
	}
	case IOCTL_SERIAL_GET_MODEMSTATUS: // alternate way of reading DTR and RTS signals plus all the other signals
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_GET_MODEMSTATUS called\n");

		// write status value to request
		status = PortDeviceCopyToRequest(requestHandle, 0, &deviceContext->FlowControlState, bytesReturned = sizeof(ULONG));

		break;
	}

	case IOCTL_SERIAL_PURGE:
	case IOCTL_SERIAL_RESET_DEVICE: // resets all buffers
	{

		dbgprintf("[i] VCOM IOCTL_SERIAL_RESET_DEVICE called\n");

		// reset buffers
		RingbufferClear(&deviceContext->SerialRxBuffer);
		RingbufferClear(&deviceContext->SerialTxBuffer);
		status = STATUS_SUCCESS;

		PortDeviceTriggerAppWait(queueContext, APPLINK_EVENT_TXEMPTY);

		break;
	}

		/* not supported control codes (these mostly don't make much sense for an virtual port) */

	case IOCTL_SERIAL_SET_BREAK_ON:
	case IOCTL_SERIAL_SET_BREAK_OFF:
	case IOCTL_SERIAL_SET_XON:
	case IOCTL_SERIAL_SET_XOFF:
		// TODO maybe implement break signal and XON/XOFF emulation passtrough if possible
	case IOCTL_SERIAL_SET_QUEUE_SIZE:
	case IOCTL_SERIAL_GET_COMMSTATUS:

		dbgprintf("[i] VCOM IODeviceControl[Serial] called: not implemented IOCTL_CODE: 0x%x\n", controlCode);

		status = STATUS_SUCCESS; // even tough they do nothing, they have to succeed to not throw errors in applications
		break;

	case IOCTL_SERIAL_XOFF_COUNTER:
	case IOCTL_SERIAL_IMMEDIATE_CHAR:
	case IOCTL_SERIAL_GET_PROPERTIES:
	case IOCTL_SERIAL_SET_MODEM_CONTROL:
	case IOCTL_SERIAL_GET_MODEM_CONTROL:
	case IOCTL_SERIAL_SET_FIFO_CONTROL:

		dbgprintf("[i] VCOM IODeviceControl[Serial] called: not supported IOCTL_CODE: 0x%x\n", controlCode);

		status = STATUS_NOT_CAPABLE; // these are usualy not used and can fail
		break;

	default:

		dbgprintf("[i] VCOM IODeviceControl[Serial] called: invalid IOCTL_CODE: 0x%x\n", controlCode);

		status = STATUS_INVALID_PARAMETER;
		break;

	}

	WdfRequestCompleteWithInformation(requestHandle, status, bytesReturned);

}
