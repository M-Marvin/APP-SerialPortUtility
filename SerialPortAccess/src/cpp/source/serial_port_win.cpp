
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
	HANDLE writeEventHandle;
	HANDLE readEventHandle;
	HANDLE comPortHandle;
	const char* portFileName;

public:

	SerialPortWin(const char* portFile)
	{
		this->portFileName = portFile;
		this->comPortHandle = INVALID_HANDLE_VALUE;
		this->comPortState = {0};
		this->comPortTimeouts = {0};
		this->writeEventHandle = INVALID_HANDLE_VALUE;
		this->readEventHandle = INVALID_HANDLE_VALUE;
	}

	~SerialPortWin() {
		closePort();
	}

	bool setConfig(const SerialAccess::SerialPortConfig &config) override
	{
		if (this->comPortHandle == INVALID_HANDLE_VALUE) return false;

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
		this->comPortState.XonChar = 17;
		this->comPortState.XoffChar = 19;
		this->comPortState.ErrorChar = 0;
		this->comPortState.EofChar = 0;
		this->comPortState.EvtChar = 0;

		if (!SetCommState(this->comPortHandle, &this->comPortState)) {
			printError("error %lu in SerialPort:setConfig:SetCommState: %s");
			return false;
		}

		return true;
	}

	bool getConfig(SerialAccess::SerialPortConfig &config) override
	{
		if (this->comPortHandle == INVALID_HANDLE_VALUE) return false;

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

		return true;
	}

	bool openPort() override
	{
		if (this->comPortHandle != INVALID_HANDLE_VALUE) return false;
		this->comPortHandle = CreateFileA(this->portFileName, GENERIC_WRITE | GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

		if (!isOpen())
			return false;

		if (!SetCommMask(this->comPortHandle, EV_RXCHAR)) {
			printError("error %lu in SerialPort:openPort:SetCommMask: %s");
			closePort();
			return false;
		}

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
		this->comPortHandle = INVALID_HANDLE_VALUE;
		this->writeEventHandle = NULL;
		this->readEventHandle = NULL;
	}

	bool isOpen() override
	{
		return this->comPortHandle != INVALID_HANDLE_VALUE;
	}

	bool setBaud(unsigned long baud) override
	{
		if (this->comPortHandle == INVALID_HANDLE_VALUE) return false;
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
		if (this->comPortHandle == INVALID_HANDLE_VALUE) return false;
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
		if (this->comPortHandle == INVALID_HANDLE_VALUE) return false;
		if (!GetCommTimeouts(this->comPortHandle, &this->comPortTimeouts)) {
			printError("error %lu in SerialPort:setTimeouts:GetCommTimeouts: %s");
			return false;
		}

		*readTimeout = (int) this->comPortTimeouts.ReadTotalTimeoutConstant == MAXULONG32 ? -1 : this->comPortTimeouts.ReadTotalTimeoutConstant;
		*readTimeoutInterval = (int) this->comPortTimeouts.ReadIntervalTimeout;
		*writeTimeout = (int) this->comPortTimeouts.WriteTotalTimeoutConstant;
		return true;
	}

	unsigned long readBytes(char* buffer, unsigned long bufferCapacity) override
	{
		if (this->comPortHandle == INVALID_HANDLE_VALUE) return 0;

		// Create overlapped event
		ZeroMemory(&this->readOverlapped, sizeof(OVERLAPPED));
		this->readOverlapped.hEvent = this->readEventHandle;
		if (!ResetEvent(this->readEventHandle)) {
			printError("error %lu in SerialPort:readBytes:ResetEvent: %s");
			return 0;
		}

		// Initiate read operation
		unsigned long receivedBytes;
		if (!ReadFile(this->comPortHandle, buffer, bufferCapacity, &receivedBytes, &this->readOverlapped)) {

			// If not completed yet, check if error
			if (GetLastError() != ERROR_IO_PENDING) {
				printError("error %lu in SerialPort:readBytes:ReadFile: %s");
				return 0;
			}

			// If not, wait for completition
			if (!GetOverlappedResult(this->comPortHandle, &this->readOverlapped, &receivedBytes, TRUE)) {
				if (GetLastError() == ERROR_OPERATION_ABORTED)
					return 0; // port closed
				printError("error %lu in SerialPort:readBytes:GetOverlappedResult: %s");
				return 0;
			}

		}

		return receivedBytes;
	}

	unsigned long writeBytes(const char* buffer, unsigned long bufferLength) override
	{
		if (this->comPortHandle == INVALID_HANDLE_VALUE) return 0;

		// Create overlapped event
		ZeroMemory(&this->writeOverlapped, sizeof(OVERLAPPED));
		this->writeOverlapped.hEvent = this->writeEventHandle;
		if (!ResetEvent(this->writeEventHandle)) {
			printError("error %lu in SerialPort:writeBytes:ResetEvent: %s");
			return 0;
		}

		// Initiate write operation
		unsigned long writtenBytes;
		if (!WriteFile(this->comPortHandle, buffer, bufferLength, &writtenBytes, &this->writeOverlapped)) {

			// If not completed yet, check if error
			if (GetLastError() != ERROR_IO_PENDING) {
				printError("error %lu in SerialPort:writeBytes:WriteFile: %s");
				return 0;
			}

			// If not, wait for completition
			if (!GetOverlappedResult(this->comPortHandle, &this->writeOverlapped, &writtenBytes, TRUE)) {
				if (GetLastError() == ERROR_OPERATION_ABORTED)
					return 0; // port closed
				printError("error %lu in SerialPort:writeBytes:GetOverlappedResult: %s");
				return 0;
			}

		}

		return writtenBytes;
	}

	bool getRawPortState(bool& dsr, bool& cts) override
	{
		if (this->comPortHandle == INVALID_HANDLE_VALUE) return false;

		DWORD state;
		if (!::GetCommModemStatus(this->comPortHandle, &state)) {
			printError("error %lu in SerialPort:getRawPortState:GetCommModemStatus: %s");
			return false;
		}

		dsr = state & MS_DSR_ON;
		cts = state & MS_CTS_ON;
		return true;
	}

	bool setRawPortState(bool dtr, bool rts) override
	{
		if (this->comPortHandle == INVALID_HANDLE_VALUE) return false;

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

	bool getFlowControl(bool& readyState) override
	{
		if (this->comPortHandle == INVALID_HANDLE_VALUE) return false;

		if (!GetCommState(this->comPortHandle, &this->comPortState)) {
			printError("error %lu in SerialPort:getFlowControl:GetCommState: %s");
			return false;
		}

		bool dsrState = false;
		bool ctsState = false;
		if (!getRawPortState(dsrState, ctsState))
			return false;

		if (this->comPortState.fOutxCtsFlow)
			readyState = ctsState;
		if (this->comPortState.fOutxDsrFlow)
			readyState = dsrState;

		return true;
	}

	bool setFlowControl(bool readyState) override
	{
		if (this->comPortHandle == INVALID_HANDLE_VALUE) return false;

		if (!GetCommState(this->comPortHandle, &this->comPortState)) {
			printError("error %lu in SerialPort:setFlowControl:GetCommState: %s");
			return false;
		}

		bool dtrState = true;
		bool rtsState = true;
		if (this->comPortState.fOutxCtsFlow)
			rtsState = readyState;
		if (this->comPortState.fOutxDsrFlow)
			dtrState = readyState;

		return setRawPortState(dtrState, rtsState);
	}

};

SerialAccess::SerialPort* SerialAccess::newSerialPort(const char* portFile) {
	return new SerialPortWin(portFile);
}

SerialAccess::SerialPort* SerialAccess::newSerialPortS(const std::string& portFile) {
	return new SerialPortWin(portFile.c_str());
}

#endif
