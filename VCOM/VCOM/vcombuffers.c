
#include <memory.h>
#include "vcombuffers.h"
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
		return STATUS_INVALID_CONFIG_VALUE;
	}

	CleanupBuffers(bufferContext);

	bufferContext->TransmitBuffer = HeapAlloc(GetProcessHeap(), 0, bufferContext->BufferSizes.TransmitSize);
	if (bufferContext->TransmitBuffer == 0) {
		dbgerrprintf("[!] VCOM unable to allocate transmission buffer of size: %lu\n", bufferContext->BufferSizes.TransmitSize);
		return STATUS_MEMORY_NOT_ALLOCATED;
	}

	bufferContext->ReceiveBuffer = HeapAlloc(GetProcessHeap(), 0, bufferContext->BufferSizes.ReceiveSize);
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

NTSTATUS WriteBuffer(char* buffer, ULONG capacity, ULONG* writePtr, ULONG* readPtr, WDFREQUEST requestHandle, ULONG length, ULONG* bytesCopied)
{

}

NTSTATUS ReadBuffer(char* buffer, ULONG capacity, ULONG writePtr, ULONG readPtr, WDFREQUEST requestHandle, ULONG length, ULONG* bytesCopied)
{

}

NTSTATUS WriteTransmittion(BUFFER_CONTEXT* bufferContext, WDFREQUEST requestHandle, ULONG length, ULONG* bytesCopied)
{
	return WriteBuffer(
		bufferContext->TransmitBuffer,
		bufferContext->BufferSizes.TransmitSize,
		&bufferContext->TransmitWritePtr,
		&bufferContext->TransmitReadPtr,
		requestHandle,
		length,
		bytesCopied
	);
}

NTSTATUS ReadTransmittion(BUFFER_CONTEXT* bufferContext, WDFREQUEST requestHandle, ULONG length, ULONG* bytesCopied)
{
	return ReadBuffer(
		bufferContext->TransmitBuffer,
		bufferContext->BufferSizes.TransmitSize,
		&bufferContext->TransmitWritePtr,
		&bufferContext->TransmitReadPtr,
		requestHandle,
		length,
		bytesCopied
	);
}

NTSTATUS WriteReception(BUFFER_CONTEXT* bufferContext, WDFREQUEST requestHandle, ULONG length, ULONG* bytesCopied)
{
	return WriteBuffer(
		bufferContext->ReceiveBuffer,
		bufferContext->BufferSizes.ReceiveSize,
		&bufferContext->ReceiveWritePtr,
		&bufferContext->ReceiveReadPtr,
		requestHandle,
		length,
		bytesCopied
	);
}

NTSTATUS ReadReception(BUFFER_CONTEXT* bufferContext, WDFREQUEST requestHandle, ULONG length, ULONG* bytesCopied)
{
	return ReadBuffer(
		bufferContext->ReceiveBuffer,
		bufferContext->BufferSizes.ReceiveSize,
		&bufferContext->ReceiveWritePtr,
		&bufferContext->ReceiveReadPtr,
		requestHandle,
		length,
		bytesCopied
	);
}
