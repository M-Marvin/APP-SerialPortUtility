/*
 * SOEClient.h
 *
 *  Created on: 04.02.2025
 *      Author: marvi
 */

#ifndef SOEIMPL_HPP_
#define SOEIMPL_HPP_

class Socket;

//#define DEBUG_PRINTS

#define DEFAULT_SOE_PORT 26

#define SERIAL_RX_ENTRY_LEN 1024 		// Buffer for incoming serial payload (individual stack entries)
#define SERIAL_RX_STACK_LIMIT 32		// Limit for the reception stack, serial reception will hold if this limit is exceeded, and data loss will occur, tx stack on the other end will automatically have the same size, although it can exceed the limit slightly under specific conditions
#define SERIAL_RX_TIMEOUT 0 			// Time to wait for requested amount of bytes (SERIAL_RX_BUF) before returning with less if nothing more was received
#define SERIAL_TX_TIMEOUT 1000 			// Time to wait for transmitting serial data before returning with the number of bytes that have been transmitted

/* The control frame operation codes */
#define OPC_ERROR 0x0
#define OPC_OPEN 0x1
#define OPC_OPENED 0x2
#define OPC_CLOSE 0x3
#define OPC_CLOSED 0x4
#define OPC_STREAM 0x5
#define OPC_TX_CONFIRM 0x6
#define OPC_RX_CONFIRM 0x7

#define SOE_FRAME_HEADER_LEN 1										// The length of the SOE control frame header
#define INET_RX_PCKG_LEN SERIAL_RX_ENTRY_LEN + SOE_FRAME_HEADER_LEN	// Buffer for incoming network payload (individual stack entries + package header)
#define INET_TX_REP_INTERVAL 100									// Interval in which the tx thread checks the rx stacks for data, even if he was not notified about new data, these intervals are used to re-send lost packages
#define INET_KEEP_ALIVE_TIMEOUT 10000								// Timeout for network connection, if no packages is received within this time, a lost connection is assumed
#define INET_KEEP_ALIVE_INTERVAL 1000								// Timeout for receiving packages, before sending keep alive package (OPC_STREAM with length 0)

#include <network.hpp>
#include <serial_port.hpp>
#include <thread>
#include <map>
#include <shared_mutex>
#include <string>
#include <condition_variable>
#include <functional>

using namespace std;

/**
 * SERIAL OVER ETHERNET PROTOCOL
 *
 * TERMS USED
 * RX STACK	- the stack used to store payload that was RECEIVED over serial and has to be TRANSMITTED over network, consists of an RXID to payload map
 * RXID		- the id of an payload that has to be transmitted over network, the id is used to detect lost packages and maintain correct order
 * TX STACK	- the stack used to store payload that was RECEIVED over network and has to be TRANSMITTED over serial, consists of an TXID to payload map
 * TXID		- the id of an payload that was received over network, the RXID will become the TXID on the other end of the network connection
 *
 *
 * CLIENT OPEN PORT (initiated by the openRemotePort method)
 * 1. Client sends OPC_OPEN request
 * 2. Server attempts to open port, sends OPC_OPENED if succeeded otherwise OPC_ERROR with an error message
 * 3a. Client receives OPC_OPENED, connection was established, client will attempt to claim local virtual port, if this fails, an close sequence will be initiated (see below)
 * 3b. Client receives OPC_ERROR, connection was NOT established, port is STILL CLOSED on server
 * 3c. Client receives nothing, connection might be or might not be open, client will attempt to close port (see below)
 *
 * CLIENT CLOSE PORT (initiated by the closeRemotePort method)
 * 1. Client sends OPC_CLOSE request
 * 2. Server attempts to close the port, sends OPC_CLOSED if succeeded, otherwise (including if the port was already closed) OPC_ERROR with an error message
 * 3a. Client receives OPC_CLOSED, connection was closed, client will close local virtual port
 * 3b. Client receives OPC_ERROR, connection IS CLOSED, but was not in expected state (was already closed for example), client will close local virtual port
 * 3c. Client receives nothing, connection might be or might not be open, client will report false to the calling application, client will close local virtual port
 * 	NOTE: If the close request was initiated from an timeout open request, an timeout on the close request might lead to an undefined state of the port on the server.
 * 	NOTE: Should this occur, the port will stay in the undefined state until an close or open attempt succeeds, or the network socket is closed, in which the server will force terminate all claimed ports.
 *
 * RECEIVE SERIAL (client and server port handlers)
 * 1. Attempt to read data from serial port to fill up payload buffer, reading ends if for some time no data was received or if the payload buffer is full
 * 2. The next free TXID is assigned to the payload, and the payload is put on the TX STACK
 * -> Continues with STREAM DATA CLIENT <-> SERVER
 *
 * TRANSMIT SERIAL (client and server port handlers)
 * -> Data comes in from STREAM DATA CLIENT <-> SERVER
 * 1. Waits for incoming data on the RX STACK, polls (removes) the payload with "last RXID + 1" from the stack, hold if this package is missing
 * 2. Attempts to transmit the data over serial, if this failed it is attempted again in regular intervals, and a OPC_ERROR is send to the other end over network
 *
 * STREAM DATA CLIENT > SERVER (initiated by the serial port handlers)
 * 1. Client sends OPC_STREAM request, but keeps the send data in a buffer until OPC_TX_CONFIRM confirmation by the server
 * 2. Server puts the received data on the port's TX stack and sends and OPC_RX_CONFIRM frame (RX refers to the server data reception)
 * 	NOTE: If the transmission id is invalid (out of order) or the RX STACK if full, the server will still respond with an OPC_CONFIRM, but will not process the data
 * 3a. Client receives OPC_RX_CONFIRM, transmission to server successful, the package will be marked as reception confirmed in the RX STACK
 * 3b. Client receives OPC_ERROR, the data could not be processed, the connection might or might not be closed, as such, further requests might fail
 * 	NOTE: If the port was closed in consequence of the error, an OPC_CLOSED will be send from the server, to notify the client about the new state of the port
 * 	NOTE: Should the OPC_CLOSED be lost, the next requests to the port will fail again, resulting in a new OPC_CLOSED frame
 * 3c. Client receives nothing, the package might be lost, the client will re-send the package after the configured timeout expires
 * 4a. Server waits until the serial port could send the data, then sends an OPC_TX_CONFIRM frame. (TX refers to the port transmission)
 * 4b. Server is unable to transmit data for various reasons, the TX STACK and as such the RX STACK on the client will continue to fill up until it hits the configured limit, in which case the serial flow control (if enabled) will be activated to prevent reception of further data on the serial port.
 *  NOTE: Should the RX STACK fill up to the limit, an "re-send stack" sequence is initiated on the client in regular intervals, all packages on the TX STACK are re-transmitted to the server, in case the server side processing hangs because of missing data (lost package)
 *  NOTE: As soon as the server continues to send data over its serial, and the RX STACK gets empty, the resulting OPC_TX_CONFIRM packages will clear the TX STACK on the client, and the flow control allows new data to be received over serial
 * 5a. Client receives OPC_TX_CONFIRM and removes the send data from its buffer, making space for reading new data from the serial port
 * 5b. Client receives OPC_ERROR, connection is not closed but transmission of data on the serial port failed, server might close port if required, and notify the client with an OPC_CLOSED about the new port state
 * 5c. Client receives nothing, the TX STACK on the client and the RX STACK on the server will continue to fill up, see 4b (NOTE) (same result)
 *
 * STREAM DATA CLIENT < SERVER (initiated by the serial port handlers)
 * same as above, just with swapped roles of client and server
 *
 *
 * SERVER CONTROL FRAME BEHAVIORS:
 * OPC_ERROR	-> log received error, no further actions required
 * OPC_OPEN		-> attempt to open the port, answer with OPC_OPENED if succeeded, answer with OPC_ERROR otherwise
 * OPC_OPENED	-> INVALID FOR SERVER, LOG INVALID FRAME
 * OPC_CLOSE	-> attempt to close the port, answer with OPC_CLOSED if succeeded, answer with OPC_ERROR otherwise
 * OPC_CLOSED	-> INVALID FOR SERVER, LOG INVALID FRAME
 * OPC_STREAM	-> put supplied data on port transmission stack, confirm with OPC_RX_CONFIRM, send OPC_TX_CONFIRM as soon as data was transmitted trough serial, answer with OPC_ERROR if data could not be processed
 *
 * CLIENT CONTROL FRAME BEHAVIORS:
 * same as above, only with three changes:
 * OPC_OPENED	-> signal success to current pending open remote port sequence if the port name matches, otherwise ignore
 * OPC_CLOSED	-> signal success to current close remote port sequence if the port name matches, otherwise close the local virtual remote port for the named remote port, since it has been closed on the server
 * OPC_ERROR	-> if the error's port name matches a currently pending open/close sequence, signal error to that sequence, otherwise log it
 */

class SOESocketHandler;

class SOEPortHandler {

public:
	/**
	 * Creates a new serial port handler
	 * @param port The serial port to handle
	 * @param newDataCallback The callback to run when new data was received on the RX STACK
	 * @param txConfirmCallback The callback to call if data from the TX STACK was successfully sent
	 */
	SOEPortHandler(SerialPort* port, function<void(void)> newDataCallback, function<void(unsigned int)> txConfirmCallback);

	/**
	 * Closes the serial port, and cleans up all allocated buffer memory
	 */
	~SOEPortHandler();

	/**
	 * Checks if the serial port is still operational
	 * @return true as long as the serial port is still open
	 */
	bool isOpen();

	/**
	 * Puts new data on the ports TX STACK
	 * Should the data have an invalid TXID, it is ignored and the function returns false
	 * Should the port be closed, the function returns false
	 * @param txid The TXID of the payload
	 * @param buffer The payload buffer
	 * @param length The length of the payload
	 * @return true if the payload could be put on the TX STACK
	 */
	bool send(unsigned int txid, const char* buffer, unsigned long length);

	/**
	 * Attempts to get the next payload to transmit over network from the RX STACK.
	 * If RX STACK reaches its limit and all data has already been requested at least once, this function will automatically start the repition sequence by returning all payloads frmo the begining.
	 * If no more paylad is available (including after an repetition sequence) false is returned by this method at least once, before eventually staring a new repetition sequence.
	 * @param rxid The RXID of the returned payload
	 * @param buffer The buffer containing the payload
	 * @param length The length of the payload
	 * @return true if an payload was found and returned
	 */
	bool read(unsigned int* rxid, const char** buffer, unsigned long* length);

	/**
	 * Confirms that the payload with the given RXID was successfully received on the other end.
	 * This function will mark this RXID on the RX STACK to be transmitteded to the other end successfully.
	 * @param rxid The RXID of the confirmed payload
	 */
	void confirmReception(unsigned int rxid);

	/**
	 * Confirms that the payload with the given RXID was successfully transmitted on the other ends serial.
	 * This function will remove this RXID from the RX STACK to make room for new payload.
	 * @param rxid The RXID of the confirmed payload
	 */
	void confirmTransmission(unsigned int rxid);

private:
	/**
	 * Handle serial transmission
	 */
	void handlePortTX();
	/**
	 * Handles serial reception
	 */
	void handlePortRX();

	unique_ptr<SerialPort> port;
	function<void(void)> new_data;
	function<void(unsigned int)> tx_confirm;

	typedef struct {
		unsigned long length;
		unique_ptr<char> payload;
	} tx_entry;

	thread thread_tx;
	unsigned int next_txid;					// Next txid that the serial port will try to transmitt
	mutex tx_stackm;						// Mutex for synchronizing access to transmission stack and condition variable
	condition_variable tx_waitc;			// TX hold variable, transmission will hold here if the tx stack runs out
	map<unsigned int, tx_entry> tx_stack;	// The serial transmission stack, holding network received data to transmitt over serial

	typedef struct  {
		unsigned long length;
		unique_ptr<char> payload;
		chrono::time_point<chrono::steady_clock> time_to_resend;
		bool rx_confirmed;
	} rx_entry;

	thread thread_rx;
	unsigned int next_free_rxid;			// Next rxid to use for packages read from serial
	unsigned int next_transmit_rxid;		// Next not yet transmitted rxid to return when requesting data for network transmission
	unsigned int last_transmitted_rxid;		// Oldest rxid in the stack which's transmission has not yet been confirmed by the other end
	mutex rx_stackm;						// Mutex for synchronizing access to reception stack and condition variable
	condition_variable rx_waitc;			// RX hold condition variable, reception will hold and wait for this if the reception stack size exceeds the limit
	map<unsigned int, rx_entry> rx_stack;	// The serial reception stack, holding serial received and network transmitted but not yet serial transmitted packages

};

class SOESocketHandler {

public:
	/**
	 * Creates a new client network connection handler
	 * @param socket The socket of the client-server connection
	 */
	SOESocketHandler(Socket* socket);

	/**
	 * Closes all ports and the socket and cleans all allocated buffer memory
	 */
	~SOESocketHandler();

#ifdef SIDE_CLIENT
	/**
	 * Attempts to claim the given port on the remote server and the given local port and connecting them over the serial over ethernet protocoll
	 * @param remoteAddress The network address to send control frames too
	 * @param remotePortName The remote port name to claim
	 * @param config The serial port configuration (baud, dataBits, stopBits etc)
	 * @param localPortName The local port name to claim
	 * @param timeoutms The timeout for the remote port claim, if exceeded the connection will fail
	 * @return true if the remote and local port could successfully be claimed and connected, false otherwise
	 */
	bool openRemotePort(const INetAddress& remoteAddress, const string& remotePortName, const SerialPortConfiguration config, const string& localPortName, unsigned int timeoutms);

	/**
	 * Attempts to release the remote port and the corresponding local port.
	 * 	 * @param remoteAddress The network address to send control frames too
	 * @param remotePortName The remote port name to release
	 * @param timeoutms Tge timeout for the remote port release, if exceeded, the release will fail
	 * @return true if the remote and local port could be released successfully, false otherwise
	 */
	bool closeRemotePort(const INetAddress& remoteAddress, const string& remotePortName, unsigned int timeoutms);
#endif
	/**
	 * Returns true if the network connection is still operational
	 * @return true as long as the network socket is still open
	 */
	bool isActive();

private:
	/**
	 * Called by the port handlers to notify that new data is available in one of the RX STACK's
	 */
	void notifySerialData();

	/**
	 * Sends an serial over ethernet frame to the other end.
	 * @param remoteAddress The network address to send control frames too
	 * @param opc The OPC of the frame
	 * @param payload The payload of the frame
	 * @param length The length of the payload
	 * @return true if the data could be send successfully, false otherwise
	 */
	bool sendFrame(const INetAddress& remoteAddress, char opc, const char* payload, unsigned int length);

	/**
	 * Assembles and transmits an error frame with the given remote port name and error message.
	 * @param remoteAddress The network address to send control frames too
	 * @param portName The remote (server) port name to which this error refers (might be null)
	 * @param msg The error message
	 */
	void sendError(const INetAddress& remoteAddress, const string& portName, const string& msg);

	/**
	 * Assembles and transmits an OPC_OPENED or OPC_CLOSED frame to the other end.
	 * @param remoteAddress The network address to send control frames too
	 * @param claimed If the port was opened (OPC_OPENED) or closed (OPC_CLOSED)
	 * @param portName The name of the remote (server) port
	 * @return true if the data could be send successfully, false otherwise
	 */
	bool sendClaimStatus(const INetAddress& remoteAddress, bool claimed, const string& portName);

	/**
	 * Assembles and transmits an OPC_RX_CONFIRM or OPC_TX_CONFIRM frame to the other end.
	 * @param remoteAddress The network address to send control frames too
	 * @param transmission If the refereed payload was only received (OPC_RX_CONFIRMED) or already transmitted over serial (OPC_TX_CONFIRM)
	 * @param portName The name of the remote (server) port
	 * @param txid The payloads TXID
	 * @return true if the data could be send successfully, false otherwise
	 */
	bool sendConfirm(const INetAddress& remoteAddress, bool transmission, const string& portName, unsigned int txid);

	/**
	 * Assembles and transmits an OPC_STREAM frame to the other end.
	 * @param remoteAddress The network address to send control frames too
	 * @param portName The name of the remote (server) port
	 * @param rxid The serial payload RXID
	 * @param payload The serial payload buffer
	 * @param length The serial payload length
	 * @return true if the data could be send successfully, false otherwise
	 */
	bool sendStream(const INetAddress& remoteAddress, const string& portName, unsigned int rxid, const char* payload, unsigned long length);

#ifdef SIDE_CLIENT
	/**
	 * Assembles and transmits an OPC_OPEN frame to the server.
	 * @param remoteAddress The network address to send control frames too
	 * @param portName The name of the remote port to open
	 * @param baud The baud rate to configure
	 * @return true if the data could be send successfully, false otherwise
	 */
	bool sendOpenRequest(const INetAddress& remoteAddress, const string& portName, const SerialPortConfiguration& config);

	/**
	 * Assembles and transmits an OPC_CLOSE frame to the server.
	 * @param remoteAddress The network address to send control frames too
	 * @param portName The name of the remote port to close
	 * @return true if the data could be send successfully, false otherwise
	 */
	bool sendCloseRequest(const INetAddress& remoteAddress, const string& portName);
#endif

	/**
	 * Handles network package reception
	 */
	void handleClientRX();
	/**
	 * Handles network package transmission
	 */
	void handleClientTX();

	typedef struct {
		unique_ptr<SOEPortHandler> handler;
		INetAddress remote_address;
		chrono::time_point<chrono::steady_clock> point_of_timeout;
		chrono::time_point<chrono::steady_clock> last_send;
	} port_claim;

	unique_ptr<Socket> socket;
	shared_timed_mutex portsm;
	map<string, port_claim> ports;

	thread thread_rx;
	thread thread_tx;
	mutex tx_waitm;							// Mutex to protect condition variable
	condition_variable tx_waitc;			// Condition variable, the tx thread whil wait here for new data to transmit over network

#ifdef SIDE_CLIENT
	map<string, string> remote2localPort;
	const string& local2remotePort(const string& name);

	string remote_port_name;				// Name of the remote port of the current open/close sequence
	bool remote_port_status;				// Status for the pending open/close sequence, set before condition variable is released
	mutex remote_port_waitm;				// Mutex protecting condition variable
	condition_variable remote_port_waitc;	// Condition variable for pending port open/close sequences, waits for OPC_OPENED or OPC_CLOSED
#endif

};

#endif /* SOEIMPL_HPP_ */
