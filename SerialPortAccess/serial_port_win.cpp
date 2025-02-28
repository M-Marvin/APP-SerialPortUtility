
#ifdef PLATFORM_WIN

#include "serial_port.hpp"
#include <thread>
#include <chrono>
#include <stdio.h>
#include <windows.h>

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

void SerialPort::setConfig(const SerialPortConfig &config) {
	if (this->implData->comPortHandle == INVALID_HANDLE_VALUE) return;

	GetCommState(this->implData->comPortHandle, &this->implData->comPortState);
	this->implData->comPortState.BaudRate = config.baudRate;
	this->implData->comPortState.fBinary = TRUE;
	this->implData->comPortState.fParity = (config.parity != SPC_PARITY_NONE);
	this->implData->comPortState.fOutxCtsFlow = (config.flowControl == SPC_FLOW_RTS_CTS);
	this->implData->comPortState.fOutxDsrFlow = (config.flowControl == SPC_FLOW_DSR_DTR);
	this->implData->comPortState.fDtrControl = (config.flowControl == SPC_FLOW_DSR_DTR) ? DTR_CONTROL_ENABLE : DTR_CONTROL_DISABLE;
	this->implData->comPortState.fDsrSensitivity = (config.flowControl == SPC_FLOW_DSR_DTR);
	this->implData->comPortState.fTXContinueOnXoff = (config.flowControl == SPC_FLOW_NONE);
	this->implData->comPortState.fOutX = (config.flowControl == SPC_FLOW_XON_XOFF);
	this->implData->comPortState.fInX = (config.flowControl == SPC_FLOW_XON_XOFF);
	this->implData->comPortState.fErrorChar = 0;
	this->implData->comPortState.fNull = 0;
	this->implData->comPortState.fRtsControl = (config.flowControl == SPC_FLOW_RTS_CTS) ? RTS_CONTROL_TOGGLE : RTS_CONTROL_ENABLE;
	this->implData->comPortState.fAbortOnError = 0;
	this->implData->comPortState.XonLim = 2048;
	this->implData->comPortState.XoffLim = 512;
	this->implData->comPortState.ByteSize = config.dataBits;
	switch (config.parity) {
	case SPC_PARITY_NONE: this->implData->comPortState.Parity = NOPARITY; break;
	case SPC_PARITY_ODD: this->implData->comPortState.Parity = ODDPARITY; break;
	case SPC_PARITY_EVEN: this->implData->comPortState.Parity = EVENPARITY; break;
	case SPC_PARITY_MARK: this->implData->comPortState.Parity = MARKPARITY; break;
	case SPC_PARITY_SPACE: this->implData->comPortState.Parity = SPACEPARITY; break;
	default: break;
	}
	switch (config.stopBits) {
	case SPC_STOPB_ONE: this->implData->comPortState.StopBits = ONESTOPBIT; break;
	case SPC_STOPB_ONE_HALF: this->implData->comPortState.StopBits = ONE5STOPBITS; break;
	case SPC_STOPB_TWO: this->implData->comPortState.StopBits = TWOSTOPBITS; break;
	default: break;
	}
	this->implData->comPortState.XonChar = 17;
	this->implData->comPortState.XoffChar = 19;
	this->implData->comPortState.ErrorChar = 0;
	this->implData->comPortState.EofChar = 0;
	this->implData->comPortState.EvtChar = 0;
	SetCommState(this->implData->comPortHandle, &this->implData->comPortState);

}

void SerialPort::getConfig(SerialPortConfig &config) {
	if (this->implData->comPortHandle == INVALID_HANDLE_VALUE) return;

	GetCommState(this->implData->comPortHandle, &this->implData->comPortState);
	config.baudRate = this->implData->comPortState.BaudRate;
	if (this->implData->comPortState.fParity == 0) {
		config.parity = SPC_PARITY_NONE;
	} else {
		switch (this->implData->comPortState.Parity) {
		case NOPARITY: config.parity = SPC_PARITY_NONE; break;
		case ODDPARITY: config.parity = SPC_PARITY_ODD; break;
		case EVENPARITY: config.parity = SPC_PARITY_EVEN; break;
		case MARKPARITY: config.parity = SPC_PARITY_MARK; break;
		case SPACEPARITY: config.parity = SPC_PARITY_SPACE; break;
		default: config.parity = SPC_PARITY_UNDEFINED; break;
		}
	}
	config.dataBits = this->implData->comPortState.ByteSize;
	switch (this->implData->comPortState.StopBits) {
	case ONESTOPBIT: config.stopBits = SPC_STOPB_ONE; break;
	case ONE5STOPBITS: config.stopBits = SPC_STOPB_ONE_HALF; break;
	case TWOSTOPBITS: config.stopBits = SPC_STOPB_TWO; break;
	default: config.stopBits = SPC_STOPB_UNDEFINED; break;
	}
	if (this->implData->comPortState.fOutX && this->implData->comPortState.fInX) {
		config.flowControl = SPC_FLOW_XON_XOFF;
	} else if (this->implData->comPortState.fOutxCtsFlow && !this->implData->comPortState.fOutxDsrFlow) {
		config.flowControl = SPC_FLOW_RTS_CTS;
	} else if (!this->implData->comPortState.fOutxCtsFlow && this->implData->comPortState.fOutxDsrFlow) {
		config.flowControl = SPC_FLOW_DSR_DTR;
	} else {
		config.flowControl = SPC_FLOW_UNDEFINED;
	}
}

bool SerialPort::openPort()
{
	if (this->implData->comPortHandle != INVALID_HANDLE_VALUE) return false;
	this->implData->comPortHandle = CreateFileA(this->implData->portFileName, GENERIC_WRITE | GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);

	if (isOpen()) {
		setConfig(DEFAULT_PORT_CONFIGURATION);
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

void SerialPort::setBaud(unsigned long baud)
{
	if (this->implData->comPortHandle == INVALID_HANDLE_VALUE) return;
	GetCommState(this->implData->comPortHandle, &this->implData->comPortState);
	this->implData->comPortState.BaudRate = baud;
	SetCommState(this->implData->comPortHandle, &this->implData->comPortState);
}

int SerialPort::getBaud()
{
	if (this->implData->comPortHandle == INVALID_HANDLE_VALUE) return 0;
	GetCommState(this->implData->comPortHandle, &this->implData->comPortState);
	return this->implData->comPortState.BaudRate;
}

void SerialPort::setTimeouts(unsigned int readTimeout, unsigned int writeTimeout)
{
	GetCommTimeouts(this->implData->comPortHandle, &this->implData->comPortTimeouts);
	this->implData->comPortTimeouts.ReadIntervalTimeout = readTimeout == 0 ? MAXDWORD : 0;
	this->implData->comPortTimeouts.ReadTotalTimeoutConstant = readTimeout;
	this->implData->comPortTimeouts.ReadTotalTimeoutMultiplier = 0;
	this->implData->comPortTimeouts.WriteTotalTimeoutConstant = writeTimeout;
	this->implData->comPortTimeouts.WriteTotalTimeoutMultiplier = 0;
	SetCommTimeouts(this->implData->comPortHandle, &this->implData->comPortTimeouts);
}

unsigned long SerialPort::readBytes(char* buffer, unsigned long bufferCapacity)
{
	if (this->implData->comPortHandle == INVALID_HANDLE_VALUE) return 0;
	unsigned long receivedBytes = 0;
	if (!ReadFile(this->implData->comPortHandle, buffer, bufferCapacity, &receivedBytes, NULL)) return 0;
	return receivedBytes;
}

unsigned long SerialPort::readBytesConsecutive(char* buffer, unsigned long bufferCapacity, unsigned int consecutiveDelay, unsigned int receptionWaitTimeout)
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
		std::this_thread::sleep_for(std::chrono::milliseconds(consecutiveDelay));
		unsigned int lastReceived = readBytes(buffer + receivedBytes, bufferCapacity - receivedBytes);
		if (lastReceived == 0) break;
		receivedBytes += lastReceived;
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
