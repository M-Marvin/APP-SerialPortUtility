/*
 * vcom.cpp
 *
 *  Created on: 21.08.2025
 *      Author: marvi
 */

#include "VCOM/public.h"
#include "VCOM/serial.h"
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include "../public/virtual_serial_port.hpp"

void printError(const char* format) {
	DWORD errorCode = GetLastError();
	if (errorCode == 0) return;
	LPSTR msg;
	if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&msg, 0, NULL) > 0) {
		printf(format, errorCode, msg); fflush(stdout);
		LocalFree(msg);
	}
}

class VirtualSerialPort : public SerialAccess::VirtualSerialPort
{

private:
	HANDLE comPortHandle;
	const char* portFileName;

public:
	VirtualSerialPort(const char* portFile)
	{
		this->portFileName = portFile;
		this->comPortHandle = INVALID_HANDLE_VALUE;
	}

	~VirtualSerialPort() {
		removePort();
	}

	bool createPort() override
	{
		if (isCreated()) return false;

		// TODO virtual port cration

		this->comPortHandle = CreateFileA(this->portFileName, GENERIC_WRITE | GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

		if (!isCreated())
			return false;

		return true;
	}

	void removePort() override
	{
		if (!isCreated()) return;
		CloseHandle(this->comPortHandle);
		this->comPortHandle = INVALID_HANDLE_VALUE;

		// TODO virtual port removal
	}

	bool isCreated() override
	{
		return this->comPortHandle != INVALID_HANDLE_VALUE;
	}

	bool getConfig(SerialAccess::SerialPortConfig &config) override
	{
		if (!isCreated()) return false;

		ULONG bytesReturned;

		SERIAL_BAUD_RATE baudRate;
		if (!::DeviceIoControl(this->comPortHandle, IOCTL_APPLINK_GET_BAUD, NULL, 0, &baudRate, sizeof(SERIAL_BAUD_RATE), &bytesReturned, NULL)) {
			printError("error %lu in SerialPort:setConfig:DeviceIoControl(IOCTL_APPLINK_GET_BAUD): %s");
			return false;
		}

		config.baudRate = baudRate.BaudRate;

		SERIAL_LINE_CONTROL lineControl;
		if (!::DeviceIoControl(this->comPortHandle, IOCTL_APPLINK_GET_LINE_CONTROL, NULL, 0, &lineControl, sizeof(SERIAL_LINE_CONTROL), &bytesReturned, NULL)) {
			printError("error %lu in SerialPort:setConfig:DeviceIoControl(IOCTL_APPLINK_GET_LINE_CONTROL): %s");
			return false;
		}

		config.dataBits = lineControl.WordLength;

		switch (lineControl.StopBits) {
		case STOP_BIT_1: config.stopBits = SerialAccess::SPC_STOPB_ONE; break;
		case STOP_BITS_1_5: config.stopBits = SerialAccess::SPC_STOPB_ONE_HALF; break;
		case STOP_BITS_2: config.stopBits = SerialAccess::SPC_STOPB_TWO; break;
		default: config.stopBits = SerialAccess::SPC_STOPB_UNDEFINED; break;
		}

		switch (lineControl.Parity) {
		case NO_PARITY: config.parity = SerialAccess::SPC_PARITY_NONE; break;
		case EVEN_PARITY: config.parity = SerialAccess::SPC_PARITY_EVEN; break;
		case ODD_PARITY: config.parity = SerialAccess::SPC_PARITY_ODD; break;
		case MARK_PARITY: config.parity = SerialAccess::SPC_PARITY_MARK; break;
		case SPACE_PARITY: config.parity = SerialAccess::SPC_PARITY_SPACE; break;
		default: config.parity = SerialAccess::SPC_PARITY_UNDEFINED; break;
		}

		SERIAL_CHARS serialChars;
		if (!::DeviceIoControl(this->comPortHandle, IOCTL_APPLINK_GET_CHARS, NULL, 0, &serialChars, sizeof(SERIAL_CHARS), &bytesReturned, NULL)) {
			printError("error %lu in SerialPort:setConfig:DeviceIoControl(IOCTL_APPLINK_GET_LINE_CONTROL): %s");
			return false;
		}

		config.xonChar = serialChars.XonChar;
		config.xoffChar = serialChars.XoffChar;

		SERIAL_HANDFLOW flowControl;
		if (!::DeviceIoControl(this->comPortHandle, IOCTL_APPLINK_GET_FLOW_CONTROL, NULL, 0, &flowControl, sizeof(SERIAL_HANDFLOW), &bytesReturned, NULL)) {
			printError("error %lu in SerialPort:setConfig:DeviceIoControl(IOCTL_APPLINK_GET_FLOW_CONTROL): %s");
			return false;
		}

		bool dtrdsr = flowControl.ControlHandShake & (SERIAL_DTR_CONTROL | SERIAL_DSR_HANDSHAKE);
		bool rtscts = flowControl.ControlHandShake & (SERIAL_RTS_CONTROL | SERIAL_CTS_HANDSHAKE);
		bool xonxoff = flowControl.XoffLimit > 0 || flowControl.XonLimit > 0;
		if (dtrdsr && !rtscts && !xonxoff) {
			config.flowControl = SerialAccess::SPC_FLOW_DSR_DTR;
		} else if (!dtrdsr && rtscts && !xonxoff) {
			config.flowControl = SerialAccess::SPC_FLOW_RTS_CTS;
		} else if (!dtrdsr && !rtscts && xonxoff) {
			config.flowControl = SerialAccess::SPC_FLOW_XON_XOFF;
		} else if (!dtrdsr && !rtscts && !xonxoff) {
			config.flowControl = SerialAccess::SPC_FLOW_NONE;
		} else {
			config.flowControl = SerialAccess::SPC_FLOW_UNDEFINED;
		}

		return true;
	}

	unsigned long getBaud() override
	{
		if (!isCreated()) return 0;

		ULONG bytesReturned;

		SERIAL_BAUD_RATE serialBaud;
		if (!::DeviceIoControl(this->comPortHandle, IOCTL_APPLINK_GET_BAUD, NULL, 0, &serialBaud, sizeof(SERIAL_BAUD_RATE), &bytesReturned, NULL)) {
			printError("error %lu in SerialPort:setConfig:DeviceIoControl(IOCTL_APPLINK_GET_BAUD): %s");
			return 0;
		}

		return serialBaud.BaudRate;
	}

	bool getTimeouts(int* readTimeout, int* readTimeoutInterval, int* writeTimeout) override
	{
		if (!isCreated()) return false;

		ULONG bytesReturned;

		SERIAL_TIMEOUTS serialTimeouts;
		if (!::DeviceIoControl(this->comPortHandle, IOCTL_APPLINK_GET_TIMEOUTS, NULL, 0, &serialTimeouts, sizeof(SERIAL_TIMEOUTS), &bytesReturned, NULL)) {
			printError("error %lu in SerialPort:setConfig:DeviceIoControl(IOCTL_APPLINK_GET_TIMEOUTS): %s");
			return 0;
		}

		*readTimeout = (int) serialTimeouts.ReadTotalTimeoutConstant == MAXULONG32 ? -1 : serialTimeouts.ReadTotalTimeoutConstant;
		*readTimeoutInterval = (int) serialTimeouts.ReadIntervalTimeout;
		*writeTimeout = (int) serialTimeouts.WriteTotalTimeoutConstant;
		return true;
	}

	unsigned long readBytes(char* buffer, unsigned long bufferCapacity) override
	{
		if (!isCreated()) return 0;

		ULONG bytesReturned;

		if (!::DeviceIoControl(this->comPortHandle, IOCTL_APPLINK_READ_BUFFER, NULL, 0, buffer, bufferCapacity, &bytesReturned, NULL)) {
			printError("error %lu in SerialPort:setConfig:DeviceIoControl(IOCTL_APPLINK_READ_BUFFER): %s");
			return 0;
		}

		return bytesReturned;
	}

	unsigned long writeBytes(const char* buffer, unsigned long bufferLength) override
	{
		if (!isCreated()) return 0;

		ULONG bytesReturned;
		if (!::DeviceIoControl(this->comPortHandle, IOCTL_APPLINK_WRITE_BUFFER, const_cast<char*>(buffer), bufferLength, NULL, 0, &bytesReturned, NULL)) {
			printError("error %lu in SerialPort:setConfig:DeviceIoControl(IOCTL_APPLINK_WRITE_BUFFER): %s");
			return 0;
		}

		return bufferLength;
	}

	bool getManualPortState(bool& dsr, bool& cts) override
	{
		if (!isCreated()) return false;

		ULONG bytesReturned;

		ULONG comStatus;
		if (!::DeviceIoControl(this->comPortHandle, IOCTL_APPLINK_GET_COMSTATUS, NULL, 0, &comStatus, sizeof(ULONG), &bytesReturned, NULL)) {
			printError("error %lu in SerialPort:setConfig:DeviceIoControl(IOCTL_APPLINK_GET_COMSTATUS): %s");
			return 0;
		}

		dsr = comStatus & SERIAL_DSR_STATE;
		cts = comStatus & SERIAL_CTS_STATE;
		return true;
	}

	bool setManualPortState(bool dtr, bool rts) override
	{
		if (!isCreated()) return false;

		ULONG bytesReturned;

		ULONG comStatus;
		if (!::DeviceIoControl(this->comPortHandle, IOCTL_APPLINK_GET_COMSTATUS, NULL, 0, &comStatus, sizeof(ULONG), &bytesReturned, NULL)) {
			printError("error %lu in SerialPort:setConfig:DeviceIoControl(IOCTL_APPLINK_GET_COMSTATUS): %s");
			return 0;
		}

		if (dtr)
			comStatus |= SERIAL_DTR_STATE;
		else
			comStatus &= ~SERIAL_DTR_STATE;

		if (rts)
			comStatus |= SERIAL_RTS_STATE;
		else
			comStatus &= ~SERIAL_RTS_STATE;

		if (!::DeviceIoControl(this->comPortHandle, IOCTL_APPLINK_SET_COMSTATUS, &comStatus, sizeof(ULONG), NULL, 0, &bytesReturned, NULL)) {
			printError("error %lu in SerialPort:setConfig:DeviceIoControl(IOCTL_APPLINK_SET_COMSTATUS): %s");
			return 0;
		}

		return true;
	}

	bool waitForEvents(bool& configChange, bool& timeoutChange, bool& comStateChange, bool& dataReady)
	{
		if (!isCreated()) return false;

		// if no event was requested, return
		if (!configChange && !timeoutChange && !comStateChange) return true;

		ULONG bytesReturned;
		ULONG eventMask = 0;
		if (configChange) eventMask = APPLINK_EVENT_CONFIG;
		if (timeoutChange) eventMask = APPLINK_EVENT_TIMEOUTS;
		if (comStateChange) eventMask = APPLINK_EVENT_COMSTATE;
		if (dataReady) eventMask = APPLINK_EVENT_NEWDATA;
		if (!::DeviceIoControl(this->comPortHandle, IOCTL_APPLINK_WAIT_FOR_CHANGE, &eventMask, sizeof(ULONG), &eventMask, sizeof(ULONG), &bytesReturned, NULL)) {
			printError("error %lu in SerialPort:setConfig:DeviceIoControl(IOCTL_APPLINK_WAIT_FOR_CHANGE): %s");
			return 0;
		}

		configChange = eventMask & APPLINK_EVENT_CONFIG;
		timeoutChange = eventMask & APPLINK_EVENT_TIMEOUTS;
		comStateChange = eventMask & APPLINK_EVENT_COMSTATE;
		dataReady = eventMask & APPLINK_EVENT_NEWDATA;
		return true;
	}

};
