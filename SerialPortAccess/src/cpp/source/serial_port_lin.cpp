#ifdef PLATFORM_LIN

#include "serial_port.hpp"
#include <thread>
#include <chrono>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <unistd.h>
#include <termios.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>

void printError(const char* format) {
	setbuf(stdout, NULL); // Work around for errors printed during JNI
	int errorCode = errno;
	if (errorCode == 0) return;
	printf(format, errorCode, strerror(errorCode));
}

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

class SerialPortLin : public SerialAccess::SerialPort {

private:
	struct termios comPortState;
	int comPortHandle;
	const char* portFileName;
	int rxTimeout = 0;
	int rxTimeoutInterval = 0;
	int txTimeout = 0;
	struct pollfd pollfdRx[2]; // rx, evt
	struct pollfd pollfdTx[2]; // tx, evt
	struct pollfd pollfdWait[3]; // rx, tx, evt;
	bool abortWaitFlag = false;

public:

	SerialPortLin(const char* portFile)
	{
		this->portFileName = portFile;
		this->comPortHandle = -1;
		this->pollfdTx[1].fd = eventfd(0, 0);
		this->pollfdTx[1].events = POLLIN;
		this->pollfdRx[1].fd = eventfd(0, 0);
		this->pollfdRx[1].events = POLLIN;
		this->pollfdWait[2].fd = eventfd(0, 0);
		this->pollfdWait[2].events = POLLIN;
	}

	~SerialPortLin() {
		closePort();
		::close(this->pollfdRx[1].fd);
		::close(this->pollfdTx[1].fd);
		::close(this->pollfdWait[1].fd);
	}

	bool openPort() override
	{
		if (this->comPortHandle >= 0) return false;
		this->comPortHandle = ::open(this->portFileName, O_RDWR);

		if (isOpen()) {
			this->pollfdRx[0].fd = this->comPortHandle;
			this->pollfdRx[0].events = POLLIN;
			this->pollfdTx[0].fd = this->comPortHandle;
			this->pollfdTx[0].events = POLLOUT;
			this->pollfdWait[0].fd = this->comPortHandle;
			this->pollfdWait[0].events = POLLIN;
			this->pollfdWait[1].fd = this->comPortHandle;
			this->pollfdWait[1].events = POLLOUT;

			setConfig(SerialAccess::DEFAULT_PORT_CONFIGURATION);
			setTimeouts(SerialAccess::DEFAULT_PORT_RX_TIMEOUT, SerialAccess::DEFAULT_PORT_RX_TIMEOUT_MULTIPLIER, SerialAccess::DEFAULT_PORT_TX_TIMEOUT);
			return true;
		}

		return false;
	}

	void closePort() override
	{
		if (!isOpen()) return;
		::close(this->comPortHandle);
		this->comPortHandle = -1;

		// trigger close event to release poll()
		unsigned long long val = 1;
		if (::write(this->pollfdRx[1].fd, (char*) &val, 8) == -1)
			printError("error %i in SerialPort:closePort:write(evtRx): %s\n");
		if (::write(this->pollfdTx[1].fd, (char*) &val, 8) == -1)
			printError("error %i in SerialPort:closePort:write(evtTx): %s\n");
		if (::write(this->pollfdWait[2].fd, (char*) &val, 8) == -1)
			printError("error %i in SerialPort:closePort:write(evtWait): %s\n");

	}

	bool isOpen() override
	{
		return this->comPortHandle >= 0;
	}

	bool setConfig(const SerialAccess::SerialPortConfig &config) override
	{
		if (!isOpen()) return false;

		if (::tcgetattr(this->comPortHandle, &this->comPortState) != 0) {
			if (errno == EBADF || errno == EIO) {
				closePort();
				return false;
			}
			printError("error %i in SerialPort:setConfig:tcgetattr: %s\n");
			return false;
		}

		// Default serial prot configuration from https://blog.mbedded.ninja/programming/operating-systems/linux/linux-serial-ports-using-c-cpp/
		this->comPortState.c_cflag &= ~CBAUD;
		this->comPortState.c_cflag &= ~PARENB; // Clear parity bit, disabling parity (most common)
		this->comPortState.c_cflag &= ~CSTOPB; // Clear stop field, only one stop bit used in communication (most common)
		this->comPortState.c_cflag |= CREAD | CLOCAL; // Turn on READ & ignore ctrl lines (CLOCAL = 1)
		this->comPortState.c_lflag &= ~ICANON; // Disable canonical mode
		this->comPortState.c_lflag &= ~ECHO; // Disable echo
		this->comPortState.c_lflag &= ~ECHOE; // Disable erasure
		this->comPortState.c_lflag &= ~ECHONL; // Disable new-line echo
		this->comPortState.c_lflag &= ~ISIG; // Disable interpretation of INTR, QUIT and SUSP
		this->comPortState.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL); // Disable any special handling of received bytes
		this->comPortState.c_oflag &= ~OPOST; // Prevent special interpretation of output bytes (e.g. newline chars)
		this->comPortState.c_oflag &= ~ONLCR; // Prevent conversion of newline to carriage return/line feed

		if (config.parity != SerialAccess::SPC_PARITY_NONE) {
			this->comPortState.c_cflag |= PARENB; // Enable parity
			if (config.parity == SerialAccess::SPC_PARITY_ODD) {
				this->comPortState.c_cflag |= PARODD;
			} else if (config.parity == SerialAccess::SPC_PARITY_EVEN) {
				this->comPortState.c_cflag &= ~PARODD;
			} else {
				return false; // mark/space parity not supported
			}
		} else {
			this->comPortState.c_cflag &= ~PARENB; // Disable parity
		}

		switch (config.flowControl) {
		case SerialAccess::SPC_FLOW_NONE:
			this->comPortState.c_cflag &= ~CRTSCTS; // Disable RTS/CTS
			this->comPortState.c_cflag &= ~IXON; // Disable XON
			this->comPortState.c_cflag &= ~IXOFF; // Disable XON
			break;
		case SerialAccess::SPC_FLOW_XON_XOFF:
			this->comPortState.c_cflag &= ~CRTSCTS; // Disable RTS/CTS
			this->comPortState.c_cflag |= IXON; // Enable XON
			this->comPortState.c_cflag |= IXOFF; // Enable XON
			break;
		case SerialAccess::SPC_FLOW_RTS_CTS:
			this->comPortState.c_cflag |= CRTSCTS; // Enable RTS/CTS
			this->comPortState.c_cflag &= ~IXON; // Disable XON
			this->comPortState.c_cflag &= ~IXOFF; // Disable XON
			break;
		default:
			return false; // RTS/DTS flow control not supported
		}

		this->comPortState.c_cflag &= ~CSIZE;
		switch (config.dataBits) {
		case 5: this->comPortState.c_cflag |= CS5; break;
		case 6: this->comPortState.c_cflag |= CS6; break;
		case 7:	this->comPortState.c_cflag |= CS7; break;
		case 8: this->comPortState.c_cflag |= CS8; break;
		default:
			return false; // data size not supported
		}

		if (config.stopBits == SerialAccess::SPC_STOPB_TWO) {
			this->comPortState.c_cflag |= CSTOPB; // Two stop bits
		} else if (config.stopBits == SerialAccess::SPC_STOPB_ONE) {
			this->comPortState.c_cflag &= ~CSTOPB; // One stop bit
		} else {
			printf("error one half stop bits not supported\n");
			return false;
		}

		int baudCfg = getBaudCfgValue(config.baudRate);
		if (baudCfg < 0 || ::cfsetspeed(&this->comPortState, baudCfg) != 0) {
			printError("error %i in SerialPort:setConfig:cfsetspeed: %s\n");
			return false;
		}

		this->comPortState.c_cc[VSTART] = config.xonChar;
		this->comPortState.c_cc[VSTOP] = config.xoffChar;

		// Save this->comPortHandle settings, also checking for error
		if (tcsetattr(this->comPortHandle, TCSANOW, &this->comPortState) != 0) {
			if (errno == EBADF || errno == EIO) {
				closePort();
				return false;
			}
			printf("error %i from tcsetattr: %s\n", errno, strerror(errno));
			return 1;
		}

		return true;
	}

	bool getConfig(SerialAccess::SerialPortConfig &config) override
	{
		if (!isOpen()) return false;

		if (::tcgetattr(this->comPortHandle, &this->comPortState) != 0) {
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

	bool setBaud(unsigned long baud) override
	{
		if (!isOpen()) return false;

		if (::tcgetattr(this->comPortHandle, &this->comPortState) != 0) {
			if (errno == EBADF || errno == EIO) {
				closePort();
				return false;
			}
			printError("error %i in SerialPort:setBaud:tcgetattr: %s\n");
			return false;
		}

		int baudCfg = getBaudCfgValue(baud);
		if (baudCfg < 0 || ::cfsetspeed(&this->comPortState, baudCfg) != 0) {
			printError("error %i in SerialPort:setBaud:cfsetspeed: %s\n");
			return false;
		}

		if (::tcsetattr(this->comPortHandle, TCSANOW, &this->comPortState) != 0) {
			if (errno == EBADF || errno == EIO) {
				closePort();
				return false;
			}
			printError("error %i in SerialPort:setBaud:tcsetattr: %s\n");
			return false;
		}

		return true;
	}

	unsigned long getBaud() override
	{
		if (!isOpen()) return 0;

		if (::tcgetattr(this->comPortHandle, &this->comPortState) != 0) {
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

	bool setTimeouts(int readTimeout, int readTimeoutInterval, int writeTimeout) override
	{
		if (!isOpen()) return false;

		if (::tcgetattr(this->comPortHandle, &this->comPortState) != 0) {
			if (errno == EBADF || errno == EIO) {
				closePort();
				return false;
			}
			printError("error %i in SerialPort:setTimeouts:tcgetattr: %s\n");
			return false;
		}

		if (readTimeout < 0) {
			// No timeout, but wait indefinitely for at least one byte
			// When receiving a byte, wait additonal readTimeoutInterval ms for another one before returning
			this->rxTimeout = -1;
			this->rxTimeoutInterval = readTimeoutInterval;
			this->comPortState.c_cc[VTIME] = readTimeoutInterval < 0 ? 0 : (unsigned char) (readTimeoutInterval / 100);
			this->comPortState.c_cc[VMIN] = 1;
		} else {
			// Wait for readTimeout ms, then return no matter what has or has not been received
			// When receiving a byte, wait additonal readTimeoutInterval ms for another one before returning
			this->rxTimeout = (unsigned char) (readTimeout / 100);
			this->rxTimeoutInterval = readTimeoutInterval;
			this->comPortState.c_cc[VTIME] = readTimeoutInterval < 0 ? 0 : (unsigned char) (readTimeoutInterval / 100);
			this->comPortState.c_cc[VMIN] = 0;
		}

		// Wait for writeTimeout ms for data to be send
		this->txTimeout = writeTimeout < 0 ? 0 : writeTimeout;

		if (::tcsetattr(this->comPortHandle, TCSANOW, &this->comPortState) != 0) {
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
		if (!isOpen()) return false;
		*readTimeout = this->rxTimeout;
		*readTimeoutInterval = this->rxTimeoutInterval;
		*writeTimeout = this->txTimeout;
		return true;
	}

	long long int readBytes(char* buffer, unsigned long bufferCapacity, bool wait) override
	{
		if (!isOpen()) return -2;

		// if an timeout is configured
		if (this->rxTimeout != 0) {

			// poll status of pending operation, return immediately if wait is false
			this->pollfdRx[0].revents = this->pollfdRx[1].revents = 0;
			int pollres = ::poll(this->pollfdRx, 2, wait ? this->rxTimeout : 0);
			if (pollres < 0) {
				return -2; // error
			} else if (pollres == 0) {
				return -1; // event still pending, but wait = false
			}

			// operation aborted, return error
			if (this->pollfdRx[0].revents == 0) return -2;

		}

		// perform actual read
		ssize_t receivedBytes = ::read(this->comPortHandle, buffer, bufferCapacity);
		if (receivedBytes < 0) return -2;
		return receivedBytes;
	}

	long long int writeBytes(const char* buffer, unsigned long bufferLength, bool wait) override
	{
		if (!isOpen()) return -2;

		// if an timeout is configured
		if (this->txTimeout != 0) {

			// poll status of pending operation, return immediately if wait is false
			this->pollfdTx[0].revents = this->pollfdTx[1].revents = 0;
			int pollres = ::poll(this->pollfdTx, 2, wait ? this->txTimeout : 0);
			if (pollres < 0) {
				return -2; // error
			} else if (pollres == 0) {
				return -1; // operation still pending, but wait = false
			}

			// operation aborted, return error
			if (this->pollfdTx[0].revents == 0) return -2;

		}

		// perform actual write
		ssize_t writtenBytes = ::write(this->comPortHandle, buffer, bufferLength);
		if (writtenBytes < 0) return -2;
		return writtenBytes;
	}

	bool getPortState(bool& dsr, bool& cts) override
	{
		if (!isOpen()) return false;

		int state = 0;
		if (::ioctl(this->comPortHandle, TIOCMGET, &state) == -1) {
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

	bool setManualPortState(bool dtr, bool rts) override
	{
		if (!isOpen()) return false;

		int state = 0;
		if (::ioctl(this->comPortHandle, TIOCMGET, &state) == -1) {
			if (errno == EBADF || errno == EIO) {
				closePort();
				return false;
			}
			printError("error %d in SerialPort:setManualPortState:ioctl(TIOCMGET): %s\n");
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

		if (::ioctl(this->comPortHandle, TIOCMSET, &state) == -1) {
			if (errno == EBADF || errno == EIO) {
				closePort();
				return false;
			}
			printError("error %d in SerialPort:setManualPortState:ioctl(TIOCMSET): %s\n");
			return false;
		}

		return true;
	}

#define PORT_STATE_POLL_INTERVAL 10

	bool waitForEvents(bool& comStateChange, bool& dataReceived, bool& dataTransmitted, bool wait) override
	{
		if (!isOpen()) return false;

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
					&& wait && isOpen() && !this->abortWaitFlag);

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

SerialAccess::SerialPort* SerialAccess::newSerialPort(const char* portFile) {
	return new SerialPortLin(portFile);
}

SerialAccess::SerialPort* SerialAccess::newSerialPortS(const std::string& portFile) {
	return new SerialPortLin(portFile.c_str());
}

#endif
