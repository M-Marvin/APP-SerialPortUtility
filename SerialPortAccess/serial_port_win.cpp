#ifdef PLATFORM_WIN

#include "serial_port.h"
#include <thread>
#include <chrono>
#include <stdio.h>
#include <windows.h>

// FIXME Update with code from ADB Project

struct SerialPortImplData {
	DCB comPortState;
	COMMTIMEOUTS comPortTimeouts;
	HANDLE comPortHandle;
	const char* portFileName;
};

SerialPort::SerialPort(const char* portFile)
{
	this->implData = new SerialPortImplData();
	this->implData->portFileName = portFile;
	this->implData->comPortHandle = INVALID_HANDLE_VALUE;
	this->implData->comPortState = {};
	this->implData->comPortTimeouts = {};
}

SerialPort::~SerialPort() {
	delete this->implData;
}

bool SerialPort::openPort()
{
	if (this->implData->comPortHandle != INVALID_HANDLE_VALUE) return false;
	this->implData->comPortHandle = CreateFileA(this->implData->portFileName, GENERIC_WRITE | GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);

	if (isOpen()) {
		GetCommState(this->implData->comPortHandle, &this->implData->comPortState);

		// Default port settings extracted after use with PuTTY
		this->implData->comPortState.fBinary = 1;
		this->implData->comPortState.fParity = 0;
		this->implData->comPortState.fOutxCtsFlow = 0;
		this->implData->comPortState.fOutxDsrFlow = 0;
		this->implData->comPortState.fDtrControl = 1;
		this->implData->comPortState.fDsrSensitivity = 0;
		this->implData->comPortState.fTXContinueOnXoff = 0;
		this->implData->comPortState.fOutX = 1;
		this->implData->comPortState.fInX = 1;
		this->implData->comPortState.fErrorChar = 0;
		this->implData->comPortState.fNull = 0;
		this->implData->comPortState.fRtsControl = 1;
		this->implData->comPortState.fAbortOnError = 0;
		this->implData->comPortState.fDummy2 = 0;
		this->implData->comPortState.wReserved = 0;
		this->implData->comPortState.XonLim = 2048;
		this->implData->comPortState.XoffLim = 512;
		this->implData->comPortState.ByteSize = 8;
		this->implData->comPortState.Parity = 0;
		this->implData->comPortState.StopBits = 0;
		this->implData->comPortState.XonChar = 17;
		this->implData->comPortState.XoffChar = 19;
		this->implData->comPortState.ErrorChar = 0;
		this->implData->comPortState.EofChar = 0;
		this->implData->comPortState.EvtChar = 0;
		this->implData->comPortState.wReserved1 = 0;

		SetCommState(this->implData->comPortHandle, &this->implData->comPortState);
		return true;
	}

	return false;
}

void SerialPort::closePort()
{
	if (this->implData->comPortHandle == INVALID_HANDLE_VALUE) return;
	CloseHandle(this->implData->comPortHandle);
	this->implData->comPortHandle = INVALID_HANDLE_VALUE;
}

bool SerialPort::isOpen()
{
	return this->implData->comPortHandle != INVALID_HANDLE_VALUE;
}

void SerialPort::setBaud(int baud)
{
	if (this->implData->comPortHandle == INVALID_HANDLE_VALUE) return;
	GetCommState(this->implData->comPortHandle, &this->implData->comPortState);
	this->implData->comPortState.BaudRate = baud;
	SetCommState(this->implData->comPortHandle, &this->implData->comPortState);
}

int SerialPort::getBaud()
{
	if (this->implData->comPortHandle == INVALID_HANDLE_VALUE) return -1;
	GetCommState(this->implData->comPortHandle, &this->implData->comPortState);
	return this->implData->comPortState.BaudRate;
}

void SerialPort::setTimeouts(int readTimeout, int writeTimeout)
{
	GetCommTimeouts(this->implData->comPortHandle, &this->implData->comPortTimeouts);
	this->implData->comPortTimeouts.ReadTotalTimeoutConstant = readTimeout;
	this->implData->comPortTimeouts.WriteTotalTimeoutConstant = writeTimeout;
	SetCommTimeouts(this->implData->comPortHandle, &this->implData->comPortTimeouts);
}

unsigned long SerialPort::readBytes(char* buffer, unsigned long bufferCapacity)
{
	if (this->implData->comPortHandle == INVALID_HANDLE_VALUE) return 0;
	unsigned long receivedBytes = 0;
	if (!ReadFile(this->implData->comPortHandle, buffer, bufferCapacity, &receivedBytes, NULL)) return -1;
	return receivedBytes;
}

unsigned long SerialPort::readBytesConsecutive(char* buffer, unsigned long bufferCapacity, long long consecutiveDelay, long long receptionWaitTimeout)
{
	if (this->implData->comPortHandle == INVALID_HANDLE_VALUE) return 0;
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
	if (this->implData->comPortHandle == INVALID_HANDLE_VALUE) return 0;
	unsigned long writtenBytes;
	WriteFile(this->implData->comPortHandle, buffer, bufferLength, &writtenBytes, NULL);
	return writtenBytes;
}

#endif
