#pragma once

#include <Windows.h>

class SerialPort
{

protected:
	DCB comPortState;
	COMMTIMEOUTS comPortTimeouts;
	HANDLE comPortHandle;
	const char* portFileName;
	
public:
	_declspec(dllexport) SerialPort(const char* portFile);
	_declspec(dllexport) void setBaud(int baud);
	_declspec(dllexport) int getBaud();
	_declspec(dllexport) void setTimeouts(int readTimeout, int writeTimeout);
	_declspec(dllexport) bool openPort();
	_declspec(dllexport) void closePort();
	_declspec(dllexport) bool isOpen();
	_declspec(dllexport) unsigned long readBytes(char* buffer, unsigned long bufferCapacity);
	_declspec(dllexport) unsigned long readBytesBurst(char* buffer, unsigned long bufferCapacity, long long receptionLoopDelay);
	_declspec(dllexport) unsigned long writeBytes(const char* buffer, unsigned long bufferLength);
	
};

