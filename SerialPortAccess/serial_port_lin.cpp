#ifdef PLATFORM_LINUX

#include "serial_port.h"
#include <thread>
#include <chrono>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>

// FIXME update code

struct SerialPortImplData {
	struct termios comPortState;
	int comPortHandle;
	const char* portFileName;
};

SerialPort::SerialPort(const char* portFile)
{
	this->implData = new SerialPortImplData();
	this->implData->portFileName = portFile;
	this->implData->comPortHandle = -1;
}

bool SerialPort::openPort()
{
	if (this->implData->comPortHandle >= 0) return false;
	this->implData->comPortHandle = open(this->implData->portFileName, O_RDWR);
	if (!isOpen()) {
		printf("Error %i from openPort: %s\n", errno, strerror(errno));
		return false;
	}

	if (tcgetattr(this->implData->comPortHandle, &this->implData->comPortState) != 0) {
		printf("Error %i from openPortCfg: %s\n", errno, strerror(errno));
		return true;
	}

	// Default serial prot configuration from https://blog.mbedded.ninja/programming/operating-systems/linux/linux-serial-ports-using-c-cpp/
	this->implData->comPortState.c_cflag &= ~PARENB; // Clear parity bit, disabling parity (most common)
	this->implData->comPortState.c_cflag &= ~CSTOPB; // Clear stop field, only one stop bit used in communication (most common)
	this->implData->comPortState.c_cflag &= ~CSIZE; // Clear all the size bits, then use one of the statements below
	this->implData->comPortState.c_cflag |= CS8; // 8 bits per byte (most common)
	this->implData->comPortState.c_cflag &= ~CRTSCTS; // Disable RTS/CTS hardware flow control (most common)
	this->implData->comPortState.c_cflag |= CREAD | CLOCAL; // Turn on READ & ignore ctrl lines (CLOCAL = 1)
	this->implData->comPortState.c_lflag &= ~ICANON;
	this->implData->comPortState.c_lflag &= ~ECHO; // Disable echo
	this->implData->comPortState.c_lflag &= ~ECHOE; // Disable erasure
	this->implData->comPortState.c_lflag &= ~ECHONL; // Disable new-line echo
	this->implData->comPortState.c_lflag &= ~ISIG; // Disable interpretation of INTR, QUIT and SUSP
	this->implData->comPortState.c_iflag &= ~(IXON | IXOFF | IXANY); // Turn off s/w flow ctrl
	this->implData->comPortState.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL); // Disable any special handling of received bytes
	this->implData->comPortState.c_oflag &= ~OPOST; // Prevent special interpretation of output bytes (e.g. newline chars)
	this->implData->comPortState.c_oflag &= ~ONLCR; // Prevent conversion of newline to carriage return/line feed

	if (tcsetattr(this->implData->comPortHandle, TCSANOW, &this->implData->comPortState) != 0) {
		printf("Error %i from openPortCfg: %s\n", errno, strerror(errno));
	}

	return true;
}

void SerialPort::closePort()
{
	if (this->implData->comPortHandle < 0) return;
	close(this->implData->comPortHandle);
	this->implData->comPortHandle = -1;
}

bool SerialPort::isOpen()
{
	return this->implData->comPortHandle >= 0;
}

void SerialPort::setBaud(int baud)
{
	if (this->implData->comPortHandle < 0) return;
	if (tcgetattr(this->implData->comPortHandle, &this->implData->comPortState) != 0) {
		printf("Error %i from setBaud: %s\n", errno, strerror(errno));
		return;
	}

	int baudSelection = 0;
	switch (baud) {
	case 0: baudSelection = B0; break;
	case 50: baudSelection = B50; break;
	case 75: baudSelection = B75; break;
	case 110: baudSelection = B110; break;
	case 134: baudSelection = B134; break;
	case 150: baudSelection = B150; break;
	case 200: baudSelection = B200; break;
	case 300: baudSelection = B300; break;
	case 600: baudSelection = B600; break;
	case 1200: baudSelection = B1200; break;
	case 1800: baudSelection = B1800; break;
	case 2400: baudSelection = B2400; break;
	case 4800: baudSelection = B4800; break;
	case 9600: baudSelection = B9600; break;
	case 19200: baudSelection = B19200; break;
	case 38400: baudSelection = B38400; break;
	default: {
			baudSelection = B9600;
			printf("Baud speed %i not supported, selected default %i!\n", baud, 9600);
		}
	}

	cfsetspeed(&this->implData->comPortState, baudSelection);

	if (tcsetattr(this->implData->comPortHandle, TCSANOW, &this->implData->comPortState) != 0) {
		printf("Error %i from setBaud: %s\n", errno, strerror(errno));
	}
}

int SerialPort::getBaud()
{
	if (this->implData->comPortHandle < 0) return 0;
	if (tcgetattr(this->implData->comPortHandle, &this->implData->comPortState)!= 0) {
		printf("Error %i from getBaud: %s\n", errno, strerror(errno));
		return 0;
	}

	int baud;
	switch (cfgetospeed(&this->implData->comPortState)) {
	case B0: baud = 0; break;
	case B50: baud = 50; break;
	case B75: baud = 75; break;
	case B110: baud = 110; break;
	case B134: baud = 134; break;
	case B150: baud = 150; break;
	case B200: baud = 200; break;
	case B300: baud = 300; break;
	case B600: baud = 600; break;
	case B1200: baud = 1200; break;
	case B1800: baud = 1800; break;
	case B2400: baud = 2400; break;
	case B4800: baud = 4800; break;
	case B9600: baud = 9600; break;
	case B19200: baud = 19200; break;
	case B38400: baud = 38400; break;
	default: {
			baud = 0;
			printf("Baud set to an unknown value!");
		}
	}

	return baud;
}

void SerialPort::setTimeouts(int readTimeout, int writeTimeout)
{
	if (this->implData->comPortHandle < 0) return;
	if (tcgetattr(this->implData->comPortHandle, &this->implData->comPortState) != 0) {
		printf("Error %i from setTimeouts: %s\n", errno, strerror(errno));
	}

	this->implData->comPortState.c_cc[VMIN] = 0;
	this->implData->comPortState.c_cc[VTIME] = (unsigned char) (readTimeout / 100); // Convert ms in ds

	if (tcsetattr(this->implData->comPortHandle, TCSANOW, &this->implData->comPortState) != 0) {
		printf("Error %i from setTimeouts: %s\n", errno, strerror(errno));
	}
}

unsigned long SerialPort::readBytes(char* buffer, unsigned long bufferCapacity)
{
	if (this->implData->comPortHandle < 0) return 0;
	unsigned long receivedBytes = read(this->implData->comPortHandle, buffer, bufferCapacity);
	return receivedBytes;
}

unsigned long SerialPort::readBytesConsecutive(char* buffer, unsigned long bufferCapacity, long long consecutiveDelay, long long receptionWaitTimeout)
{
	if (this->implData->comPortHandle < 0) return 0;
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
	if (this->implData->comPortHandle < 0) return 0;
	unsigned long writtenBytes = write(this->implData->comPortHandle, buffer, bufferLength);
	return writtenBytes;
}

#endif
