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
	 * Zero means no timeout (warning, this might cause read methods on closed ports to block indefinitely).
	 * The port has to be open for this to work.
	 * @param readTimeout The read timeout, if the requested amount of data is not received within this time, it returns with what it has (might be zero)
	 * @param writeTimeout The write timeout, if the supplied data could not be written within this time, it returns with the ammount of data that could be written (might be zero)
	 * @return true if the timeouts where set, false if an error occurred
	 */
	virtual bool setTimeouts(unsigned int readTimeout, unsigned int writeTimeout) = 0;

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
	 * The consecutive delay is implemented as delay between the read operations.
	 * This means that the application will always wait for the configured ammount of time before attempting to read the next bytes.
	 * So the value should be held small enough to not cause a buffer overflow.
	 *
	 * NOTE
	 * The reception wait timeout can not be lower than the read timeout of the port, lower values will simply mean to use the read timeout
	 * The actual timeout will also always be round up to the next multiple of the read timeout.
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
