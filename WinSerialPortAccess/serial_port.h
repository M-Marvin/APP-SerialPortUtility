#pragma once

#ifdef PLATFORM_WIN
#define LIB_EXPORT __declspec(dllexport)
#define LIB_IMPORT __declspec(dllimport)
#endif

struct SerialPortImplData;

class SerialPort
{

public:
	LIB_EXPORT SerialPort(const char* portFile);
	LIB_EXPORT ~SerialPort();
	LIB_EXPORT void setBaud(int baud);
	LIB_EXPORT int getBaud();
	LIB_EXPORT void setTimeouts(int readTimeout, int writeTimeout);
	LIB_EXPORT bool openPort();
	LIB_EXPORT void closePort();
	LIB_EXPORT bool isOpen();
	LIB_EXPORT unsigned long readBytes(char* buffer, unsigned long bufferCapacity);
	LIB_EXPORT unsigned long readBytesConsecutive(char* buffer, unsigned long bufferCapacity, long long consecutiveDelay, long long receptionWaitTimeout);
	LIB_EXPORT unsigned long writeBytes(const char* buffer, unsigned long bufferLength);

private:
	SerialPortImplData* implData;
	
};

