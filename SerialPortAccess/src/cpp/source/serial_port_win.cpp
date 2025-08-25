
#ifdef PLATFORM_WIN

#include "serial_port.hpp"
#include <windows.h>
#include <thread>
#include <chrono>
#include <stdio.h>

void printError(const char* format) {
	DWORD errorCode = GetLastError();
	if (errorCode == 0) return;
	LPSTR msg;
	if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&msg, 0, NULL) > 0) {
		setbuf(stdout, NULL); // Work around for errors printed during JNI
		printf(format, errorCode, msg);
		LocalFree(msg);
	}
}

class SerialPortWin : public SerialAccess::SerialPort {

private:
	DCB comPortState;
	COMMTIMEOUTS comPortTimeouts;
	OVERLAPPED writeOverlapped;
	OVERLAPPED readOverlapped;
	OVERLAPPED waitOverlapped;
	HANDLE writeEventHandle;
	HANDLE readEventHandle;
	HANDLE waitEventHandle;
	DWORD eventMask = 0;
	DWORD eventMaskReturned = 0;
	HANDLE comPortHandle;
	const char* portFileName;

public:

	SerialPortWin(const char* portFile)
	{
		this->portFileName = portFile;
		this->comPortHandle = INVALID_HANDLE_VALUE;
		this->comPortState = {0};
		this->comPortTimeouts = {0};
		this->writeOverlapped.hEvent = this->writeEventHandle = INVALID_HANDLE_VALUE;
		this->readOverlapped.hEvent = this->readEventHandle = INVALID_HANDLE_VALUE;
		this->waitOverlapped.hEvent = this->waitEventHandle = INVALID_HANDLE_VALUE;
	}

	~SerialPortWin() {
		closePort();
	}

	bool openPort() override
	{
		if (isOpen()) return false;
		this->comPortHandle = CreateFileA(this->portFileName, GENERIC_WRITE | GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

		if (!isOpen())
			return false;

		// We ignore if this fails, this could just mean that the default configuration is not supported
		setConfig(SerialAccess::DEFAULT_PORT_CONFIGURATION);

		this->writeEventHandle = CreateEventA(NULL, TRUE, FALSE, NULL);
		if (this->writeEventHandle == NULL) {
			printError("error %lu in SerialPort:openPort:CreateEventA: %s");
			closePort();
			return false;
		}

		this->readEventHandle = CreateEventA(NULL, TRUE, FALSE, NULL);
		if (this->readEventHandle == NULL) {
			printError("error %lu in SerialPort:openPort:CreateEventA: %s");
			closePort();
			return false;
		}

		this->waitEventHandle = CreateEventA(NULL, TRUE, FALSE, NULL);
		if (this->waitEventHandle == NULL) {
			printError("error %lu in SerialPort:openPort:CreateEventA: %s");
			closePort();
			return false;
		}

		return true;
	}

	void closePort() override
	{
		if (this->comPortHandle == INVALID_HANDLE_VALUE) return;
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

	bool isOpen() override
	{
		return this->comPortHandle != INVALID_HANDLE_VALUE;
	}

	bool setConfig(const SerialAccess::SerialPortConfig &config) override
	{
		if (!isOpen()) return false;

		if (!GetCommState(this->comPortHandle, &this->comPortState)) {
			printError("error %lu in SerialPort:setConfig:GetCommState: %s");
			return false;
		}

		this->comPortState.BaudRate = config.baudRate;
		this->comPortState.fBinary = TRUE;
		this->comPortState.fOutxCtsFlow = (config.flowControl == SerialAccess::SPC_FLOW_RTS_CTS);
		this->comPortState.fOutxDsrFlow = (config.flowControl == SerialAccess::SPC_FLOW_DSR_DTR);
		this->comPortState.fDtrControl = (config.flowControl == SerialAccess::SPC_FLOW_DSR_DTR) ? DTR_CONTROL_ENABLE : DTR_CONTROL_DISABLE;
		this->comPortState.fDsrSensitivity = (config.flowControl == SerialAccess::SPC_FLOW_DSR_DTR);
		this->comPortState.fTXContinueOnXoff = (config.flowControl == SerialAccess::SPC_FLOW_NONE);
		this->comPortState.fOutX = (config.flowControl == SerialAccess::SPC_FLOW_XON_XOFF);
		this->comPortState.fInX = (config.flowControl == SerialAccess::SPC_FLOW_XON_XOFF);
		this->comPortState.fErrorChar = 0;
		this->comPortState.fNull = 0;
		this->comPortState.fRtsControl = (config.flowControl == SerialAccess::SPC_FLOW_RTS_CTS) ? RTS_CONTROL_TOGGLE : RTS_CONTROL_ENABLE;
		this->comPortState.fAbortOnError = 0;
		this->comPortState.XonLim = 2048;
		this->comPortState.XoffLim = 512;
		this->comPortState.ByteSize = config.dataBits;
		this->comPortState.fParity = (config.parity != SerialAccess::SPC_PARITY_NONE);
		switch (config.parity) {
		case SerialAccess::SPC_PARITY_ODD: this->comPortState.Parity = ODDPARITY; break;
		case SerialAccess::SPC_PARITY_EVEN: this->comPortState.Parity = EVENPARITY; break;
		case SerialAccess::SPC_PARITY_MARK: this->comPortState.Parity = MARKPARITY; break;
		case SerialAccess::SPC_PARITY_SPACE: this->comPortState.Parity = SPACEPARITY; break;
		default: break;
		case SerialAccess::SPC_PARITY_NONE: this->comPortState.Parity = NOPARITY; break;
		}
		switch (config.stopBits) {
		case SerialAccess::SPC_STOPB_ONE_HALF: this->comPortState.StopBits = ONE5STOPBITS; break;
		case SerialAccess::SPC_STOPB_TWO: this->comPortState.StopBits = TWOSTOPBITS; break;
		default: break;
		case SerialAccess::SPC_STOPB_ONE: this->comPortState.StopBits = ONESTOPBIT; break;
		}
		this->comPortState.XonChar = config.xonChar;
		this->comPortState.XoffChar = config.xoffChar;

		if (!SetCommState(this->comPortHandle, &this->comPortState)) {
			printError("error %lu in SerialPort:setConfig:SetCommState: %s");
			return false;
		}

		return true;
	}

	bool getConfig(SerialAccess::SerialPortConfig &config) override
	{
		if (!isOpen()) return false;

		if (!GetCommState(this->comPortHandle, &this->comPortState)) {
			printError("error %lu in SerialPort:getConfig:GetCommState: %s");
			return false;
		}

		config.baudRate = this->comPortState.BaudRate;
		switch (this->comPortState.Parity) {
		case NOPARITY: config.parity = SerialAccess::SPC_PARITY_NONE; break;
		case ODDPARITY: config.parity = SerialAccess::SPC_PARITY_ODD; break;
		case EVENPARITY: config.parity = SerialAccess::SPC_PARITY_EVEN; break;
		case MARKPARITY: config.parity = SerialAccess::SPC_PARITY_MARK; break;
		case SPACEPARITY: config.parity = SerialAccess::SPC_PARITY_SPACE; break;
		default: config.parity = SerialAccess::SPC_PARITY_UNDEFINED; break;
		}
		config.dataBits = this->comPortState.ByteSize;
		switch (this->comPortState.StopBits) {
		case ONESTOPBIT: config.stopBits = SerialAccess::SPC_STOPB_ONE; break;
		case ONE5STOPBITS: config.stopBits = SerialAccess::SPC_STOPB_ONE_HALF; break;
		case TWOSTOPBITS: config.stopBits = SerialAccess::SPC_STOPB_TWO; break;
		default: config.stopBits = SerialAccess::SPC_STOPB_UNDEFINED; break;
		}
		if (this->comPortState.fOutX && this->comPortState.fInX) {
			config.flowControl = SerialAccess::SPC_FLOW_XON_XOFF;
		} else if (this->comPortState.fOutxCtsFlow && !this->comPortState.fOutxDsrFlow) {
			config.flowControl = SerialAccess::SPC_FLOW_RTS_CTS;
		} else if (!this->comPortState.fOutxCtsFlow && this->comPortState.fOutxDsrFlow) {
			config.flowControl = SerialAccess::SPC_FLOW_DSR_DTR;
		} else {
			config.flowControl = SerialAccess::SPC_FLOW_UNDEFINED;
		}
		config.xonChar = this->comPortState.XonChar;
		config.xoffChar = this->comPortState.XoffChar;
		config.errorChar = 0;
		config.eofChar = 0;
		config.eventChar = 0;

		return true;
	}

	bool setBaud(unsigned long baud) override
	{
		if (!isOpen()) return false;
		if (!GetCommState(this->comPortHandle, &this->comPortState)) {
			printError("error %lu in SerialPort:setBaud:GetCommState: %s");
			return false;
		}
		this->comPortState.BaudRate = baud;
		if (!SetCommState(this->comPortHandle, &this->comPortState)) {
			printError("error %lu in SerialPort:setBaud:SetCommState: %s");
			return false;
		}
		return true;
	}

	unsigned long getBaud() override
	{
		if (this->comPortHandle == INVALID_HANDLE_VALUE) return 0;
		if (!GetCommState(this->comPortHandle, &this->comPortState)) {
			printError("error %lu in SerialPort:getBaud:GetCommState: %s");
			return 0;
		}
		return this->comPortState.BaudRate;
	}

	bool setTimeouts(int readTimeout, int readTimeoutInterval, int writeTimeout) override
	{
		if (!isOpen()) return false;
		if (!GetCommTimeouts(this->comPortHandle, &this->comPortTimeouts)) {
			printError("error %lu in SerialPort:setTimeouts:GetCommTimeouts: %s");
			return false;
		}

		if (readTimeout < 0) {
			// No timeout, but wait indefinitely* for at least one byte
			// When receiving a byte, wait additional readTimeoutInterval ms for another one before returning
			this->comPortTimeouts.ReadIntervalTimeout = readTimeoutInterval < 1 ? 1 : readTimeoutInterval;
			//																	^- interval = 0 causes read to block indefinetly because ... windows ...
			this->comPortTimeouts.ReadTotalTimeoutConstant = MAXULONG32;
			this->comPortTimeouts.ReadTotalTimeoutMultiplier = 0;
		} else {
			// Wait for readTimeout ms, then return no matter what has or has not been received
			// When receiving a byte, wait additional readTimeoutInterval ms for another one before returning
			this->comPortTimeouts.ReadTotalTimeoutConstant = readTimeout;
			this->comPortTimeouts.ReadIntervalTimeout = readTimeoutInterval < 0 ? 0 : readTimeoutInterval;
			this->comPortTimeouts.ReadTotalTimeoutMultiplier = 0;
		}
		this->comPortTimeouts.WriteTotalTimeoutConstant = writeTimeout < 0 ? 0 : writeTimeout;
		this->comPortTimeouts.WriteTotalTimeoutMultiplier = 0;

		if (!SetCommTimeouts(this->comPortHandle, &this->comPortTimeouts)) {
			printError("error %lu in SerialPort:setTimeouts:SetCommTimeouts: %s");
			return false;
		}
		return true;
	}

	bool getTimeouts(int* readTimeout, int* readTimeoutInterval, int* writeTimeout) override
	{
		if (!isOpen()) return false;
		if (!GetCommTimeouts(this->comPortHandle, &this->comPortTimeouts)) {
			printError("error %lu in SerialPort:setTimeouts:GetCommTimeouts: %s");
			return false;
		}

		*readTimeout = (int) this->comPortTimeouts.ReadTotalTimeoutConstant == MAXULONG32 ? -1 : this->comPortTimeouts.ReadTotalTimeoutConstant;
		*readTimeoutInterval = (int) this->comPortTimeouts.ReadIntervalTimeout;
		*writeTimeout = (int) this->comPortTimeouts.WriteTotalTimeoutConstant;
		return true;
	}

	long long int readBytes(char* buffer, unsigned long bufferCapacity, bool wait) override
	{
		if (this->comPortHandle == INVALID_HANDLE_VALUE) return 0;

		unsigned long receivedBytes;

		// Check if there is already an operation pending
		if (this->readOverlapped.hEvent == INVALID_HANDLE_VALUE) {

			// If not, initiate new operation

			// Create overlapped event
			ZeroMemory(&this->readOverlapped, sizeof(OVERLAPPED));
			this->readOverlapped.hEvent = this->readEventHandle;
			if (!ResetEvent(this->readEventHandle)) {
				printError("error %lu in SerialPort:readBytes:ResetEvent: %s");
				return -2;
			}

			// Initiate read operation
			if (!ReadFile(this->comPortHandle, buffer, bufferCapacity, &receivedBytes, &this->readOverlapped)) {

				// If not completed yet, check if error
				if (GetLastError() != ERROR_IO_PENDING) {
					printError("error %lu in SerialPort:readBytes:ReadFile: %s");
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
			printError("error %lu in SerialPort:readBytes:GetOverlappedResult: %s");
			return -2;
		}
		this->readOverlapped.hEvent = INVALID_HANDLE_VALUE;

		return receivedBytes;
	}

	long long int writeBytes(const char* buffer, unsigned long bufferLength, bool wait) override
	{
		if (this->comPortHandle == INVALID_HANDLE_VALUE) return 0;

		unsigned long writtenBytes;

		// Check if there is already an operation pending
		if (this->writeOverlapped.hEvent == INVALID_HANDLE_VALUE) {

			// If not, initiate new operation

			// Create overlapped event
			ZeroMemory(&this->writeOverlapped, sizeof(OVERLAPPED));
			this->writeOverlapped.hEvent = this->writeEventHandle;
			if (!ResetEvent(this->writeEventHandle)) {
				printError("error %lu in SerialPort:writeBytes:ResetEvent: %s");
				return -2;
			}

			// Initiate write operation
			if (!WriteFile(this->comPortHandle, buffer, bufferLength, &writtenBytes, &this->writeOverlapped)) {

				// If not completed yet, check if error
				if (GetLastError() != ERROR_IO_PENDING) {
					printError("error %lu in SerialPort:writeBytes:WriteFile: %s");
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
			printError("error %lu in SerialPort:writeBytes:GetOverlappedResult: %s");
			return -2;
		}
		this->writeOverlapped.hEvent = INVALID_HANDLE_VALUE;

		return writtenBytes;
	}

	bool getPortState(bool& dsr, bool& cts) override
	{
		if (!isOpen()) return false;

		DWORD state;
		if (!::GetCommModemStatus(this->comPortHandle, &state)) {
			printError("error %lu in SerialPort:getRawPortState:GetCommModemStatus: %s");
			return false;
		}

		dsr = state & MS_DSR_ON;
		cts = state & MS_CTS_ON;
		return true;
	}

	bool setManualPortState(bool dtr, bool rts) override
	{
		if (!isOpen()) return false;

		if (!::EscapeCommFunction(this->comPortHandle, dtr ? SETDTR : CLRDTR)) {
			printError("error %lu in SerialPort:setPortState:EscapeCommFunction(DTR): %s");
			return false;
		}

		if (!::EscapeCommFunction(this->comPortHandle, rts ? SETRTS : CLRRTS)) {
			printError("error %lu in SerialPort:setPortState:EscapeCommFunction(RTS): %s");
			return false;
		}

		return true;
	}

	bool waitForEvents(bool& comStateChange, bool& dataReceived, bool& dataTransmitted, bool wait) {
		if (!isOpen()) return false;

		// Check if the event mask was changed
		DWORD newMask = 0;
		if (comStateChange) newMask |= EV_CTS | EV_DSR;
		if (dataReceived) newMask |= EV_RXCHAR;
		if (dataTransmitted) newMask |= EV_TXEMPTY;
		if (newMask != eventMask && this->waitOverlapped.hEvent != INVALID_HANDLE_VALUE) {
			// Cancel previous wait (will be canceled if SetCommMask runs)
			this->waitOverlapped.hEvent = INVALID_HANDLE_VALUE;
		}
		eventMask = newMask;

		// Check if there is already an operation pending
		if (this->waitOverlapped.hEvent == INVALID_HANDLE_VALUE) {

			// If not, initiate new operation

			// If no event was requested, return
			if (!comStateChange && !dataReceived && !dataTransmitted) return true;

			// Configure event mask
			if (!::SetCommMask(this->comPortHandle, eventMask)) {
				printError("error %lu in SerialPort:waitForEvents:SetCommMask: %s");
				return false;
			}

			// Create overlapped event
			ZeroMemory(&this->waitOverlapped, sizeof(OVERLAPPED));
			this->waitOverlapped.hEvent = this->waitEventHandle;
			if (!ResetEvent(this->waitEventHandle)) {
				printError("error %lu in SerialPort:waitForEvents:ResetEvent: %s");
				return false;
			}

			// Initiate wait operation
			if (!WaitCommEvent(this->comPortHandle, &eventMaskReturned, &this->waitOverlapped)) {

				// If not completed yet, check if error
				if (GetLastError() != ERROR_IO_PENDING) {
					printError("error %lu in SerialPort:waitForEvents:WaitCommEvent: %s");
					return false;
				}

			} else {

				// Already completed, return results
				this->waitOverlapped.hEvent = INVALID_HANDLE_VALUE;
				comStateChange = eventMaskReturned & (EV_CTS | EV_DSR);
				dataReceived = eventMaskReturned & EV_RXCHAR;
				return true;

			}

		}

		comStateChange = false;
		dataReceived = false;
		dataTransmitted = false;

		// If not, wait for completition
		DWORD returnedBytes;
		if (!GetOverlappedResult(this->comPortHandle, &this->waitOverlapped, &returnedBytes, wait)) {
			if (GetLastError() == ERROR_IO_INCOMPLETE)
				return true; // not yet completed
			this->waitOverlapped.hEvent = INVALID_HANDLE_VALUE;
			if (GetLastError() == ERROR_OPERATION_ABORTED)
				return false; // port closed
			printError("error %lu in SerialPort:waitForEvents:GetOverlappedResult: %s");
			return false;
		}
		this->waitOverlapped.hEvent = INVALID_HANDLE_VALUE;

		comStateChange = eventMaskReturned & (EV_CTS | EV_DSR);
		dataReceived = eventMaskReturned & EV_RXCHAR;
		dataTransmitted = eventMaskReturned & EV_TXEMPTY;
		return true;
	}

	void abortWait() override
	{
		if (!isOpen() || this->waitOverlapped.hEvent == INVALID_HANDLE_VALUE) return;

		if (CancelIoEx(this->comPortHandle, &this->waitOverlapped)) {
			this->waitOverlapped.hEvent = INVALID_HANDLE_VALUE;
		}
	}

};

SerialAccess::SerialPort* SerialAccess::newSerialPort(const char* portFile) {
	return new SerialPortWin(portFile);
}

SerialAccess::SerialPort* SerialAccess::newSerialPortS(const std::string& portFile) {
	return new SerialPortWin(portFile.c_str());
}

#endif
