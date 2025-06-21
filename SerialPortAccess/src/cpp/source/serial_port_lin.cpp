#ifdef PLATFORM_LIN

#include "serial_port.hpp"
#include <thread>
#include <chrono>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>

void printError(const char* format) {
	setbuf(stdout, NULL);
	int errorCode = errno;
	if (errorCode == 0) return;
	printf(format, errorCode, strerror(errorCode));
}

unsigned int getBaudCfgValue(unsigned long baud) {
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
	default: return 0;
	}
}

unsigned long getBaudValue(unsigned int baudCfg) {
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
	default: return 0;
	}
}

class SerialPortLin : public SerialPort {

private:
	struct termios comPortState;
	int comPortHandle;
	const char* portFileName;

public:

	SerialPortLin(const char* portFile)
	{
		this->portFileName = portFile;
		this->comPortHandle = -1;
	}

	~SerialPortLin() {
		closePort();
	}

	bool setConfig(const SerialPortConfig &config) {
		if (this->comPortHandle < 0) return false;

		if (tcgetattr(this->comPortHandle, &this->comPortState) != 0) {
			printError("Error %i in setConfig:tcgetattr: %s\n");
			return false;
		}

		// Default serial prot configuration from https://blog.mbedded.ninja/programming/operating-systems/linux/linux-serial-ports-using-c-cpp/
		this->comPortState.c_cflag &= ~PARENB; // Clear parity bit, disabling parity (most common)
		this->comPortState.c_cflag &= ~CSTOPB; // Clear stop field, only one stop bit used in communication (most common)
		this->comPortState.c_cflag &= ~CSIZE; // Clear all the size bits, then use one of the statements below
		this->comPortState.c_cflag |= CS8; // 8 bits per byte (most common)
		this->comPortState.c_cflag &= ~CRTSCTS; // Disable RTS/CTS hardware flow control (most common)
		this->comPortState.c_cflag |= CREAD | CLOCAL; // Turn on READ & ignore ctrl lines (CLOCAL = 1)
		this->comPortState.c_lflag &= ~ICANON;
		this->comPortState.c_lflag &= ~ECHO; // Disable echo
		this->comPortState.c_lflag &= ~ECHOE; // Disable erasure
		this->comPortState.c_lflag &= ~ECHONL; // Disable new-line echo
		this->comPortState.c_lflag &= ~ISIG; // Disable interpretation of INTR, QUIT and SUSP
		this->comPortState.c_iflag &= ~(IXON | IXOFF | IXANY); // Turn off s/w flow ctrl
		this->comPortState.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL); // Disable any special handling of received bytes
		this->comPortState.c_oflag &= ~OPOST; // Prevent special interpretation of output bytes (e.g. newline chars)
		this->comPortState.c_oflag &= ~ONLCR; // Prevent conversion of newline to carriage return/line feed
		// Some fields might get overriden below depending on the configuration

		if (cfsetspeed(&this->comPortState, getBaudCfgValue(config.baudRate)) != 0) {
			printError("Error %i in setConfig:cfsetspeed: %s\n");
			return false;
		}

		if (config.parity != SPC_PARITY_NONE)
			this->comPortState.c_cflag |= PARENB; // Enable parity
		else {
			this->comPortState.c_cflag &= ~PARENB; // Disable parity
			if (config.parity == SPC_PARITY_ODD)
				this->comPortState.c_cflag |= PARODD;
			else if (config.parity == SPC_PARITY_EVEN)
				this->comPortState.c_cflag &= ~PARODD;
			else
				return false; // mark/space parity not supported
		}

		switch (config.flowControl) {
		case SPC_FLOW_XON_XOFF:
			this->comPortState.c_cflag |= CRTSCTS; // Disable RTS/CTS
			this->comPortState.c_cflag &= ~IXON; // Enable XON
			this->comPortState.c_cflag &= ~IXOFF; // Enable XON
			break;
		case SPC_FLOW_RTS_CTS:
			this->comPortState.c_cflag &= ~CRTSCTS; // Enable RTS/CTS
			this->comPortState.c_cflag |= IXON; // Disable XON
			this->comPortState.c_cflag |= IXOFF; // Disable XON
			break;
		default:
			return false; // RTS/DTS flow control not supported
		case SPC_PARITY_NONE:
			this->comPortState.c_cflag |= CRTSCTS; // Disable RTS/CTS
			this->comPortState.c_cflag |= IXON; // Disable XON
			this->comPortState.c_cflag |= IXOFF; // Disable XON
			break;
		}

		this->comPortState.c_cflag &= ~CSIZE;
		switch (config.dataBits) {
		case 5: this->comPortState.c_cflag |= CS5; break;
		case 6: this->comPortState.c_cflag |= CS6; break;
		case 7:	this->comPortState.c_cflag |= CS7; break;
		default:
			return false; // data size not supported
		case 8:this->comPortState.c_cflag |= CS8; break;
		}

		if (config.stopBits == SPC_STOPB_TWO)
			this->comPortState.c_cflag |= CSTOPB; // Two stop bits
		else if (config.stopBits == SPC_STOPB_TWO)
			this->comPortState.c_cflag &= ~CSTOPB; // One stop bit
		else {
			printf("Error one half stop bits not supported");
			this->comPortState.c_cflag &= ~CSTOPB; // One stop bit
		}

		if (tcsetattr(this->comPortHandle, TCSANOW, &this->comPortState) != 0) {
			printError("Error %i in setConfig:tcsetattr: %s\n");
			return false;
		}

		return true;
	}

	bool getConfig(SerialPortConfig &config) {
		if (this->comPortHandle < 0) return false;

		if (tcgetattr(this->comPortHandle, &this->comPortState) != 0) {
			printError("Error %i in getConfig:tcgetattr: %s\n");
			return false;
		}

		config.baudRate = getBaudValue(cfgetospeed(&this->comPortState));

		if (this->comPortState.c_cflag & PARENB) {
			config.parity = (this->comPortState.c_cflag & PARODD) ? SPC_PARITY_ODD : SPC_PARITY_EVEN;
		} else
			config.parity = SPC_PARITY_NONE;

		if ((this->comPortState.c_cflag & IXON) || (this->comPortState.c_cflag & IXOFF))
			config.flowControl = SPC_FLOW_XON_XOFF;
		else if (this->comPortState.c_cflag & CRTSCTS)
			config.flowControl = SPC_FLOW_RTS_CTS;
		else
			config.flowControl = SPC_FLOW_NONE;

		switch (config.dataBits & CSIZE) {
		case CS5: config.dataBits = 5; break;
		case CS6: config.dataBits = 6; break;
		case CS7: config.dataBits = 7; break;
		default:
		case CS8: config.dataBits = 8; break;
		}

		config.stopBits = (this->comPortState.c_cflag & CSTOPB) ? SPC_STOPB_TWO : SPC_STOPB_ONE;

		return true;
	}

	bool openPort()
	{
		if (this->comPortHandle >= 0) return false;
		this->comPortHandle = open(this->portFileName, O_RDWR);

		if (isOpen()) {
			setConfig(DEFAULT_PORT_CONFIGURATION);
			return true;
		}

		return false;
	}

	void closePort()
	{
		if (this->comPortHandle < 0) return;
		close(this->comPortHandle);
		this->comPortHandle = -1;
	}

	bool isOpen()
	{
		return this->comPortHandle >= 0;
	}

	bool setBaud(unsigned long baud)
	{
		if (this->comPortHandle < 0) return false;

		if (tcgetattr(this->comPortHandle, &this->comPortState) != 0) {
			printError("Error %i in setBaud:tcgetattr: %s\n");
			return false;
		}

		if (cfsetspeed(&this->comPortState, getBaudCfgValue(baud)) != 0) {
			printError("Error %i in setBaud:cfsetspeed: %s\n");
			return false;
		}

		if (tcsetattr(this->comPortHandle, TCSANOW, &this->comPortState) != 0) {
			printError("Error %i in setBaud:tcsetattr: %s\n");
			return false;
		}

		return true;
	}

	unsigned long getBaud()
	{
		if (this->comPortHandle < 0) return 0;

		if (tcgetattr(this->comPortHandle, &this->comPortState) != 0) {
			printError("Error %i in getBaud:tcgetattr: %s\n");
			return 0;
		}

		return getBaudValue(cfgetospeed(&this->comPortState));
	}

	bool setTimeouts(unsigned int readTimeout, unsigned int writeTimeout)
	{
		if (this->comPortHandle < 0) return false;

		if (tcgetattr(this->comPortHandle, &this->comPortState) != 0) {
			printError("Error %i in setTimeouts:tcgetattr: %s\n");
			return false;
		}

		this->comPortState.c_cc[VMIN] = 0;
		this->comPortState.c_cc[VTIME] = (unsigned char) (readTimeout / 100); // Convert ms in ds

		if (tcsetattr(this->comPortHandle, TCSANOW, &this->comPortState) != 0) {
			printError("Error %i in setTimeouts:tcsetattr: %s\n");
			return false;
		}

		return true;
	}

	unsigned long readBytes(char* buffer, unsigned long bufferCapacity)
	{
		if (this->comPortHandle < 0) return 0;
		unsigned long receivedBytes = read(this->comPortHandle, buffer, bufferCapacity);
		return receivedBytes;
	}

	unsigned long readBytesConsecutive(char* buffer, unsigned long bufferCapacity, unsigned int consecutiveDelay, unsigned int receptionWaitTimeout)
	{
		if (this->comPortHandle < 0) return 0;
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

	unsigned long writeBytes(const char* buffer, unsigned long bufferLength)
	{
		if (this->comPortHandle < 0) return 0;
		unsigned long writtenBytes = write(this->comPortHandle, buffer, bufferLength);
		return writtenBytes;
	}

};

SerialPort* newSerialPort(const char* portFile) {
	return new SerialPortLin(portFile);
}

SerialPort* newSerialPort(const std::string& portFile) {
	return new SerialPortLin(portFile.c_str());
}

#endif
