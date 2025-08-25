/*
 * vcom.hpp
 *
 *  Created on: 21.08.2025
 *      Author: marvi
 */

#ifndef HEADER_VCOM_HPP_
#define HEADER_VCOM_HPP_

#include <serial_port.hpp>

namespace SerialAccess {

class VirtualSerialPort
{

public:
	virtual ~VirtualSerialPort() {};

	virtual bool createPort() = 0;

	virtual void removePort() = 0;

	virtual bool isCreated() = 0;

	/**
	 * Reads the current configuration of the port.
	 * The port has to be open for this to work.
	 * @param config The configuration struct to write the configuration to
	 * @return true if the configuration was read, false if an error occurred
	 */
	virtual bool getConfig(SerialPortConfig &config) = 0;

	/**
	 * Gets the current baud rate configured for the port, equal to doing it using getConfig().
	 * The port has to be open for this to work.
	 * @return The current baud configured for the port, or 0 if the port is not open
	 */
	virtual unsigned long getBaud() = 0;

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
	 * Attempts to fill the buffer by reading bytes from the port.
	 * If not enough bytes could be read after the read timeout expires, the function returns with what was received.
	 * @param buffer The buffer to write the data to
	 * @param bufferCapacity The capacity of the buffer, aka the max number of bytes to read
	 * @return The number of bytes read
	 */
	virtual unsigned long readBytes(char* buffer, unsigned long bufferCapacity) = 0;

	/**
	 * Attempts to write the content of the buffer to the serial port.
	 * If not all data could be written until the write timeout expires, the function returns.
	 * @param buffer The buffer to read the data from
	 * @param bufferLength The length of the buffer, aka the number of bytes to write
	 * @return The number of bytes written
	 */
	virtual unsigned long writeBytes(const char* buffer, unsigned long bufferLength) = 0;

	/**
	 * Assigns the current pin input states of the serial port:
	 *
	 * Then input and output pins are:
	 * OUT    IN
	 * DTR -> DSR // flow control option 1
	 * RTS -> CTS // flow control option 2
	 */
	virtual bool setManualPortState(bool dsr, bool cts) = 0;

	/**
	 * Reads the current pin output states of the serial port:
	 *
	 * Then input and output pins are:
	 * OUT    IN
	 * DTR -> DSR // flow control option 1
	 * RTS -> CTS // flow control option 2
	 */
	virtual bool getManualPortState(bool& dtr, bool& rts) = 0;

	/**
	 * Waits for the requested events.
	 * The arguments are input and outputs at the same time.
	 * The function block until an event occurred, for which the argument was set to true.
	 * The before returning, the function overrides the values in the arguments to signal which events occured.
	 * @param configChange Configuration (baud, flow control, parity, etc) change event
	 * @param timeoutChange Timeout change event
	 * @param comStateChange COM state (DTR, RTS) change event
	 * @param dataReady New data available event
	 * @return false if the function returned because of an error, true otherwise
	 */
	virtual bool waitForEvents(bool& configChange, bool& timeoutChange, bool& comStateChange, bool& dataReady) = 0;

};

}

#endif /* HEADER_VCOM_HPP_ */
