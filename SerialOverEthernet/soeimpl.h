/*
 * SOEClient.h
 *
 *  Created on: 04.02.2025
 *      Author: marvi
 */

#ifndef SOEIMPL_H_
#define SOEIMPL_H_

class Socket;

#define DEFAULT_SOE_PORT 26

#define INET_RX_BUF 1024 			// Buffer for incoming network payload (individual stack entries)
#define SERIAL_RX_BUF 1024 			// Buffer for incoming serial payload (individual stack entries)
#define SERIAL_RX_TIMEOUT 100 		// Time to wait for requested ammount of bytest (SERIAL_RX_BUF) before returning with less if nothing more was received
#define SERIAL_TX_TIMEOUT 1000 		// Time to wait for transmitting serial data before returning and throwing an error
#define SERIAL_CONSEC_DELAY 100		// Max delay between serial reads until the current buffer put on the transmission stack

#define RX_STACK_LIMIT 16			// Limit for the reception stack, serial reception will hold if this limit is exceeded, and data loss will occur
#define TX_STACK_LIMIT 16			// Limit for the transmission stack, TODO
#define INET_TX_REPETITION 4000		// Interval in which the tx thread checks the rx stacks for data, even if he was not notified about new data

#define OPC_ERROR 0x0
#define OPC_OPEN 0x1
#define OPC_OPENED 0x2
#define OPC_CLOSE 0x3
#define OPC_CLOSED 0x4
#define OPC_STREAM 0x5
#define OPC_TX_CONFIRM 0x6
#define OPC_RX_CONFIRM 0x7

#define REMOTE_PORT_NAME_BUF_LEN 1024

#include <network.h>
#include <serial_port.h>
#include <thread>
#include <map>
#include <mutex>
#include <string>
#include <condition_variable>

using namespace std;

/**
 * SERIAL OVER ETHERNET PROTOCOL
 *
 * CLIENT OPEN PORT (initialed by the openRemotePort method)
 * 1. Client sends OPC_OPEN request
 * 2. Server attempts to open port, sends OPC_OPENED if succeeded otherwise OPC_ERROR with an error message
 * 3a. Client receives OPC_OPENED, connection was established, client will attempt to claim local virtual port, if this fails, an close sequence will be initiated (see below)
 * 3b. Client receives OPC_ERROR, connection was NOT established, port is STILL CLOSED on server
 * 3c. Client receives nothing, connection might be or might not be open, client will attempt to close port (see below)
 *
 * CLIENT CLOSE PORT (initialed by the closeRemotePort method)
 * 1. Client sends OPC_CLOSE request
 * 2. Server attempts to close the port, sends OPC_CLOSED if succeeded, otherwise (including if the port was already closed) OPC_ERROR with an error message
 * 3a. Client receives OPC_CLOSED, connection was closed, client will close local virtual port
 * 3b. Client receives OPC_ERROR, connection IS CLOSED, but was not in expected state (was already closed for example), client will close local virtual port
 * 3c. Client receives nothing, connection might be or might not be open, client will report false to the calling application, client will close local virtual port
 * 	NOTE: If the close request was initiated from an timeout open request, an timeout on the close request might lead to an undefined state of the port on the server.
 * 	NOTE: Should this occur, the port will stay in the undefined state until an close or open attempt succeeds, or the network socket is closed, in which the server will force terminate all claimed ports.
 *
 * CLIENT STREAM DATA (initiated automatically by the port handlers)
 * 1. Client sends OPC_STREAM request, but keeps the send data in a buffer until OPC_TX_CONFIRM confirmation by the server
 * 2. Server puts the received data on the port's TX stack and sends and OPC_RX_CONFIRM frame (RX refers to the server data reception)
 * 	NOTE: If the txid is invalid (out of order) or the tx stack if full, the server will still respond with an OPC_CONFIRM, but will not process the data
 * 3a. Client receives OPC_RX_CONFIRM, transmission to server successful, this is mostly for debugging purposes, and will have no actual effect on the transmissions
 * 3b. Client receives OPC_ERROR, the data could not be processed, the connection might or might not be closed, as such, further requests might fail
 * 	NOTE: If the port was closed in consequence of the error, an OPC_CLOSED will be send from the server, to notify the client about the new state of the port
 * 3c. Client receives nothing, a lost RX_CONFIRM has no affect on the connection, since its mostly for debugging purposes
 * 4. Server waits until the serial port could send the data, then sends an OPC_TX_CONFIRM frame. (TX refers to the port transmission)
 * 4a. Client receives OPC_TX_CONFIRM and removes the send data from its buffer, making space for reading new data from the serial port
 * 5b. Client receives OPC_ERROR, connection is not closed but transmission of data on the serial port failed, server might close port if required, and notify the client with an OPC_CLOSED about the new port state
 * 5c. Client receives nothing, the package might be lost, if the client stops receiving OPC_TX_CONFIRM messages, an repetition of all packages still on the stack will be initiated, to attempt to resume normal operation
 *
 * SERVER STREAM DATA
 * same behavior as in CLIENT STREAM DATA, but without OPC_OPENED implementation
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
 * OPC_OPENED	-> signal success to current open remote port sequence if the port name matches, otherwise signal error
 * OPC_CLOSED	-> signal success to current close remote port sequence if the port name matches, otherwise signal error
 * OPC_ERROR	-> if the error's port name matches a currently pending open/close sequence, signal error
 */

class SOESocketHandler;

class SOEPortHandler {

public:
	SOEPortHandler(SOESocketHandler* client, SerialPort &port, const char* portName);
	~SOEPortHandler();

	bool isOpen();
	bool send(unsigned int txid, const char* buffer, size_t length);
	bool read(unsigned int* rxid, const char** buffer, size_t* length);
	void confirmTransmission(unsigned int rxid);

private:
	void handlePortTX();
	void handlePortRX();

	string portName;
	SOESocketHandler* client;
	SerialPort* port;

	thread thread_tx;
	unsigned int next_txid;								// Next txid that the serial port will try to transmitt
	mutex tx_stackm;									// Mutex for synchronizing access to transmission stack and condition variable
	condition_variable tx_waitc;						// TX hold variable, transmission will hold here if the tx stack runs out
	map<unsigned int, pair<size_t, char*>> tx_stack;	// The serial transmission stack, holding network received data to transmitt over serial

	thread thread_rx;
	unsigned int next_free_rxid;						// Next rxid to use for packages read from serial
	unsigned int next_transmit_rxid;					// Next not yet transmitted rxid to return when reading for network transmission
	unsigned int last_transmitted_rxid;					// Oldest rxid in the stack (might be empty in the stack) which's transmission has not yet been confirmed by the other end
	mutex rx_stackm;									// Mutex for synchronizing access to reception stack and condition variable
	condition_variable rx_waitc;						// RX hold condition variable, reception will hold and wait for this if the reception stack size exceeds the limit
	map<unsigned int, pair<size_t, char*>> rx_stack;	// The serial reception stack, holding serial received and network transmitted but not yet serial transmitted packages

};

class SOESocketHandler {

public:
	SOESocketHandler(Socket &socket);
	~SOESocketHandler();

#ifdef SIDE_CLIENT
	bool openRemotePort(const char* remotePortName, unsigned int baud, const char* localPortName, unsigned long long timeoutms);
	bool closeRemotePort(const char* remotePortName, unsigned long long timeoutms);
#endif
	bool isActive();
	void notifySerialData();
	bool sendFrame(char opc, const char* payload, unsigned int length);
	void sendError(const char* portName, const char* msg);
	bool sendClaimStatus(bool claimed, const char* portName);
	bool sendConfirm(bool transmission, const char* portName, unsigned int txid);
	bool sendStream(const char* portName, unsigned int rxid, const char* payload, size_t length);
#ifdef SIDE_CLIENT
	bool sendOpenRequest(const char* portName, unsigned int baud);
	bool sendCloseRequest(const char* portName);
#endif

private:
	void handleClientRX();
	void handleClientTX();

	Socket* socket;
	map<string, SOEPortHandler*> ports;

	thread thread_rx;
	char op_code;
	char* pckg_buf;
	unsigned int pckg_len;
	unsigned int pckg_recv;

	thread thread_tx;
	mutex tx_waitm;
	condition_variable tx_waitc;

#ifdef SIDE_CLIENT
	map<string, string> remote2localPort;

	char remote_port_name[REMOTE_PORT_NAME_BUF_LEN] = {0};	// Name of the remote port of the current open/close sequence
	bool remote_port_status;								// Status for the pending open/close sequence, set before condition variable is released
	mutex remote_port_waitm;								// Mutex protecting condition variable
	condition_variable remote_port_waitc;					// Condition variable for pending port open/close sequences, waits for OPC_OPENED or OPC_CLOSED
#endif

};

#endif /* SOEIMPL_H_ */
