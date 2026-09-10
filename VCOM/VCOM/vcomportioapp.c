/* Implements IO read/write/wait operations for the application side of the port device
*/

#include "vcomport.h"
#include "serial.h"
#include "dbgprint.h"

void PortDeviceIOReadApp(WDFQUEUE queueHandle, WDFREQUEST requestHandle, SIZE_T length)
{

	dbgprintf("[i] VCOM PortDeviceIOReadApp called\n");

	NTSTATUS status;
	QUEUE_CONTEXT* queueContext = GetQueueContext(queueHandle);
	DEVICE_CONTEXT* deviceContext = queueContext->DeviceContext;
	REQUEST_CONTEXT* requestContext = GetRequestContext(requestHandle);
	size_t bytesRead;
	size_t bufferContent;

	// attempt to read the full buffer length from the reception buffer
	status = RingbufferRead(&deviceContext->SerialTxBuffer, requestHandle, length, &bytesRead, &bufferContent);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM IORead:ReadReception failed: NTSTATUS 0x%x\n", status);
		WdfRequestComplete(requestHandle, status);
		return;
	}

	// update the number of bytes transfered in the context
	requestContext->ReadWrite.BytesTransfered += bytesRead;

	// complete or re-queue request with timeouts if not all bytes have ben read yet
	if (requestContext->ReadWrite.BytesTransfered >= length) {
		// stop timeout timer if running
		WdfTimerStop(queueContext->AppReadTimer, FALSE);
		WdfTimerStop(queueContext->AppReadIntervalTimer, FALSE);

		WdfRequestCompleteWithInformation(requestHandle, status, requestContext->ReadWrite.BytesTransfered);
	}
	else {
		// TODO timout config
		SIZE_T timeout = length * deviceContext->AppTimeouts.ReadTotalTimeoutMultiplier + deviceContext->AppTimeouts.ReadTotalTimeoutConstant;

		// start timeout timer if an timeout is defined
		if (timeout > 0) {
			dbgprintf("[DBG] started read timeout: %llu\n", timeout);
			WdfTimerStart(queueContext->AppReadTimer, -(LONGLONG)timeout * 10000);
		}

		if (bytesRead > 0) {
			SIZE_T timeoutInterval = deviceContext->AppTimeouts.ReadIntervalTimeout;

			// start additonal interval timeout timer if an byte was received
			if (timeoutInterval > 0) {
				dbgprintf("[DBG] started interval timeout: %llu\n", timeout);
				WdfTimerStart(queueContext->AppReadIntervalTimer, -(LONGLONG)timeoutInterval * 10000);
			}
		}

		// put request into parking queue
		WdfRequestForwardToIoQueue(requestHandle, queueContext->AppReadQueue);
	}

	if (bufferContent == 0)
		PortDeviceTriggerSerialWait(queueContext, SERIAL_EV_TXEMPTY);

	// invoke pending write requests, let them attempt to use the freed space in the buffer
	PortDeviceReInvokeRequests(queueContext, queueContext->SerialWriteQueue);

}

void PortDeviceEvtReadAppTimedOut(WDFTIMER timer) {

	WDFQUEUE queueHandle = (WDFQUEUE)WdfTimerGetParentObject(timer);
	QUEUE_CONTEXT* queueContext = GetQueueContext(queueHandle);

	dbgprintf("[i] PortDeviceIOReadApp timed out\n");

	// stop the other read timeout to avoid unwanted trigger for next read operation
	if (timer == queueContext->AppReadTimer)
		WdfTimerStop(queueContext->AppReadIntervalTimer, FALSE);
	else
		WdfTimerStop(queueContext->AppReadTimer, FALSE);

	// complete the read operation with timeout status
	PortDeviceTimeoutRequests(queueContext->AppReadQueue);

}

void PortDeviceIOWriteApp(WDFQUEUE queueHandle, WDFREQUEST requestHandle, SIZE_T length)
{

	dbgprintf("[i] VCOM PortDeviceIOWriteApp called\n");

	NTSTATUS status;
	QUEUE_CONTEXT* queueContext = GetQueueContext(queueHandle);
	DEVICE_CONTEXT* deviceContext = queueContext->DeviceContext;
	REQUEST_CONTEXT* requestContext = GetRequestContext(requestHandle);
	SIZE_T bytesWritten;
	SIZE_T bufferContent;

	// attempt to write the full buffer length to the transmission buffer
	status = RingbufferWrite(&deviceContext->SerialRxBuffer, requestHandle, length, &bytesWritten, &bufferContent);
	if (status != STATUS_SUCCESS) {
		dbgerrprintf("[!] VCOM PortDeviceIOWriteApp:WriteTransmittion failed: NTSTATUS 0x%x\n", status);
		WdfRequestComplete(requestHandle, status);
		return;
	}

	// update the number of bytes transfered in the context
	requestContext->ReadWrite.BytesTransfered += bytesWritten;

	// complete or re-queue request if not all bytes have ben written yet
	if (requestContext->ReadWrite.BytesTransfered >= length) {
		// stop timeout timer if running
		WdfTimerStop(queueContext->AppWriteTimer, FALSE);

		WdfRequestCompleteWithInformation(requestHandle, status, requestContext->ReadWrite.BytesTransfered);
	}
	else {
		SIZE_T timeout = length * deviceContext->AppTimeouts.WriteTotalTimeoutMultiplier + deviceContext->AppTimeouts.WriteTotalTimeoutConstant;

		// start timeout timer if an timeout is defined
		if (timeout > 0)
			WdfTimerStart(queueContext->AppWriteTimer, -(LONGLONG)timeout * 10000);

		// put request into parking queue
		WdfRequestForwardToIoQueue(requestHandle, queueContext->AppWriteQueue);
	}

	if (bytesWritten > 0)
		PortDeviceTriggerSerialWait(queueContext, SERIAL_EV_RXCHAR | (bufferContent > (deviceContext->SerialRxBuffer.Capacity * 0.8) ? SERIAL_EV_RX80FULL : 0));

	// invoke pending read requests, let them attempt to use the new data in the buffer
	PortDeviceReInvokeRequests(queueContext, queueContext->SerialReadQueue);

}

void PortDeviceEvtWriteAppTimedOut(WDFTIMER timer)
{

	WDFQUEUE queueHandle = (WDFQUEUE)WdfTimerGetParentObject(timer);
	QUEUE_CONTEXT* queueContext = GetQueueContext(queueHandle);

	dbgprintf("[i] PortDeviceIOWriteApp timed out\n");

	// complete the read operation with timeout status
	PortDeviceTimeoutRequests(queueContext->AppWriteQueue);
	
}
