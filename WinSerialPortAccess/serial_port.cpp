
#include "serial_port.h"
#include <thread>
#include <chrono>
#include <stdio.h>

SerialPort::SerialPort(const char* portFile)
{
	this->portFileName = portFile;
	this->comPortHandle = INVALID_HANDLE_VALUE;
	this->comPortState = {};
	this->comPortTimeouts = {};
}

bool SerialPort::openPort()
{
	if (comPortHandle != INVALID_HANDLE_VALUE) return false;
	comPortHandle = CreateFileA(portFileName, GENERIC_WRITE | GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);

	if (isOpen()) {
		GetCommState(comPortHandle, &comPortState);

		// Default port settings extracted after use with PuTTY
		comPortState.fBinary = 1;
		comPortState.fParity = 0;
		comPortState.fOutxCtsFlow = 0;
		comPortState.fOutxDsrFlow = 0;
		comPortState.fDtrControl = 1;
		comPortState.fDsrSensitivity = 0;
		comPortState.fTXContinueOnXoff = 0;
		comPortState.fOutX = 1;
		comPortState.fInX = 1;
		comPortState.fErrorChar = 0;
		comPortState.fNull = 0;
		comPortState.fRtsControl = 1;
		comPortState.fAbortOnError = 0;
		comPortState.fDummy2 = 0;
		comPortState.wReserved = 0;
		comPortState.XonLim = 2048;
		comPortState.XoffLim = 512;
		comPortState.ByteSize = 8;
		comPortState.Parity = 0;
		comPortState.StopBits = 0;
		comPortState.XonChar = 17;
		comPortState.XoffChar = 19;
		comPortState.ErrorChar = 0;
		comPortState.EofChar = 0;
		comPortState.EvtChar = 0;
		comPortState.wReserved1 = 0;

		SetCommState(comPortHandle, &comPortState);
		return true;
	}

	return false;
}

void SerialPort::closePort()
{
	if (comPortHandle == INVALID_HANDLE_VALUE) return;
	CloseHandle(comPortHandle);
	comPortHandle = INVALID_HANDLE_VALUE;
}

bool SerialPort::isOpen()
{
	return comPortHandle != INVALID_HANDLE_VALUE;
}

void SerialPort::setBaud(int baud)
{
	if (comPortHandle == INVALID_HANDLE_VALUE) return;
	GetCommState(comPortHandle, &comPortState);
	comPortState.BaudRate = baud;
	SetCommState(comPortHandle, &comPortState);
}

int SerialPort::getBaud()
{
	if (comPortHandle == INVALID_HANDLE_VALUE) return -1;
	GetCommState(comPortHandle, &comPortState);
	return comPortState.BaudRate;
}

void SerialPort::setTimeouts(int readTimeout, int writeTimeout)
{
	GetCommTimeouts(comPortHandle, &comPortTimeouts);
	comPortTimeouts.ReadTotalTimeoutConstant = readTimeout;
	comPortTimeouts.WriteTotalTimeoutConstant = writeTimeout;
	SetCommTimeouts(comPortHandle, &comPortTimeouts);
}

unsigned long SerialPort::readBytes(char* buffer, unsigned long bufferCapacity)
{
	if (comPortHandle == INVALID_HANDLE_VALUE) return -1;
	unsigned long receivedBytes = 0;
	if (!ReadFile(comPortHandle, buffer, bufferCapacity, &receivedBytes, NULL)) return -1;
	return receivedBytes;
}

unsigned long SerialPort::readBytesConsecutive(char* buffer, unsigned long bufferCapacity, long long consecutiveDelay, long long receptionWaitTimeout)
{
	if (comPortHandle == INVALID_HANDLE_VALUE) return -1;
	unsigned long receivedBytes;
	long long waitStart = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	while ((receivedBytes = readBytes(buffer, bufferCapacity)) == 0) {
		long long time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		if (time - waitStart > receptionWaitTimeout) return 0;
	}
	while (receivedBytes < bufferCapacity)
	{
		unsigned int lastReceived = readBytes(buffer + receivedBytes, bufferCapacity - receivedBytes);
		if (lastReceived == 0) break;
		receivedBytes += lastReceived;
		std::this_thread::sleep_for(std::chrono::milliseconds(consecutiveDelay));
	}
	return receivedBytes;
}

unsigned long SerialPort::writeBytes(const char* buffer, unsigned long bufferLength)
{
	if (comPortHandle == INVALID_HANDLE_VALUE) return -1;
	unsigned long writtenBytes;
	WriteFile(comPortHandle, buffer, bufferLength, &writtenBytes, NULL);
	return writtenBytes;
}
