/*
 * serial_port.h
 *
 *  Created on: 02.07.2023
 *      Author: marvin
 */

#ifndef SERIAL_PORT_H_
#define SERIAL_PORT_H_

#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>

class SerialPort {

protected:
	struct termios comPortState;
	int comPortHandle;
	const char* portFileName;

public:
	SerialPort(const char* portFile);
	void setBaud(int baud);
	int getBaud();
	void setTimeouts(int readTimeout, int writeTimeout);
	bool openPort();
	void closePort();
	bool isOpen();
	unsigned long readBytes(char* buffer, unsigned long bufferCapacity);
	unsigned long readBytesConsecutive(char* buffer, unsigned long bufferCapacity, long long consecutiveDelay, long long receptionWaitTimeout);
	unsigned long writeBytes(const char* buffer, unsigned long bufferLength);

};

#endif /* SERIAL_PORT_H_ */
