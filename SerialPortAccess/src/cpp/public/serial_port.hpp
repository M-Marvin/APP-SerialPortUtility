
#ifndef SERIAL_PORT_HPP_
#define SERIAL_PORT_HPP_

#include <string>
#include <vector>

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
	char xonChar;
	char xoffChar;
} SerialPortConfig;

static const SerialPortConfig DEFAULT_PORT_CONFIGURATION = {
	.baudRate = 9600,
	.dataBits = 8,
	.stopBits = SPC_STOPB_ONE,
	.parity = SPC_PARITY_NONE,
	.flowControl = SPC_FLOW_NONE,
	.xonChar = 17,
	.xoffChar = 19
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
	 * An readTimeout greater than zero means to wait until all requested characters have been received or time runs out.<br>
	 * An readTimeout of zero means no timeout, instant return with what is in the buffer even if no data is available.<br>
	 * An readTimeout of less than zero causes the read method to block until at least one character has arrived and/or is available in the buffer, then return with what is in the buffer.<br>
	 * An writeTimeout greater than zero means to wait until everything is written or the time runs out.<br>
	 * An writeTimeout of zero or less has means block until everything is written.<br>
	 * The readTimeoutInterval defines an additional timeout between the reception of individual bytes, it is started (and reset) when the first byte is received and times out if no  further byte is received within this time.<br>
	 * An readTimeoutInterval of zero or less indicates to not use this timeout and wait indefinitely (or until the normal timeout runs out)<br>
	 * Note that readTimeoutInterval might not be supported on all platforms, some might always behave as if it where zero.<br>
	 * In combination with readTimeout less than zero, readTimeoutInterval can act as an additional timeout after the first received byte (and all successive bytes).<br>
	 *
	 * The port has to be open for this to work.
	 * @param readTimeout The read timeout, if the requested amount of data is not received within this time, it returns with what it has
	 * @param readTimeoutInterval An additional timeout between received bytes
	 * @param writeTimeout The write timeout, if the supplied data could not be written within this time, it returns with the amount of data that could be written
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
	 *
	 * NOTE:
	 * If wait is false, the caller has to make sure the source buffer remains valid until the event finished.
	 * @param buffer The buffer to write the data to
	 * @param bufferCapacity The capacity of the buffer, aka the max number of bytes to read
	 * @return The number of bytes read, -1 if the event is still pending, a value less than -1 if an error occurred
	 */
	virtual long long int readBytes(char* buffer, unsigned long bufferCapacity, bool wait = true) = 0;

	/**
	 * Attempts to write the content of the buffer to the serial port.
	 * If not all data could be written until the write timeout expires, the function returns.
	 *
	 * NOTE:
	 * If wait is false, the caller has to make sure the target buffer remains valid until the event finished.
	 * @param buffer The buffer to read the data from
	 * @param bufferLength The length of the buffer, aka the number of bytes to write
	 * @return The number of bytes written, -1 if the event is still pending, a value less than -1 if an error occurred
	 */
	virtual long long int writeBytes(const char* buffer, unsigned long bufferLength, bool wait = true) = 0;
	
	/**
	 * Reads the logical values of the serial port pins DSR and CTS.
	 * @param dtrState The state of the DTR pin
	 * @param rtsState The state of the RTS pin
	 * @return true if the state was assigned successfully, false otherwise
	 */
	virtual bool getPortState(bool& dsr, bool& cts) = 0;

	/**
	 * Assigns the current pin output states of the serial port.
	 * Only effective if the signals are not controlled by hardware flow control.
	 * @param dtrState The state of the DTR pin
	 * @param rtsState The state of the RTS pin
	 * @return true if the state was assigned successfully, false otherwise
	 */
	virtual bool setManualPortState(bool dtrState, bool rtsState) = 0;

	/**
	 * Waits for the requested events.
	 * The arguments are input and outputs at the same time.
	 * The function block until an event occurred, for which the argument was set to true.
	 * The before returning, the function overrides the values in the arguments to signal which events occurred.
	 *
	 * NOTE:
	 * If wait is false, the function will always return immediately, but all event flags set to false until an event occurred.
	 * The wait operation will in this case continue until an event occurred and the event was read using this method.
	 * The wait operation is canceled and replaced by an new wait operation if this function is called with different event-arguments than the pending event.
	 * NOTE on dataTransmitted:
	 * This event behaves different on linux and windows, but in general, if it is signaled, more data can be written
	 * On windows, it is signaled once if the transmit buffer runs out of data.
	 * On linux, it continuously fires until the transmit buffer is completely full.
	 * @param comStateChange COM state (DSR, CTS) change event
	 * @param dataReceived data received event
	 * @param dataTransmitted transmission buffer is ready for new data
	 * @return false if the function returned because of an error, true otherwise
	 */
	virtual bool waitForEvents(bool& comStateChange, bool& dataReceived, bool& dataTransmitted, bool wait = true) = 0;

	/**
	 * Cancels an pending wait for events operation.
	 */
	virtual void abortWait() = 0;

};

SerialPort* newSerialPort(const char* portFile);
SerialPort* newSerialPortS(const std::string& portFile);

}

#endif
