/* Implements IO read/write/wait operations for the serial port side of the port device
*/

#include "vcomport.h"
#include "serial.h"
#include "dbgprint.h"

void PortDeviceIOReadSerial(WDFQUEUE queueHandle, WDFREQUEST requestHandle, SIZE_T length)
{

	dbgprintf("[i] VCOM PortDeviceIOReadSerial called: buflen: %llu\n", length);

	NTSTATUS status;
	QUEUE_CONTEXT* queueContext = GetQueueContext(queueHandle);
	DEVICE_CONTEXT* deviceContext = queueContext->DeviceContext;
	REQUEST_CONTEXT* requestContext = GetRequestContext(requestHandle);
	SIZE_T bytesRead;
	SIZE_T bufferContent;

	// attempt to read the full buffer length from the reception buffer
	status = RingbufferRead(&deviceContext->SerialRxBuffer, requestHandle, length, &bytesRead, &bufferContent);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM PortDeviceIOReadSerial:RingbufferWrite(SerialRx) failed: NTSTATUS 0x%x\n", status);
		WdfRequestComplete(requestHandle, status);
		return;
	}

	// update the number of bytes transfered in the context
	requestContext->ReadWrite.BytesTransfered += bytesRead;

	// complete or re-queue request with timeouts if not all bytes have ben read yet
	if (requestContext->ReadWrite.BytesTransfered >= length) {
		// stop timeout timer if running
		WdfTimerStop(queueContext->SerialReadTimer, FALSE);
		WdfTimerStop(queueContext->SerialReadIntervalTimer, FALSE);

		WdfRequestCompleteWithInformation(requestHandle, status, requestContext->ReadWrite.BytesTransfered);
	}
	else {
		SIZE_T timeout = length * deviceContext->Timeouts.ReadTotalTimeoutMultiplier + deviceContext->Timeouts.ReadTotalTimeoutConstant;

		// start timeout timer if an timeout is defined
		if (timeout > 0) {
			dbgprintf("[DBG] started read timeout: %llu\n", timeout);
			WdfTimerStart(queueContext->SerialReadTimer, -(LONGLONG)timeout * 10000);
		}

		if (bytesRead > 0) {
			SIZE_T timeoutInterval = deviceContext->Timeouts.ReadIntervalTimeout;

			// start additonal interval timeout timer if an byte was received
			if (timeoutInterval > 0) {
				dbgprintf("[DBG] started interval timeout: %llu\n", timeout);
				WdfTimerStart(queueContext->SerialReadIntervalTimer, -(LONGLONG)timeoutInterval * 10000);
			}
		}

		// put request into parking queue
		WdfRequestForwardToIoQueue(requestHandle, queueContext->SerialReadQueue);
	}

	if (bufferContent == 0)
		PortDeviceTriggerAppWait(queueContext, APPLINK_EVENT_TXEMPTY);

	// invoke pending write requests, let them attempt to use the freed space in the buffer
	PortDeviceReInvokeRequests(queueContext, queueContext->AppWriteQueue);

}

void PortDeviceEvtReadSerialTimedOut(WDFTIMER timer) {

	WDFQUEUE queueHandle = (WDFQUEUE)WdfTimerGetParentObject(timer);
	QUEUE_CONTEXT* queueContext = GetQueueContext(queueHandle);

	dbgprintf("[i] PortDeviceIOReadSerial timed out\n");

	// stop the other read timeout to avoid unwanted trigger for next read operation
	if (timer == queueContext->SerialReadTimer)
		WdfTimerStop(queueContext->SerialReadIntervalTimer, FALSE);
	else
		WdfTimerStop(queueContext->SerialReadTimer, FALSE);

	// complete the read operation with timeout status
	PortDeviceTimeoutRequests(queueContext->SerialReadQueue);

}

void PortDeviceIOWriteSerial(WDFQUEUE queueHandle, WDFREQUEST requestHandle, SIZE_T length)
{

	dbgprintf("[i] VCOM PortDeviceIOWriteSerial called: buflen: %llu\n", length);

	NTSTATUS status;
	QUEUE_CONTEXT* queueContext = GetQueueContext(queueHandle);
	DEVICE_CONTEXT* deviceContext = queueContext->DeviceContext;
	REQUEST_CONTEXT* requestContext = GetRequestContext(requestHandle);
	SIZE_T bytesWritten;
	SIZE_T bufferContent;

	// attempt to write the full buffer length to the transmission buffer
	status = RingbufferWrite(&deviceContext->SerialTxBuffer, requestHandle, length, &bytesWritten, &bufferContent);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM PortDeviceIOWriteSerial:RingbufferWrite(SerialTx) failed: NTSTATUS 0x%x\n", status);
		WdfRequestComplete(requestHandle, status);
		return;
	}

	// update the number of bytes transfered in the context
	requestContext->ReadWrite.BytesTransfered += bytesWritten;

	// complete or re-queue request if not all bytes have ben written yet
	if (requestContext->ReadWrite.BytesTransfered >= length) {
		// stop timeout timer if running
		WdfTimerStop(queueContext->SerialWriteTimer, FALSE);

		WdfRequestCompleteWithInformation(requestHandle, status, requestContext->ReadWrite.BytesTransfered);
	}
	else {
		SIZE_T timeout = length * deviceContext->Timeouts.WriteTotalTimeoutMultiplier + deviceContext->Timeouts.WriteTotalTimeoutConstant;

		// start timeout timer if an timeout is defined
		if (timeout > 0)
			WdfTimerStart(queueContext->SerialWriteTimer, -(LONGLONG)timeout * 10000);

		// put request into parking queue
		WdfRequestForwardToIoQueue(requestHandle, queueContext->SerialWriteQueue);
	}

	if (bytesWritten > 0)
		PortDeviceTriggerSerialWait(queueContext, APPLINK_EVENT_RXCHAR | (bufferContent > (deviceContext->SerialTxBuffer.Capacity * 0.8) ? APPLINK_EVENT_RX80FULL : 0));

	// invoke pending read requests, let them attempt to use the new data in the buffer
	PortDeviceReInvokeRequests(queueContext, queueContext->AppReadQueue);

}

void PortDeviceEvtWriteSerialTimedOut(WDFTIMER timer) {

	WDFQUEUE queueHandle = (WDFQUEUE)WdfTimerGetParentObject(timer);
	QUEUE_CONTEXT* queueContext = GetQueueContext(queueHandle);

	dbgprintf("[i] PortDeviceIOWriteSerial timed out\n");

	// complete the read operation with timeout status
	PortDeviceTimeoutRequests(queueContext->SerialWriteQueue);

}
