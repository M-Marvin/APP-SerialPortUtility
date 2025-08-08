#pragma once

#include <string>

namespace SerialAccess {

typedef enum SerialPortParity {
	SPC_PARITY_NONE = 1,
	SPC_PARITY_ODD = 2,
	SPC_PARITY_EVEN = 3,
	SPC_PARITY_MARK = 4,
	SPC_PARITY_SPACE = 5,
	SPC_PARITY_UNDEFINED = 0
} SerialPortParity;

enum SerialPortFlowControl {
	SPC_FLOW_NONE = 1,
	SPC_FLOW_XON_XOFF = 2,
	SPC_FLOW_RTS_CTS = 3,
	SPC_FLOW_DSR_DTR = 4,
	SPC_FLOW_UNDEFINED = 0
};

enum SerialPortStopBits {
	SPC_STOPB_ONE = 1,
	SPC_STOPB_ONE_HALF = 2,
	SPC_STOPB_TWO = 3,
	SPC_STOPB_UNDEFINED = 0
};

typedef struct SerialPortConfiguration {
	unsigned long baudRate;
	unsigned char dataBits;
	SerialPortStopBits stopBits;
	SerialPortParity parity;
	SerialPortFlowControl flowControl;
} SerialPortConfig;

static const SerialPortConfig DEFAULT_PORT_CONFIGURATION = {
	.baudRate = 9600,
	.dataBits = 8,
	.stopBits = SPC_STOPB_ONE,
	.parity = SPC_PARITY_NONE,
	.flowControl = SPC_FLOW_NONE
};

static const int DEFAULT_PORT_RX_TIMEOUT = -1;
static const int DEFAULT_PORT_RX_TIMEOUT_MULTIPLIER = 0;
static const int DEFAULT_PORT_TX_TIMEOUT = 100;

class SerialPort
{

public:
	virtual ~SerialPort() {};

	/**
	 * Applies the supplied configuration to the port.
	 * The port has to be open for this to work.
	 * @param config The configuration struct to apply
	 * @return true if the configuration was set, false if an error occurred
	 */
	virtual bool setConfig(const SerialPortConfig &config) = 0;

	/**
	 * Reads the current configuration of the port.
	 * The port has to be open for this to work.
	 * @param config The configuration struct to write the configuration to
	 * @return true if the configuration was read, false if an error occurred
	 */
	virtual bool getConfig(SerialPortConfig &config) = 0;

	/**
	 * Sets the baud rate for the port, this is equal to doing it using setConfig().
	 * The port has to be open for this to work.
	 * @param baud The baud rate to set for the port
	 * @return true if the baud was set, false if an error occurred
	 */
	virtual bool setBaud(unsigned long baud) = 0;

	/**
	 * Gets the current baud rate configured for the port, equal to doing it using getConfig().
	 * The port has to be open for this to work.
	 * @return The current baud configured for the port, or 0 if the port is not open
	 */
	virtual unsigned long getBaud() = 0;

	/**
	 * Sets the read write timeouts for the port.
	 * An readTimeout of zero means no timeout. (instant return if no data is available)
	 * An readTimeout of less than zero causes the read method to block until at least one character has arrived.
	 * An writeTimeout of zero or less has means block until everything is sent.
	 * The readTimeoutInterval defines an additional timeout that is appended to each received byte.
	 * The port has to be open for this to work.
	 * @param readTimeout The read timeout, if the requested amount of data is not received within this time, it returns with what it has (might be zero)
	 * @param readTimeoutInterval An additional timeout that is waited for after each received byte, only has an effect if readTimeout >= 0.
	 * @param writeTimeout The write timeout, if the supplied data could not be written within this time, it returns with the amount of data that could be written (might be zero)
	 * @return true if the timeouts where set, false if an error occurred
	 */
	virtual bool setTimeouts(int readTimeout, int readTimeoutInterval, int writeTimeout) = 0;

	/**
	 * Returns the current timeouts of the serial port.
	 * The port has to be open for this to work.
	 * @param readTimeout Where to store the read timeout
	 * @param readTimeoutInterval Where to store the read timeout multiplier
	 * @param writeTimeout Where to store the write timeout
	 * @return true if the port was open and timeouts where read, false if the port was closed
	 */
	virtual bool getTimeouts(int* readTimeout, int* readTimeoutInterval, int* writeTimeout) = 0;

	/**
	 * Attempt to claim/open the port.
	 * This might fail if the port was not found or is already claimed by an another process.
	 * @return true if the port was successfully opened, false otherwise
	 */
	virtual bool openPort() = 0;

	/**
	 * Closes the port.
	 * If the port is already closed, this has no affect.
	 */
	virtual void closePort() = 0;

	/**
	 * Returns true if the port is open and can be written to or read from.
	 * @return true if the port is open, false otherwise
	 */
	virtual bool isOpen() = 0;

	/**
	 * Attempts to fill the buffer by reading bytes from the port.
	 * If not enough bytes could be read after the read timeout expires, the function returns with what was received.
	 * @param buffer The buffer to write the data to
	 * @param bufferCapacity The capacity of the buffer, aka the max number of bytes to read
	 * @return The number of bytes read
	 */
	virtual unsigned long readBytes(char* buffer, unsigned long bufferCapacity) = 0;

	/**
	 * Attempts to fill the buffer by reading bytes from the port.
	 * The difference to the normal read is, that the timeout is extended each time a byte is received.
	 * This is useful if the data arrives in random intervals, such as user input.
	 *
	 * NOTE
	 * This function is an convenience method for doing this between normal reading operations.
	 * This function temporary changes the timeout configuration of the port using the supplied values.
	 * If this is the only required behavior, the same should be done using setTimeouts and readBytes.
	 *
	 * @param buffer The buffer to write the data to
	 * @param bufferCapacity The capacity of the buffer, aka the max number of bytes to read
	 * @param consecutiveDelay The timeout to add when data was received
	 * @param receptionWaitTimeout The minimum time to wait for the first byte
	 * @return The number of bytes read
	 */
	virtual unsigned long readBytesConsecutive(char* buffer, unsigned long bufferCapacity, unsigned int consecutiveDelay, unsigned int receptionWaitTimeout) = 0;

	/**
	 * Attempts to write the content of the buffer to the serial port.
	 * If not all data could be written until the write timeout expires, the function returns.
	 * @param buffer The buffer to read the data from
	 * @param bufferLength The length of the buffer, aka the number of bytes to write
	 * @return The number of bytes written
	 */
	virtual unsigned long writeBytes(const char* buffer, unsigned long bufferLength) = 0;
	
};

SerialPort* newSerialPort(const char* portFile);
SerialPort* newSerialPortS(const std::string& portFile);

}
