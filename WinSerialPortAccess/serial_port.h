#pragma once

#include <windows.h>

class SerialPort
{

protected:
	DCB comPortState;
	COMMTIMEOUTS comPortTimeouts;
	HANDLE comPortHandle;
	const char* portFileName;
	
public:
	__declspec(dllexport) SerialPort(const char* portFile);
	__declspec(dllexport) void setBaud(int baud);
	__declspec(dllexport) int getBaud();
	__declspec(dllexport) void setTimeouts(int readTimeout, int writeTimeout);
	__declspec(dllexport) bool openPort();
	__declspec(dllexport) void closePort();
	__declspec(dllexport) bool isOpen();
	__declspec(dllexport) unsigned long readBytes(char* buffer, unsigned long bufferCapacity);
	__declspec(dllexport) unsigned long readBytesBurst(char* buffer, unsigned long bufferCapacity, long long receptionLoopDelay);
	__declspec(dllexport) unsigned long writeBytes(const char* buffer, unsigned long bufferLength);
	
};

