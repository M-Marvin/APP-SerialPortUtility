
#include "serial_port.h"
#include <thread>
#include <chrono>

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
	return this->isOpen();
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

unsigned long SerialPort::readBytesBurst(char* buffer, unsigned long bufferCapacity, long long receptionLoopDelay)
{
	if (comPortHandle == INVALID_HANDLE_VALUE) return -1;
	unsigned long receivedBytes;
	while ((receivedBytes = readBytes(buffer, bufferCapacity)) == 0);
	while (receivedBytes < bufferCapacity)
	{
		unsigned int lastReceived = readBytes(buffer + receivedBytes, bufferCapacity - receivedBytes);
		if (lastReceived == 0) break;
		receivedBytes += lastReceived;
		std::this_thread::sleep_for(std::chrono::milliseconds(receptionLoopDelay));
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
