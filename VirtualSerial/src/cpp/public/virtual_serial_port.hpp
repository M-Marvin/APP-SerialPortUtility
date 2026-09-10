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

	virtual bool openPort() = 0;

	virtual void closePort() = 0;

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
	 * Returns the current timeouts of the virtual port.
	 * The port has to be open for this to work.
	 * NOTE: These are not the timeouts configured at the serial port, but the timeouts that apply for the application reading the Rx/Tx buffers.
	 * @param readTimeout Where to store the read timeout
	 * @param readTimeoutInterval Where to store the read timeout multiplier
	 * @param writeTimeout Where to store the write timeout
	 * @return true if the port was open and timeouts where read, false if the port was closed
	 */
	virtual bool getTimeouts(int* readTimeout, int* readTimeoutInterval, int* writeTimeout) = 0;

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
	 * NOTE: These are not the timeouts configured at the serial port, but the timeouts that apply for the application reading the Rx/Tx buffers.
	 * @param readTimeout The read timeout, if the requested amount of data is not received within this time, it returns with what it has
	 * @param readTimeoutInterval An additional timeout between received bytes
	 * @param writeTimeout The write timeout, if the supplied data could not be written within this time, it returns with the amount of data that could be written
	 * @return true if the timeouts where set, false if an error occurred
	 */
	virtual bool setTimeouts(int readTimeout, int readTimeoutInterval, int writeTimeout) = 0;

	/**
	 * Attempts to fill the buffer by reading bytes from the port.
	 * If not enough bytes could be read after the read timeout expires, the function returns with what was received.
	 *
	 * NOTE:
	 * If wait is false, the caller has to make sure the target buffer remains valid until the event finished.
	 * @param buffer The buffer to write the data to
	 * @param bufferCapacity The capacity of the buffer, aka the max number of bytes to read
	 * @param wait If true, the function blocks until the operation finished
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
	 * @param wait If true, the function blocks until the operation finished
	 * @return The number of bytes written, -1 if the event is still pending, a value less than -1 if an error occurred
	 */
	virtual long long int writeBytes(const char* buffer, unsigned long bufferLength, bool wait = true) = 0;

	/**
	 * Reads the current size of the internal buffers of the virtual port.
	 *
	 * @param txBufferSize The length in bytes of the transmission buffer
	 * @param rxBufferSize The length in bytes of the reception buffer
	 * @return true if the buffer sizes could be successfully read, false otherwise
	 */
	virtual bool getBufferSizes(unsigned long* txBufferSize, unsigned long* rxBufferSize) = 0;

	/**
	 * Sets the current size of the internal buffers of the virtual port.
	 * This will clear the current buffers.
	 *
	 * @param txBufferSize The length in bytes of the transmission buffer
	 * @param rxBufferSize The length in bytes of the reception buffer
	 * @return true if the buffer sizes could be successfully set, false otherwise
	 */
	virtual bool setBufferSizes(unsigned long txBufferSize, unsigned long rxBufferSize) = 0;

	/**
	 * Assigns the current pin input states of the serial port:
	 *
	 * Then input and output pins are:
	 * OUT    IN
	 * DTR -> DSR // flow control option 1
	 * RTS -> CTS // flow control option 2
	 */
	virtual bool setPortState(bool dsr, bool cts) = 0;

	/**
	 * Reads the current pin output states of the serial port:
	 *
	 * Then input and output pins are:
	 * OUT    IN
	 * DTR -> DSR // flow control option 1
	 * RTS -> CTS // flow control option 2
	 */
	virtual bool getPortState(bool& dtr, bool& rts) = 0;

	/**
	 * Waits for the requested events.
	 * The arguments are input and outputs at the same time.
	 * The function block until an event occurred, for which the argument was set to true.
	 * The before returning, the function overrides the values in the arguments to signal which events occured.
	 * @param configChange Configuration (baud, flow control, parity, etc) change event
	 * @param comStateChange COM state (DTR, RTS) change event
	 * @param dataReceived New data available event
	 * @param dataTransmitted All data was sent and the buffer is emtpy
	 * @param wait If true, the function will block until one of the events occurred
	 * @return false if the function returned because of an error, true otherwise
	 */
	virtual bool waitForEvents(bool& configChange, bool& comStateChange, bool& dataReceived, bool& dataTransmitted, bool wait = true) = 0;

	virtual void abortWait() = 0;

};

VirtualSerialPort* newVirtualSerialPort(const char* portFile);
VirtualSerialPort* newVirtualSerialPortS(const std::string& portFile);

}

#endif /* HEADER_VCOM_HPP_ */
