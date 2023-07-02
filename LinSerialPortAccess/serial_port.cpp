
#include "serial_port.h"
#include <thread>
#include <chrono>
#include <stdio.h>
#include <string.h>
SerialPort::SerialPort(const char* portFile)
{
	this->portFileName = portFile;
	this->comPortHandle = -1;
}

bool SerialPort::openPort()
{
	if (comPortHandle >= 0) return false;
	comPortHandle = open(portFileName, O_RDWR);
	if (!isOpen()) {
		printf("Error %i from serial port: %s\n", comPortHandle, strerror(comPortHandle));
		return false;
	}
	return true;
}

void SerialPort::closePort()
{
	if (comPortHandle < 0) return;
	close(comPortHandle);
	comPortHandle = -1;
}

bool SerialPort::isOpen()
{
	return comPortHandle >= 0;
}

void SerialPort::setBaud(int baud)
{
	if (comPortHandle < 0) return;
	int errorN = 0;
	if ((errorN = tcgetattr(comPortHandle, &comPortState)) != 0) {
		printf("Error %i from serial port: %s\n", errno, strerror(errno));
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

	cfsetspeed(&comPortState, baudSelection);

	if ((errorN = tcsetattr(comPortHandle, TCSANOW, &comPortState)) != 0) {
		printf("Error %i from serial port: %s\n", errno, strerror(errno));
	}
}

int SerialPort::getBaud()
{
	if (comPortHandle < 0) return -1;
	int errorN = 0;
	if ((errorN = tcgetattr(comPortHandle, &comPortState)) != 0) {
		printf("Error %i from serial port: %s\n", errno, strerror(errno));
		return -1;
	}

	int baud;
	switch (cfgetospeed(&comPortState)) {
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
			baud = -1;
			printf("Baud set to an unknown value!");
		}
	}

	return baud;
}

void SerialPort::setTimeouts(int readTimeout, int writeTimeout)
{
	if (comPortHandle < 0) return;
	int errorN = 0;
	if ((errorN = tcgetattr(comPortHandle, &comPortState)) != 0) {
		printf("Error %i from serial port: %s\n", errno, strerror(errno));
	}

	comPortState.c_cc[VMIN] = 0;
	comPortState.c_cc[VTIME] = (unsigned char) (readTimeout / 100); // Convert ms in ds

	if ((errorN = tcsetattr(comPortHandle, TCSANOW, &comPortState)) != 0) {
		printf("Error %i from serial port: %s\n", errno, strerror(errno));
	}
}

unsigned long SerialPort::readBytes(char* buffer, unsigned long bufferCapacity)
{
	if (comPortHandle < 0) return -1;
	unsigned long receivedBytes = read(comPortHandle, &buffer, bufferCapacity);
	return receivedBytes;
}

unsigned long SerialPort::readBytesBurst(char* buffer, unsigned long bufferCapacity, long long receptionLoopDelay)
{
	if (comPortHandle < 0) return -1;
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
	if (comPortHandle < 0) return -1;
	unsigned long writtenBytes = write(comPortHandle, buffer, bufferLength);
	return writtenBytes;
}
