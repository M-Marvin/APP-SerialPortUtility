/*
 * virtual_serial_port_lin.cpp
 *
 *  Created on: 08.09.2026
 *      Author: marvi
 */

#define _XOPEN_SOURCE 600
#include "virtual_serial_port.hpp"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
//#include <asm/termios.h>
#include <termios.h>
#include <poll.h>
#include <sys/eventfd.h>

void printError(const char* format) {
	setbuf(stdout, NULL); // Work around for errors printed during JNI
	int errorCode = errno;
	if (errorCode == 0) return;
	printf(format, errorCode, strerror(errorCode));
}

/* taken from serial port access serial_port_lin.cpp */
int getBaudCfgValue(int baud) {
	switch (baud) {
	case 0: return B0; break;
	case 50: return B50; break;
	case 75: return B75; break;
	case 110: return B110; break;
	case 134: return B134; break;
	case 150: return B150; break;
	case 200: return B200; break;
	case 300: return B300; break;
	case 600: return B600; break;
	case 1200: return B1200; break;
	case 1800: return B1800; break;
	case 2400: return B2400; break;
	case 4800: return B4800; break;
	case 9600: return B9600; break;
	case 19200: return B19200; break;
	case 38400: return B38400; break;
	case 57600: return B57600; break;
	case 115200: return B115200; break;
	case 230400: return B230400; break;
	case 460800: return B460800; break;
	case 500000: return B500000; break;
	case 576000: return B576000; break;
	case 921600: return B921600; break;
	case 1000000: return B1000000; break;
	case 1152000: return B1152000; break;
	case 1500000: return B1500000; break;
	case 2000000: return B2000000; break;
	case 2500000: return B2500000; break;
	case 3000000: return B3000000; break;
	case 3500000: return B3500000; break;
	case 4000000: return B4000000; break;
	default: return -1;
	}
}

/* taken from serial port access serial_port_lin.cpp */
int getBaudValue(unsigned int baudCfg) {
	switch (baudCfg) {
	case B0: return 0; break;
	case B50: return 50; break;
	case B75: return 75; break;
	case B110: return 110; break;
	case B134: return 134; break;
	case B150: return 150; break;
	case B200: return 200; break;
	case B300: return 300; break;
	case B600: return 600; break;
	case B1200: return 1200; break;
	case B1800: return 1800; break;
	case B2400: return 2400; break;
	case B4800: return 4800; break;
	case B9600: return 9600; break;
	case B19200: return 19200; break;
	case B38400: return 38400; break;
	case B57600: return 57600; break;
	case B115200: return 115200; break;
	case B230400: return 230400; break;
	case B460800: return 460800; break;
	case B500000: return 500000; break;
	case B576000: return 576000; break;
	case B921600: return 921600; break;
	case B1000000: return 1000000; break;
	case B1152000: return 1152000; break;
	case B1500000: return 1500000; break;
	case B2000000: return 2000000; break;
	case B2500000: return 2500000; break;
	case B3000000: return 3000000; break;
	case B3500000: return 3500000; break;
	case B4000000: return 4000000; break;
	default: return -1;
	}
}

#define PTY_PATH_LEN_LIMIT 64U

class VirtualSerialPortLin : public SerialAccess::VirtualSerialPort
{

private:
	struct termios comPortState;
	int ptyFileDescriptor;
	char ptyFileName[PTY_PATH_LEN_LIMIT] {0};
	const char* portFileName;

	int rxTimeout = 0;
	int rxTimeoutInterval = 0; // NOTE currently not supported by linux, value just stored
	int txTimeout = 0;

	struct pollfd pollfdRx[2]; // rx, evt
	struct pollfd pollfdTx[2]; // tx, evt
	struct pollfd pollfdWait[3]; // rx, tx, evt;
	bool abortWaitFlag = false;

public:
	VirtualSerialPortLin(const char* portFile)
	{
		this->portFileName = portFile;
		this->ptyFileDescriptor = -1;

	}

	~VirtualSerialPortLin() {
		closePort();
	}

	bool openPort() override
	{
		if (isCreated()) return false;

		this->ptyFileDescriptor = ::posix_openpt(O_RDWR | O_NOCTTY);
		if (this->ptyFileDescriptor == -1) {
			printError("error %i in VirtualSerialPort:posix_openpt: %s\n");
			return false;
		}

		if (::grantpt(this->ptyFileDescriptor) != 0) {
			printError("error %i in VirtualSerialPort:grantpt: %s\n");
			closePort();
			return false;
		}

		if (::unlockpt(this->ptyFileDescriptor) != 0) {
			printError("error %i in VirtualSerialPort:unlockpt: %s\n");
			closePort();
			return false;
		}

		if (::ptsname_r(this->ptyFileDescriptor, this->ptyFileName, PTY_PATH_LEN_LIMIT) != 0) {
			printError("error %i in VirtualSerialPort:ptsname_r: %s\n");
			closePort();
			return false;
		}

		// force null termination
		this->ptyFileName[PTY_PATH_LEN_LIMIT - 1] = 0;

		::unlink(this->portFileName);
		if (::symlink(this->ptyFileName, this->portFileName) != 0) {
			printError("error %i in VirtualSerialPort:symlink: %s\n");
			closePort();
			return false;
		}

		return true;
	}

	void closePort() override
	{
		if (!isCreated()) return;

		::close(this->ptyFileDescriptor);
		::unlink(this->portFileName);
		this->ptyFileDescriptor = -1;
	}

	bool isCreated() override
	{
		return this->ptyFileDescriptor != -1;
	}

	bool getConfig(SerialAccess::SerialPortConfiguration &config) override
	{
		if (!isCreated()) return false;

		/* taken from serial port access serial_port_lin.cpp */
		if (::tcgetattr(this->ptyFileDescriptor, &this->comPortState) != 0) {
			if (errno == EBADF || errno == EIO) {
				closePort();
				return false;
			}
			printError("error %i in SerialPort:getConfig:tcgetattr: %s\n");
			return false;
		}

		int baudRate = getBaudValue(cfgetospeed(&this->comPortState));
		config.baudRate = baudRate < 0 ? 0 : baudRate;

		if (this->comPortState.c_cflag & PARENB) {
			config.parity = (this->comPortState.c_cflag & PARODD) ? SerialAccess::SPC_PARITY_ODD : SerialAccess::SPC_PARITY_EVEN;
		} else
			config.parity = SerialAccess::SPC_PARITY_NONE;

		if ((this->comPortState.c_cflag & IXON) || (this->comPortState.c_cflag & IXOFF))
			config.flowControl = SerialAccess::SPC_FLOW_XON_XOFF;
		else if (this->comPortState.c_cflag & CRTSCTS)
			config.flowControl = SerialAccess::SPC_FLOW_RTS_CTS;
		else
			config.flowControl = SerialAccess::SPC_FLOW_NONE;

		switch (config.dataBits & CSIZE) {
		case CS5: config.dataBits = 5; break;
		case CS6: config.dataBits = 6; break;
		case CS7: config.dataBits = 7; break;
		default:
		case CS8: config.dataBits = 8; break;
		}

		config.stopBits = (this->comPortState.c_cflag & CSTOPB) ? SerialAccess::SPC_STOPB_TWO : SerialAccess::SPC_STOPB_ONE;

		config.xonChar = this->comPortState.c_cc[VSTART];
		config.xoffChar = this->comPortState.c_cc[VSTOP];

		return true;
	}

	unsigned long getBaud() override
	{
		if (!isCreated()) return false;

		/* taken from serial port access serial_port_lin.cpp */
		if (::tcgetattr(this->ptyFileDescriptor, &this->comPortState) != 0) {
			if (errno == EBADF || errno == EIO) {
				closePort();
				return false;
			}
			printError("error %i in SerialPort:getBaud:tcgetattr: %s\n");
			return 0;
		}

		int baudRate = getBaudValue(cfgetospeed(&this->comPortState));
		return baudRate < 0 ? 0 : baudRate;
	}

	// FIXME buffer size
	bool getBufferSizes(unsigned long* txBufferSize, unsigned long* rxBufferSize) override
	{
		*txBufferSize = 1024;
		*rxBufferSize = 1024;
		return true;
	}

	bool setBufferSizes(unsigned long txBufferSize, unsigned long rxBufferSize) override
	{
		return true;
	}

	bool getPortState(bool& dsr, bool& cts) override
	{
		if (!isCreated()) return false;

		/* taken from serial port access serial_port_lin.cpp */
		int state = 0;
		if (::ioctl(this->ptyFileDescriptor, TIOCMGET, &state) == -1) {
			if (errno == EBADF || errno == EIO) {
				closePort();
				return false;
			}
			printError("error %d in SerialPort:getPortState:ioctl(TIOCMGET): %s\n");
			return false;
		}

		dsr = state & TIOCM_DSR;
		cts = state & TIOCM_CTS;
		return true;
	}

	bool setPortState(bool dtr, bool rts) override
	{
		if (!isCreated()) return false;

		/* taken from serial port access serial_port_lin.cpp */
		int state = 0;
		if (::ioctl(this->ptyFileDescriptor, TIOCMGET, &state) == -1) {
			if (errno == EBADF || errno == EIO) {
				closePort();
				return false;
			}
			printError("error %d in SerialPort:setPortState:ioctl(TIOCMGET): %s\n");
			return false;
		}

		if (dtr)
			state |= TIOCM_DTR;
		else
			state &= ~TIOCM_DTR;

		// do not override hardware flow control if enabled
		if (this->comPortState.c_cflag & CRTSCTS == 0) {
			if (rts)
				state |= TIOCM_RTS;
			else
				state &= ~TIOCM_RTS;
		}

		if (::ioctl(this->ptyFileDescriptor, TIOCMSET, &state) == -1) {
			if (errno == EBADF || errno == EIO) {
				closePort();
				return false;
			}
			printError("error %d in SerialPort:setPortState:ioctl(TIOCMSET): %s\n");
			return false;
		}

		return true;
	}

	bool setTimeouts(int readTimeout, int readTimeoutInterval, int writeTimeout) override
	{
		if (!isCreated()) return false;

		if (::tcgetattr(this->ptyFileDescriptor, &this->comPortState) != 0) {
			if (errno == EBADF || errno == EIO) {
				closePort();
				return false;
			}
			printError("error %i in SerialPort:setTimeouts:tcgetattr: %s\n");
			return false;
		}

		if (readTimeout < 0) {
			// No timeout, but wait indefinitely for at least one byte
			// When receiving a byte, wait additional readTimeoutInterval ms for another one before returning
			this->rxTimeout = -1;
			this->rxTimeoutInterval = readTimeoutInterval; // NOTE currently not supported by linux, value just stored
			this->comPortState.c_cc[VTIME] = readTimeoutInterval < 0 ? 0 : (unsigned char) (readTimeoutInterval / 100);
			this->comPortState.c_cc[VMIN] = 1;
		} else {
			// Wait for readTimeout ms, then return no matter what has or has not been received
			// When receiving a byte, wait additonal readTimeoutInterval ms for another one before returning
			this->rxTimeout = (unsigned char) (readTimeout / 100);
			this->rxTimeoutInterval = readTimeoutInterval; // NOTE currently not supported by linux, value just stored
			this->comPortState.c_cc[VTIME] = readTimeoutInterval < 0 ? 0 : (unsigned char) (readTimeoutInterval / 100);
			this->comPortState.c_cc[VMIN] = 0;
		}

		// Wait for writeTimeout ms for data to be send
		this->txTimeout = writeTimeout < 0 ? 0 : writeTimeout;

		if (::tcsetattr(this->ptyFileDescriptor, TCSANOW, &this->comPortState) != 0) {
			if (errno == EBADF || errno == EIO) {
				closePort();
				return false;
			}
			printError("error %i in SerialPort:setTimeouts:tcsetattr: %s\n");
			return false;
		}

		return true;
	}

	bool getTimeouts(int* readTimeout, int* readTimeoutInterval, int* writeTimeout) override
	{
		if (!isCreated()) return false;
		*readTimeout = this->rxTimeout;
		*readTimeoutInterval = this->rxTimeoutInterval;
		*writeTimeout = this->txTimeout;
		return true;
	}

	long long int readBytes(char* buffer, unsigned long bufferCapacity, bool wait = true) override
	{
		if (!isCreated()) return false;

		// if an timeout is configured
		if (this->rxTimeout != 0) {

			// poll status of pending operation, return immediately if wait is false
			this->pollfdRx[0].revents = this->pollfdRx[1].revents = 0;
			int pollres = ::poll(this->pollfdRx, 2, wait ? this->rxTimeout : 0);
			if (pollres < 0) {
				closePort();
				return -2; // error
			} else if (pollres == 0) {
				return -1; // event still pending, but wait = false
			}

			// operation aborted, return error
			if (this->pollfdRx[0].revents == 0) {
				closePort();
				return -2;
			}

		}

		// perform actual read
		ssize_t receivedBytes = ::read(this->ptyFileDescriptor, buffer, bufferCapacity);
		if (receivedBytes < 0) {
			closePort();
			return -2;
		}
		return receivedBytes;
	}

	long long int writeBytes(const char* buffer, unsigned long bufferLength, bool wait = true) override
	{
		if (!isCreated()) return false;

		// if an timeout is configured
		if (this->txTimeout != 0) {

			// poll status of pending operation, return immediately if wait is false
			this->pollfdTx[0].revents = this->pollfdTx[1].revents = 0;
			int pollres = ::poll(this->pollfdTx, 2, wait ? this->txTimeout : 0);
			if (pollres < 0) {
				closePort();
				return -2; // error
			} else if (pollres == 0) {
				return -1; // operation still pending, but wait = false
			}

			// operation aborted, return error
			if (this->pollfdTx[0].revents == 0) {
				closePort();
				return -2;
			}

		}

		// perform actual write
		ssize_t writtenBytes = ::write(this->ptyFileDescriptor, buffer, bufferLength);
		if (writtenBytes < 0) {
			closePort();
			return -2;
		}
		return writtenBytes;
	}

#define PORT_STATE_POLL_INTERVAL 10

	// FIXME configChange
	bool waitForEvents(bool& configChange, bool& comStateChange, bool& dataReceived, bool& dataTransmitted, bool wait = true) override
	{
		if (!isCreated()) return false;

		// read current com port state
		bool dsrState, ctsState;
		if (!getPortState(dsrState, ctsState)) return false;

		// enable read event
		this->pollfdWait[0].events = dataReceived ? POLLIN : 0;
		// enable write event
		this->pollfdWait[1].events = dataTransmitted ? POLLOUT : 0;

		bool comStateEvent = false, dataReceiveEvent = false, dataTransmitEvent = false;
		this->abortWaitFlag = false;
		do {

			// wait for read/write events for interval
			this->pollfdWait[0].revents = this->pollfdTx[1].revents = this->pollfdTx[2].revents = 0;
			int pollres = ::poll(this->pollfdWait, 3, wait ? PORT_STATE_POLL_INTERVAL : 0);
			if (pollres < 0) return false;

			// check dsr cts state
			bool dsrNew, ctsNew;
			if (!getPortState(dsrNew, ctsNew)) return false;
			if (dsrNew != dsrState || ctsNew != ctsState) {
				comStateEvent = true;
			}

			if (pollres > 0) {
				if (this->pollfdWait[0].revents > 0) dataReceiveEvent = true;
				if (this->pollfdWait[1].revents > 0) dataTransmitEvent = true;
			}

		} while (	(!comStateEvent || !comStateChange) &&
					(!dataReceiveEvent || !dataReceived) &&
					(!dataTransmitEvent || !dataTransmitted)
					&& wait && isCreated() && !this->abortWaitFlag);

		comStateChange = comStateEvent;
		dataReceived = dataReceiveEvent;
		dataTransmitted = dataTransmitEvent;
		return true;
	}

	void abortWait() override
	{
		// signal event wait loop to exit
		this->abortWaitFlag = true;
	}

};

SerialAccess::VirtualSerialPort* SerialAccess::newVirtualSerialPort(const char* portFile) {
	return new VirtualSerialPortLin(portFile);
}

SerialAccess::VirtualSerialPort* SerialAccess::newVirtualSerialPortS(const std::string& portFile) {
	return new VirtualSerialPortLin(portFile.c_str());
}
