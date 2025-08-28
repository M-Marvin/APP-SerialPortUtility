
#include <memory.h>
#include "vcombuffers.h"
#include "vcomcontrol.h"
#include "dbgprint.h"

NTSTATUS CreateBuffers(BUFFER_CONTEXT* bufferContext)
{

	bufferContext->BufferSizes.ReceiveSize = DEFAULT_TXRX_BUFFFER_SIZE;
	bufferContext->BufferSizes.TransmitSize = DEFAULT_TXRX_BUFFFER_SIZE;

	bufferContext->TransmitBuffer = 0;
	bufferContext->ReceiveBuffer = 0;

	return ReallocateBuffers(bufferContext);

}

void CleanupBuffers(BUFFER_CONTEXT* bufferContext)
{

	if (bufferContext->TransmitBuffer != 0) {
		HeapFree(GetProcessHeap(), 0, bufferContext->TransmitBuffer);
		bufferContext->TransmitBuffer = 0;
	}

	if (bufferContext->ReceiveBuffer != 0) {
		HeapFree(GetProcessHeap(), 0, bufferContext->ReceiveBuffer);
		bufferContext->ReceiveBuffer = 0;
	}

}

NTSTATUS ReallocateBuffers(BUFFER_CONTEXT* bufferContext)
{

	if (bufferContext->BufferSizes.ReceiveSize == 0 || bufferContext->BufferSizes.TransmitSize == 0) {
		return STATUS_INVALID_BUFFER_SIZE;
	}

	CleanupBuffers(bufferContext);

	bufferContext->TransmitBuffer = HeapAlloc(GetProcessHeap(), 0, bufferContext->BufferSizes.TransmitSize + 1); // spare byte to avoid pointer collision in ring buffer
	if (bufferContext->TransmitBuffer == 0) {
		dbgerrprintf("[!] VCOM unable to allocate transmission buffer of size: %lu\n", bufferContext->BufferSizes.TransmitSize);
		return STATUS_MEMORY_NOT_ALLOCATED;
	}

	bufferContext->ReceiveBuffer = HeapAlloc(GetProcessHeap(), 0, bufferContext->BufferSizes.ReceiveSize + 1); // spare byte to avoid pointer collision in ring buffer
	if (bufferContext->ReceiveBuffer == 0) {
		dbgerrprintf("[!] VCOM unable to allocate transmission buffer of size: %lu\n", bufferContext->BufferSizes.ReceiveSize);
		return STATUS_MEMORY_NOT_ALLOCATED;
	}

	bufferContext->TransmitReadPtr = 0;
	bufferContext->TransmitWritePtr = 0;
	bufferContext->ReceiveReadPtr = 0;
	bufferContext->ReceiveWritePtr = 0;

	return STATUS_SUCCESS;

}

static NTSTATUS WriteBuffer(char* buffer, ULONG capacity, ULONG* writePtr, ULONG* readPtr, WDFREQUEST requestHandle, ULONG length, ULONG* bytesCopied, ULONG* bufferContent)
{

	dbgprintf("[i] WRITE TO BUFFER: readPtr=%lu writePtr=%lu\n", *readPtr, *writePtr);

	ULONG freeBytes = (*writePtr >= *readPtr ? capacity - (*writePtr - *readPtr) : *readPtr - *writePtr) - 1;
	*bytesCopied = min(freeBytes, length);
	*bufferContent = capacity - freeBytes;

	if (*bytesCopied == 0) {
		return STATUS_SUCCESS; // a full buffer is not considered an error here
	}

	ULONG ptrToEnd = min(capacity - *writePtr - 1, *bytesCopied);	// bytes that can be copied before hitting the end of the buffer
	ULONG startToPtr = *bytesCopied - ptrToEnd;						// remaining bytes that then have to be copied to the start of the buffer

	dbgprintf("[i] VARIABLES: freeBytes=%lu bytesCopied=%lu ptrToEnd=%lu startToPtr=%lu\n", freeBytes, *bytesCopied, ptrToEnd, startToPtr);

	NTSTATUS status = CopyFromRequest(requestHandle, 0, buffer + *writePtr, ptrToEnd);
	if (status == STATUS_SUCCESS && startToPtr > 0) {
		status = CopyFromRequest(requestHandle, ptrToEnd, buffer, startToPtr);
	}
	if (status != STATUS_SUCCESS) {
		*bytesCopied = 0;
	}
	else {
		*writePtr = (*writePtr + *bytesCopied) % capacity;
	}

	*bufferContent += *bytesCopied;

	dbgprintf("[i] WRITE TO BUFFER: readPtr=%lu writePtr=%lu\n", *readPtr, *writePtr);

	return status;

}

static NTSTATUS ReadBuffer(char* buffer, ULONG capacity, ULONG* writePtr, ULONG* readPtr, WDFREQUEST requestHandle, ULONG length, ULONG* bytesCopied, ULONG* bufferContent)
{

	dbgprintf("[i] READ FROM BUFFER: readPtr=%lu writePtr=%lu\n", *readPtr, *writePtr);

	ULONG availableBytes = (*writePtr >= *readPtr ? *writePtr - *readPtr : capacity - (*readPtr - *writePtr));
	*bytesCopied = min(availableBytes, length);
	*bufferContent = availableBytes;

	if (*bytesCopied == 0) {
		return STATUS_SUCCESS; // a full buffer is not considered an error here
	}

	ULONG ptrToEnd = min(capacity - *readPtr - 1, *bytesCopied);	// bytes that can be copied before hitting the end of the buffer
	ULONG startToPtr = *bytesCopied - ptrToEnd;						// remaining bytes that then have to be copied from the start of the buffer

	dbgprintf("[i] VARIABLES: availableBytes=%lu bytesCopied=%lu ptrToEnd=%lu startToPtr=%lu\n", availableBytes, *bytesCopied, ptrToEnd, startToPtr);

	NTSTATUS status = CopyToRequest(requestHandle, 0, buffer + *readPtr, ptrToEnd);
	if (status == STATUS_SUCCESS && startToPtr > 0) {
		status = CopyToRequest(requestHandle, ptrToEnd, buffer, startToPtr);
	}
	if (status != STATUS_SUCCESS) {
		*bytesCopied = 0;
	}
	else {
		*readPtr = (*readPtr + *bytesCopied) % capacity;
	}

	*bufferContent -= *bytesCopied;

	dbgprintf("[i] READ FROM BUFFER: readPtr=%lu writePtr=%lu\n", *readPtr, *writePtr);

	return status;

}

NTSTATUS WriteTransmittion(BUFFER_CONTEXT* bufferContext, WDFREQUEST requestHandle, ULONG length, ULONG* bytesCopied, ULONG* bufferContent)
{
	return WriteBuffer(
		bufferContext->TransmitBuffer,
		bufferContext->BufferSizes.TransmitSize,
		&bufferContext->TransmitWritePtr,
		&bufferContext->TransmitReadPtr,
		requestHandle,
		length,
		bytesCopied,
		bufferContent
	);
}

NTSTATUS ReadTransmittion(BUFFER_CONTEXT* bufferContext, WDFREQUEST requestHandle, ULONG length, ULONG* bytesCopied, ULONG* bufferContent)
{
	return ReadBuffer(
		bufferContext->TransmitBuffer,
		bufferContext->BufferSizes.TransmitSize,
		&bufferContext->TransmitWritePtr,
		&bufferContext->TransmitReadPtr,
		requestHandle,
		length,
		bytesCopied,
		bufferContent
	);
}

NTSTATUS WriteReception(BUFFER_CONTEXT* bufferContext, WDFREQUEST requestHandle, ULONG length, ULONG* bytesCopied, ULONG* bufferContent)
{
	return WriteBuffer(
		bufferContext->ReceiveBuffer,
		bufferContext->BufferSizes.ReceiveSize,
		&bufferContext->ReceiveWritePtr,
		&bufferContext->ReceiveReadPtr,
		requestHandle,
		length,
		bytesCopied,
		bufferContent
	);
}

NTSTATUS ReadReception(BUFFER_CONTEXT* bufferContext, WDFREQUEST requestHandle, ULONG length, ULONG* bytesCopied, ULONG* bufferContent)
{
	return ReadBuffer(
		bufferContext->ReceiveBuffer,
		bufferContext->BufferSizes.ReceiveSize,
		&bufferContext->ReceiveWritePtr,
		&bufferContext->ReceiveReadPtr,
		requestHandle,
		length,
		bytesCopied,
		bufferContent
	);
}
