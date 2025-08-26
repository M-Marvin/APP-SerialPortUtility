/*
 * soeconnection.h
 *
 * Defines the functions and classes as well as default configurations for Serial Over Ethernet.
 *
 *  Created on: 04.02.2025
 *      Author: Marvin Koehler (M_Marvin)
 */

#ifndef SOE_CONNECTION_HPP_
#define SOE_CONNECTION_HPP_

#include <netsocket.hpp>
#include <serial_port.hpp>
#include <thread>
#include <map>
#include <shared_mutex>
#include <string>
#include <condition_variable>
#include <functional>
#include "ringbuffer.hpp"

namespace SerialOverEthernet {

#define SOE_TCP_DEFAULT_SOE_PORT 26U											// default server port
#define SOE_TCP_FRAME_MAX_LEN 256UL												// max package length
#define SOE_TCP_FRAME_LEN_BYTES 3												// length of package length field
#define SOE_TCP_PROTO_IDENT_LEN 4												// length of package identifier
#define SOE_TCP_PROTO_IDENT 0x534F4950U											// package identifier
#define SOE_TCP_HANDSHAKE_TIMEOUT 4000UL										// timeout for handshake operations and initial connection
#define SOE_TCP_HEADER_LEN (SOE_TCP_PROTO_IDENT_LEN + SOE_TCP_FRAME_LEN_BYTES)	// length of the package header
#define SOE_SERIAL_BUFFER_LEN (SOE_TCP_FRAME_MAX_LEN - SOE_TCP_HEADER_LEN) - 1	// max length of received serial data for one package
#define SOE_TCP_STREAM_BUFFER_LEN 512UL											// ring buffer capacity for received data to transmit over serial

class SOELinkHandler {

public:

	/**
	 * Creates a new client network connection handler
	 * @param socket The socket of the client-server connection
	 * @param onDeath A callback invoked when the connection was closed
	 */
	SOELinkHandler(NetSocket::Socket* socket, std::string& hostName, std::string& hostPort, std::function<void(SOELinkHandler*)> onDeath);

	/**
	 * Closes all ports and the socket and cleans all allocated buffer memory
	 */
	virtual ~SOELinkHandler();

	/**
	 * Attempts to open the local serial port.
	 * @param localSerial The serial port file name
	 * @return true if the port as opened successfully, false otherwise
	 */
	virtual bool openLocalPort(const std::string& localSerial) = 0;
	/**
	 * Attempts to open the remote serial port.
	 * @param localSerial The serial port file name
	 * @return true if the port as opened successfully, false otherwise
	 */
	bool openRemotePort(const std::string& remoteSerial);

	/**
	 * Attempts to apply the serial port configuration to the remote port
	 * @param remoteConfig The serial port configuration
	 * @return true if the configuration was applied successfully, false otherwise
	 */
	virtual bool setLocalConfig(const SerialAccess::SerialPortConfiguration& localConfig) = 0;

	/**
	 * Attempts to apply the serial port configuration to the remote port
	 * @param remoteConfig The serial port configuration
	 * @return true if the configuration was applied successfully, false otherwise
	 */
	bool setRemoteConfig(const SerialAccess::SerialPortConfiguration& remoteConfig);

	/**
	 * Closes this connection, releasing both the local and the remote serial port.
	 * This function blocks until everything is closed.
	 * If an shutdown was already issued and the function is called a second time, it returns immediately.
	 * @return false if the method was already called before and this call did not have any effect, true if this was the first call and the shutdown was performed
	 */
	virtual bool shutdown() { return true; };

	/**
	 * Attempts to release the remote port.
	 * @return true if the remote port could be released successfully, false otherwise
	 */
	bool closeRemotePort();

	/**
	 * Attempts to release the local port.
	 * @return true if the local port could be released successfully, false otherwise
	 */
	virtual bool closeLocalPort() = 0;

	/**
	 * Returns true if the network connection is still operational
	 * @return true as long as the network socket is still open
	 */
	bool isAlive();

protected:

	/**
	 * Handles network package reception
	 */
	void doNetworkReception();
	/**
	 * Handles serial data reception
	 */
	virtual void doSerialReception() = 0;

	bool processPackage(const char* package, unsigned int packageLen);
	bool transmitPackage(const char* package, unsigned int packageLen);

	bool sendError(const std::string& errorMessage);
	bool processError(const char* package, unsigned int packageLen);

	bool sendConfirm(bool success);
	bool processConfirm(const char* package, unsigned int packageLen);

	bool sendRemoteOpen(const std::string& remoteSerial);
	bool processRemoteOpen(const char* package, unsigned int packageLen);

	bool sendRemoteClose();
	bool processRemoteClose(const char* package, unsigned int packageLen);

	bool sendRemoteConfig(const SerialAccess::SerialPortConfiguration& remoteConfig);
	bool processRemoteConfig(const char* package, unsigned int packageLen);

	bool sendSerialData(const char* data, unsigned int len);
	bool processSerialData(const char* package, unsigned int packageLen);

	bool sendPortState(bool dtrState, bool rtsState);
	bool processPortState(const char* package, unsigned int packageLen);

	bool sendFlowControl(bool readyState);
	bool processFlowControl(const char* package, unsigned int packageLen);

	virtual void transmitSerialData(const char* data, unsigned int len);
	virtual void updateFlowControl(bool enableTransmit);
	virtual void updatePortState(bool dtr, bool rts) = 0;

	std::mutex m_socketTX;												// protect against async writes to network
	std::unique_ptr<NetSocket::Socket> socket;							// network TCP socket
	std::string remoteHostName;											// the host name this connection was established with
	std::string remoteHostPort;											// the host port this connection was established with
	std::function<void(SOELinkHandler*)> onDeath;						// callback when connection is shut down

	std::thread thread_rx;												// TCP reception thread
	std::thread thread_tx;												// TCP transmission thread
	bool txNothingToDo;													// true if the TX thread is currently waiting for an event to occur
	Ringbuffer serialData = Ringbuffer(SOE_TCP_STREAM_BUFFER_LEN);		// intermediate buffer for TCP to serial data
	bool flowEnable = true;												// flow control for TCP transmissions
	bool remoteFlowEnable = true;										// keeps track of the flow control signal for the remote port

	std::mutex m_remoteReturn;											// protect return value against async writes
	std::condition_variable cv_remoteReturn;							// waiting point for return value
	bool remoteReturn;													// remote return value set by confirm package

	std::mutex m_localPort;												// protect local serial port against async modification
	std::condition_variable cv_openLocalPort;							// waiting point for TX thread when port closed
	std::string localPortName;											// local serial port name currently open
	std::string remotePortName;											// remote serial prot currently open

};

class SOELinkHandlerCOM : public SOELinkHandler {

public:
	using SOELinkHandler::SOELinkHandler;

	bool shutdown() override;

	bool openLocalPort(const std::string& localSerial) override;
	bool setLocalConfig(const SerialAccess::SerialPortConfiguration& localConfig) override;
	bool closeLocalPort() override;

private:

	std::unique_ptr<SerialAccess::SerialPort> localPort;				// local serial port

	void doSerialReception() override;

	void transmitSerialData(const char* data, unsigned int len) override;
	void updateFlowControl(bool enableTransmit) override;
	void updatePortState(bool dtr, bool rts) override;

};

// TODO virtual port integration
//class SOELinkHandlerVCOM : public SOELinkHandler {
//
//public:
//
//	bool openLocalPort(const std::string& localSerial) override;
//	bool setLocalConfig(const SerialAccess::SerialPortConfiguration& localConfig) override;
//	bool closeLocalPort() override;
//
//private:
//
//	std::unique_ptr<SerialAccess::VirtualSerialPort> localPort;	// local serial port
//
//	void transmitSerialData(const char* data, unsigned int len) override;
//	void updatePortState(bool dtr, bool rts) override;
//
//};

}

#endif /* SOE_CONNECTION_HPP_ */
