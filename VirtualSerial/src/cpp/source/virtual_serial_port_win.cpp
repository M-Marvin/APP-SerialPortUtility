/*
 * vcom.cpp
 *
 *  Created on: 21.08.2025
 *      Author: marvi
 */

#include "VCOM/public.h"
#include "VCOM/serial.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "virtual_serial_port.hpp"

void printError(const char* format) {
	DWORD errorCode = GetLastError();
	if (errorCode == 0) return;
	LPSTR msg;
	if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&msg, 0, NULL) > 0) {
		printf(format, errorCode, msg); fflush(stdout);
		LocalFree(msg);
	} else {
		printf(format, errorCode, "<no error message defined>\n"); fflush(stdout);
	}
}

class VirtualSerialPortWin : public SerialAccess::VirtualSerialPort
{

private:
	OVERLAPPED writeOverlapped;
	OVERLAPPED readOverlapped;
	OVERLAPPED waitOverlapped;
	HANDLE writeEventHandle;
	HANDLE readEventHandle;
	HANDLE waitEventHandle;
	ULONG eventMask = 0;
	ULONG eventMaskReturned = 0;
	HANDLE comPortHandle;
	const char* portFileName;

public:
	VirtualSerialPortWin(const char* portFile)
	{
		this->portFileName = portFile;
		this->comPortHandle = INVALID_HANDLE_VALUE;
		this->writeOverlapped.hEvent = this->writeEventHandle = INVALID_HANDLE_VALUE;
		this->readOverlapped.hEvent = this->readEventHandle = INVALID_HANDLE_VALUE;
		this->waitOverlapped.hEvent = this->waitEventHandle = INVALID_HANDLE_VALUE;
	}

	~VirtualSerialPortWin() {
		closePort();
	}

	bool openPort() override
	{
		if (isCreated()) return false;

		// open port
		this->comPortHandle = CreateFileA(this->portFileName, GENERIC_WRITE | GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
		if (this->comPortHandle != INVALID_HANDLE_VALUE) {
			// send virtual port specific IOCTL to verify virtual port
			unsigned long tx, rx;
			if (!getBufferSizes(&tx, &rx) || !(tx > 0 && rx > 0)) {
				// port exits
				CloseHandle(this->comPortHandle);
				return false;
			}
		}

		this->writeEventHandle = CreateEventA(NULL, TRUE, FALSE, NULL);
		if (this->writeEventHandle == NULL) {
			printError("error 0x%x in VirtualSerialPort:createPort:CreateEventA: %s");
			closePort();
			return false;
		}

		this->readEventHandle = CreateEventA(NULL, TRUE, FALSE, NULL);
		if (this->readEventHandle == NULL) {
			printError("error 0x%x in VirtualSerialPort:createPort:CreateEventA: %s");
			closePort();
			return false;
		}

		this->waitEventHandle = CreateEventA(NULL, TRUE, FALSE, NULL);
		if (this->waitEventHandle == NULL) {
			printError("error 0x%x in VirtualSerialPort:createPort:CreateEventA: %s");
			closePort();
			return false;
		}

		return true;
	}

	void closePort() override
	{
		if (!isCreated()) return;
		CloseHandle(this->comPortHandle);
		if (this->writeEventHandle != NULL)
			CloseHandle(this->writeEventHandle);
		if (this->readEventHandle != NULL)
			CloseHandle(this->readEventHandle);
		if (this->waitEventHandle != NULL)
			CloseHandle(this->waitEventHandle);
		this->comPortHandle = INVALID_HANDLE_VALUE;
		this->writeEventHandle = NULL;
		this->readEventHandle = NULL;
		this->waitEventHandle = NULL;
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
			if (GetLastError() == ERROR_INVALID_HANDLE) {
				closePort();
				return false;
			}
			printError("error 0x%x in VirtualSerialPort:getConfig:DeviceIoControl(IOCTL_APPLINK_GET_BAUD): %s");
			return false;
		}

		config.baudRate = baudRate.BaudRate;

		SERIAL_LINE_CONTROL lineControl;
		if (!::DeviceIoControl(this->comPortHandle, IOCTL_APPLINK_GET_LINE_CONTROL, NULL, 0, &lineControl, sizeof(SERIAL_LINE_CONTROL), &bytesReturned, NULL)) {
			if (GetLastError() == ERROR_INVALID_HANDLE) {
				closePort();
				return false;
			}
			printError("error 0x%x in VirtualSerialPort:getConfig:DeviceIoControl(IOCTL_APPLINK_GET_LINE_CONTROL): %s");
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
			if (GetLastError() == ERROR_INVALID_HANDLE) {
				closePort();
				return false;
			}
			printError("error 0x%x in VirtualSerialPort:getConfig:DeviceIoControl(IOCTL_APPLINK_GET_LINE_CONTROL): %s");
			return false;
		}

		config.xonChar = serialChars.XonChar;
		config.xoffChar = serialChars.XoffChar;

		SERIAL_HANDFLOW flowControl;
		if (!::DeviceIoControl(this->comPortHandle, IOCTL_APPLINK_GET_FLOW_CONTROL, NULL, 0, &flowControl, sizeof(SERIAL_HANDFLOW), &bytesReturned, NULL)) {
			if (GetLastError() == ERROR_INVALID_HANDLE) {
				closePort();
				return false;
			}
			printError("error 0x%x in VirtualSerialPort:getConfig:DeviceIoControl(IOCTL_APPLINK_GET_FLOW_CONTROL): %s");
			return false;
		}

		bool dtrdsr = (flowControl.ControlHandShake & SERIAL_DTR_CONTROL) && (flowControl.ControlHandShake & SERIAL_DSR_HANDSHAKE);
		bool rtscts = (flowControl.ControlHandShake & SERIAL_RTS_CONTROL) && (flowControl.ControlHandShake & SERIAL_CTS_HANDSHAKE);
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
		if (!isCreated()) return false;

		ULONG bytesReturned;

		SERIAL_BAUD_RATE serialBaud;
		if (!::DeviceIoControl(this->comPortHandle, IOCTL_APPLINK_GET_BAUD, NULL, 0, &serialBaud, sizeof(SERIAL_BAUD_RATE), &bytesReturned, NULL)) {
			if (GetLastError() == ERROR_INVALID_HANDLE) {
				closePort();
				return false;
			}
			printError("error 0x%x in VirtualSerialPort:getBaud:DeviceIoControl(IOCTL_APPLINK_GET_BAUD): %s");
			return false;
		}

		return serialBaud.BaudRate;
	}

	bool getTimeouts(int* readTimeout, int* readTimeoutInterval, int* writeTimeout) override
	{
		if (!isCreated()) return false;

		ULONG bytesReturned;

		SERIAL_TIMEOUTS serialTimeouts;
		if (!::DeviceIoControl(this->comPortHandle, IOCTL_APPLINK_GET_TIMEOUTS, NULL, 0, &serialTimeouts, sizeof(SERIAL_TIMEOUTS), &bytesReturned, NULL)) {
			if (GetLastError() == ERROR_INVALID_HANDLE) {
				closePort();
				return false;
			}
			printError("error 0x%x in VirtualSerialPort:getTimeouts:DeviceIoControl(IOCTL_APPLINK_GET_TIMEOUTS): %s");
			return false;
		}

		*readTimeout = (int) serialTimeouts.ReadTotalTimeoutConstant == MAXULONG32 ? -1 : serialTimeouts.ReadTotalTimeoutConstant;
		*readTimeoutInterval = (int) serialTimeouts.ReadIntervalTimeout;
		*writeTimeout = (int) serialTimeouts.WriteTotalTimeoutConstant;
		return true;
	}

	long long int readBytes(char* buffer, unsigned long bufferCapacity, bool wait) override
	{
		if (!isCreated()) return -2;

		unsigned long receivedBytes;

		// Check if there is already an operation pending
		if (this->readOverlapped.hEvent == INVALID_HANDLE_VALUE) {

			// If not, initiate new operation

			// Create overlapped event
			ZeroMemory(&this->readOverlapped, sizeof(OVERLAPPED));
			this->readOverlapped.hEvent = this->readEventHandle;
			if (!ResetEvent(this->readEventHandle)) {
				printError("error 0x%x in VirtualSerialPort:readBytes:ResetEvent: %s");
				return -2;
			}

			// Initiate read operation
			if (!DeviceIoControl(this->comPortHandle, IOCTL_APPLINK_READ_BUFFER, NULL, 0, buffer, bufferCapacity, &receivedBytes, &this->readOverlapped)) {

				if (GetLastError() == ERROR_INVALID_HANDLE) {
					closePort();
					return false;
				}

				// If not completed yet, check if error
				if (GetLastError() != ERROR_IO_PENDING) {
					printError("error 0x%x in VirtualSerialPort:readBytes:ReadFile: %s");
					return -2;
				}

			} else {

				// Already completed, return results
				this->readOverlapped.hEvent = INVALID_HANDLE_VALUE;
				return receivedBytes;

			}

		}

		// Wait for completition
		if (!GetOverlappedResult(this->comPortHandle, &this->readOverlapped, &receivedBytes, wait)) {
			if (GetLastError() == ERROR_IO_INCOMPLETE)
				return -1; // not yet completed
			this->readOverlapped.hEvent = INVALID_HANDLE_VALUE;
			if (GetLastError() == ERROR_OPERATION_ABORTED)
				return -2; // port closed
			if (GetLastError() == ERROR_INVALID_HANDLE) {
				closePort();
				return false;
			}
			printError("error 0x%x in VirtualSerialPort:readBytes:GetOverlappedResult: %s");
			return -2;
		}
		this->readOverlapped.hEvent = INVALID_HANDLE_VALUE;

		return receivedBytes;
	}

	long long int writeBytes(const char* buffer, unsigned long bufferLength, bool wait) override
	{
		if (!isCreated()) return -2;

		unsigned long writtenBytes;

		// Check if there is already an operation pending
		if (this->writeOverlapped.hEvent == INVALID_HANDLE_VALUE) {

			// If not, initiate new operation

			// Create overlapped event
			ZeroMemory(&this->writeOverlapped, sizeof(OVERLAPPED));
			this->writeOverlapped.hEvent = this->writeEventHandle;
			if (!ResetEvent(this->writeEventHandle)) {
				printError("error 0x%x in VirtualSerialPort:writeBytes:ResetEvent: %s");
				return -2;
			}

			// Initiate write operation
			if (!DeviceIoControl(this->comPortHandle, IOCTL_APPLINK_WRITE_BUFFER, (char*) buffer, bufferLength, NULL, 0, &writtenBytes, &this->writeOverlapped)) {

				if (GetLastError() == ERROR_INVALID_HANDLE) {
					closePort();
					return false;
				}

				// If not completed yet, check if error
				if (GetLastError() != ERROR_IO_PENDING) {
					printError("error 0x%x in VirtualSerialPort:writeBytes:WriteFile: %s");
					return -2;
				}

			} else {

				// Already completed, return results
				this->writeOverlapped.hEvent = INVALID_HANDLE_VALUE;
				return writtenBytes;

			}

		}

		// If not, wait for completition
		if (!GetOverlappedResult(this->comPortHandle, &this->writeOverlapped, &writtenBytes, wait)) {
			if (GetLastError() == ERROR_IO_INCOMPLETE)
				return -1; // not yet completed
			this->writeOverlapped.hEvent = INVALID_HANDLE_VALUE;
			if (GetLastError() == ERROR_OPERATION_ABORTED)
				return -2; // port closed
			if (GetLastError() == ERROR_INVALID_HANDLE) {
				closePort();
				return false;
			}
			printError("error 0x%x in VirtualSerialPort:writeBytes:GetOverlappedResult: %s");
			return -2;
		}
		this->writeOverlapped.hEvent = INVALID_HANDLE_VALUE;

		return writtenBytes;
	}

	bool getBufferSizes(unsigned long* txBufferSize, unsigned long* rxBufferSize) override
	{
		if (!isCreated()) return false;

		ULONG bytesReturned;

		BUFFER_SIZES bufferSizes;
		if (!::DeviceIoControl(this->comPortHandle, IOCTL_APPLINK_GET_BUFFER_SIZES, NULL, 0, &bufferSizes, sizeof(BUFFER_SIZES), &bytesReturned, NULL)) {
			if (GetLastError() == ERROR_INVALID_HANDLE) {
				closePort();
				return false;
			}
			printError("error 0x%x in VirtualSerialPort:getPortState:DeviceIoControl(IOCTL_APPLINK_GET_BUFFER_SIZES): %s");
			return false;
		}

		*txBufferSize = bufferSizes.TransmitSize;
		*rxBufferSize = bufferSizes.ReceiveSize;
		return true;
	}

	bool setBufferSizes(unsigned long txBufferSize, unsigned long rxBufferSize) override
	{
		if (!isCreated()) return false;

		ULONG bytesReturned;

		BUFFER_SIZES bufferSizes;
		bufferSizes.TransmitSize = txBufferSize;
		bufferSizes.ReceiveSize = rxBufferSize;

		if (!::DeviceIoControl(this->comPortHandle, IOCTL_APPLINK_SET_BUFFER_SIZES, &bufferSizes, sizeof(BUFFER_SIZES), NULL, 0, &bytesReturned, NULL)) {
			if (GetLastError() == ERROR_INVALID_HANDLE) {
				closePort();
				return false;
			}
			printError("error 0x%x in VirtualSerialPort:getPortState:DeviceIoControl(IOCTL_APPLINK_SET_BUFFER_SIZES): %s");
			return false;
		}

		return true;
	}

	bool getPortState(bool& dsr, bool& cts) override
	{
		if (!isCreated()) return false;

		ULONG bytesReturned;

		ULONG comStatus;
		if (!::DeviceIoControl(this->comPortHandle, IOCTL_APPLINK_GET_COMSTATUS, NULL, 0, &comStatus, sizeof(ULONG), &bytesReturned, NULL)) {
			if (GetLastError() == ERROR_INVALID_HANDLE) {
				closePort();
				return false;
			}
			printError("error 0x%x in VirtualSerialPort:getPortState:DeviceIoControl(IOCTL_APPLINK_GET_COMSTATUS): %s");
			return false;
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
			if (GetLastError() == ERROR_INVALID_HANDLE) {
				closePort();
				return false;
			}
			printError("error 0x%x in VirtualSerialPort:setManualPortState:DeviceIoControl(IOCTL_APPLINK_GET_COMSTATUS): %s");
			return false;
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
			if (GetLastError() == ERROR_INVALID_HANDLE) {
				closePort();
				return false;
			}
			printError("error 0x%x in VirtualSerialPort:setManualPortState:DeviceIoControl(IOCTL_APPLINK_SET_COMSTATUS): %s");
			return false;
		}

		return true;
	}

	bool waitForEvents(bool& configChange, bool& timeoutChange, bool& comStateChange, bool& dataReceived, bool& dataTransmitted, bool wait)
	{
		if (!isCreated()) return false;

		// Check if the event mask was changed
		ULONG bytesReturned;
		DWORD newMask = 0;
		if (configChange) newMask |= APPLINK_EVENT_CONFIG;
		if (timeoutChange) newMask |= APPLINK_EVENT_TIMEOUTS;
		if (comStateChange) newMask |= APPLINK_EVENT_COMSTATE;
		if (dataReceived) newMask |= APPLINK_EVENT_RXCHAR;
		if (dataTransmitted) newMask |= APPLINK_EVENT_TXEMPTY;
		if (newMask != eventMask && this->waitOverlapped.hEvent != INVALID_HANDLE_VALUE) {
			// Cancel previous wait
			this->waitOverlapped.hEvent = INVALID_HANDLE_VALUE;
		}
		eventMask = newMask;

		// Check if there is already an operation pending
		if (this->waitOverlapped.hEvent == INVALID_HANDLE_VALUE) {

			// If not, initiate new operation

			// if no event was requested, return
			if (!configChange && !timeoutChange && !comStateChange && !dataReceived && !dataTransmitted) return true;

			// Create overlapped event
			ZeroMemory(&this->waitOverlapped, sizeof(OVERLAPPED));
			this->waitOverlapped.hEvent = this->waitEventHandle;
			if (!ResetEvent(this->waitEventHandle)) {
				printError("error 0x%x in VirtualSerialPort:waitForEvents:ResetEvent: %s");
				return false;
			}

			// Initiate wait operation
			if (!DeviceIoControl(this->comPortHandle, IOCTL_APPLINK_WAIT_FOR_CHANGE, &this->eventMask, sizeof(ULONG), &this->eventMaskReturned, sizeof(ULONG), &bytesReturned, &this->waitOverlapped)) {

				if (GetLastError() == ERROR_INVALID_HANDLE) {
					closePort();
					return false;
				}

				// If not completed yet, check if error
				if (GetLastError() != ERROR_IO_PENDING) {
					printError("error 0x%x in VirtualSerialPort:waitForEvents:WaitCommEvent: %s");
					return false;
				}

			} else {

				// Already completed, return results
				this->waitOverlapped.hEvent = INVALID_HANDLE_VALUE;
				configChange = eventMaskReturned & APPLINK_EVENT_CONFIG;
				timeoutChange = eventMaskReturned & APPLINK_EVENT_TIMEOUTS;
				comStateChange = eventMaskReturned & APPLINK_EVENT_COMSTATE;
				dataReceived = eventMaskReturned & APPLINK_EVENT_RXCHAR;
				dataTransmitted = eventMaskReturned & APPLINK_EVENT_TXEMPTY;
				return true;

			}

		}

		configChange = false;
		timeoutChange = false;
		comStateChange = false;
		dataReceived = false;
		dataTransmitted = false;

		// If not, wait for completition
		if (!GetOverlappedResult(this->comPortHandle, &this->waitOverlapped, &bytesReturned, wait)) {
			if (GetLastError() == ERROR_IO_INCOMPLETE)
				return true; // not yet completed
			this->waitOverlapped.hEvent = INVALID_HANDLE_VALUE;
			if (GetLastError() == ERROR_OPERATION_ABORTED)
				return false; // port closed
			if (GetLastError() == ERROR_INVALID_HANDLE) {
				closePort();
				return false;
			}
			printError("error 0x%x in VirtualSerialPort:waitForEvents:GetOverlappedResult: %s");
			return false;
		}
		this->waitOverlapped.hEvent = INVALID_HANDLE_VALUE;

		configChange = eventMaskReturned & APPLINK_EVENT_CONFIG;
		timeoutChange = eventMaskReturned & APPLINK_EVENT_TIMEOUTS;
		comStateChange = eventMaskReturned & APPLINK_EVENT_COMSTATE;
		dataReceived = eventMaskReturned & APPLINK_EVENT_RXCHAR;
		dataTransmitted = eventMaskReturned & APPLINK_EVENT_TXEMPTY;
		return true;
	}

	void abortWait()
	{
		if (!isCreated() || this->waitOverlapped.hEvent == INVALID_HANDLE_VALUE) return;

		if (CancelIoEx(this->comPortHandle, &this->waitOverlapped)) {
			this->waitOverlapped.hEvent = INVALID_HANDLE_VALUE;
		}
	}

};

SerialAccess::VirtualSerialPort* SerialAccess::newVirtualSerialPort(const char* portFile) {
	return new VirtualSerialPortWin(portFile);
}

SerialAccess::VirtualSerialPort* SerialAccess::newVirtualSerialPortS(const std::string& portFile) {
	return new VirtualSerialPortWin(portFile.c_str());
}
