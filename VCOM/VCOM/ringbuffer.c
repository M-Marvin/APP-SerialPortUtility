/* Implements the ringbuffer.
*/

#include <memory.h>
#include "ringbuffer.h"
#include "vcomport.h"
#include "dbgprint.h"

NTSTATUS RingbufferCreate(Ringbuffer* bufferContext)
{

	bufferContext->Capacity = DEFAULT_TXRX_BUFFFER_SIZE;
	bufferContext->Buffer = 0;
	bufferContext->ReadPtr = 0;
	bufferContext->WritePtr = 0;

	return RingbufferReallocate(bufferContext);

}

void RingbufferCleanup(Ringbuffer* bufferContext)
{
	if (bufferContext->Buffer != 0) {
		HeapFree(GetProcessHeap(), 0, bufferContext->Buffer);
		bufferContext->Buffer = 0;
	}
}

void RingbufferClear(Ringbuffer* bufferContext)
{
	bufferContext->WritePtr = bufferContext->ReadPtr = 0;
}

NTSTATUS RingbufferReallocate(Ringbuffer* bufferContext)
{

	if (bufferContext->Capacity == 0) {
		return STATUS_INVALID_BUFFER_SIZE;
	}

	RingbufferCleanup(bufferContext);

	bufferContext->Buffer = HeapAlloc(GetProcessHeap(), 0, bufferContext->Capacity + 1); // spare byte to avoid pointer collision in ring buffer
	if (bufferContext->Buffer == 0) {
		dbgerrprintf("[!] VCOM unable to allocate ringbuffer of size: %llu\n", bufferContext->Capacity);
		return STATUS_FATAL_MEMORY_EXHAUSTION;
	}

	bufferContext->ReadPtr = 0;
	bufferContext->WritePtr = 0;

	return STATUS_SUCCESS;

}

NTSTATUS RingbufferWrite(Ringbuffer* buffer, WDFREQUEST requestHandle, SIZE_T length, SIZE_T* bytesCopied, SIZE_T* bufferContent)
{

	dbgprintf("[i] WRITE TO BUFFER: readPtr=%llu writePtr=%llu\n", buffer->ReadPtr, buffer->WritePtr);

	SIZE_T freeBytes = (buffer->WritePtr >= buffer->ReadPtr ? buffer->Capacity - (buffer->WritePtr - buffer->ReadPtr) : buffer->ReadPtr - buffer->WritePtr) - 1;
	*bytesCopied = min(freeBytes, length);
	*bufferContent = buffer->Capacity - freeBytes;

	if (*bytesCopied == 0) {
		return STATUS_SUCCESS; // a full buffer is not considered an error here
	}

	SIZE_T ptrToEnd = min(buffer->Capacity - buffer->WritePtr, *bytesCopied);		// bytes that can be copied before hitting the end of either input buffer or ringbuffer
	SIZE_T startToPtr = *bytesCopied - ptrToEnd;						// remaining bytes that then have to be copied to the start of the buffer

	dbgprintf("[i] BUFFER STATE: freeBytes=%llu bytesCopied=%llu ptrToEnd=%llu startToPtr=%llu\n", freeBytes, *bytesCopied, ptrToEnd, startToPtr);

	NTSTATUS status = STATUS_SUCCESS;
	if (ptrToEnd > 0)
		status = PortDeviceCopyFromRequest(requestHandle, 0, buffer->Buffer + buffer->WritePtr, ptrToEnd);
	if (status == STATUS_SUCCESS && startToPtr > 0) {
		status = PortDeviceCopyFromRequest(requestHandle, ptrToEnd, buffer->Buffer, startToPtr);
	}
	if (status != STATUS_SUCCESS) {
		*bytesCopied = 0;
	}
	else {
		buffer->WritePtr = (buffer->WritePtr + *bytesCopied) % buffer->Capacity;
	}

	*bufferContent += *bytesCopied;

	dbgprintf("[i] WRITE TO BUFFER: readPtr=%llu writePtr=%llu\n", buffer->ReadPtr, buffer->WritePtr);

	return status;

}

NTSTATUS RingbufferRead(Ringbuffer* buffer, WDFREQUEST requestHandle, SIZE_T length, SIZE_T* bytesCopied, SIZE_T* bufferContent)
{

	dbgprintf("[i] READ FROM BUFFER: readPtr=%llu writePtr=%llu\n", buffer->ReadPtr, buffer->WritePtr);

	SIZE_T availableBytes = (buffer->WritePtr >= buffer->ReadPtr ? buffer->WritePtr - buffer->ReadPtr : buffer->Capacity - (buffer->ReadPtr - buffer->WritePtr));
	*bytesCopied = min(availableBytes, length);
	*bufferContent = availableBytes;

	if (*bytesCopied == 0) {
		return STATUS_SUCCESS; // a empty buffer is not considered an error here
	}

	SIZE_T ptrToEnd = min(buffer->Capacity - buffer->ReadPtr, *bytesCopied);		// bytes that can be copied before hitting the end of the buffer
	SIZE_T startToPtr = *bytesCopied - ptrToEnd;						// remaining bytes that then have to be copied from the start of the buffer

	dbgprintf("[i] BUFFER STATE: availableBytes=%llu bytesCopied=%llu ptrToEnd=%llu startToPtr=%llu\n", availableBytes, *bytesCopied, ptrToEnd, startToPtr);

	NTSTATUS status = STATUS_SUCCESS;
	if (ptrToEnd > 0)
		status = PortDeviceCopyToRequest(requestHandle, 0, buffer->Buffer + buffer->ReadPtr, ptrToEnd);
	if (status == STATUS_SUCCESS && startToPtr > 0) {
		status = PortDeviceCopyToRequest(requestHandle, ptrToEnd, buffer->Buffer, startToPtr);
	}
	if (status != STATUS_SUCCESS) {
		*bytesCopied = 0;
	}
	else {
		buffer->ReadPtr = (buffer->ReadPtr + *bytesCopied) % buffer->Capacity;
	}

	*bufferContent -= *bytesCopied;

	dbgprintf("[i] READ FROM BUFFER: readPtr=%llu writePtr=%llu\n", buffer->ReadPtr, buffer->WritePtr);

	return status;

}
