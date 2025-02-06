/*
 * SOEClient.h
 *
 *  Created on: 04.02.2025
 *      Author: marvi
 */

#ifndef SOEIMPL_H_
#define SOEIMPL_H_

class Socket;

#define INET_RX_BUF 1024 			// Buffer for incoming network payload (individual stack entries)
#define SERIAL_RX_BUF 1024 			// Buffer for outgoing serial payload (individual stack entries)
#define SERIAL_RX_TIMEOUT 1000 		// Time to wait for the payload buffer to fill up before force transmitting it
#define SERIAL_TX_TIMEOUT 2000 		// Time to wait for transmitted serial data before returnsing and throwing an error

#define OPC_ERROR 0x0
#define OPC_OPEN 0x1
#define OPC_OPENED 0x2
#define OPC_CLOSE 0x3
#define OPC_CLOSED 0x4
#define OPC_STREAM 0x5
#define OPC_TX_CONFIRM 0x6
#define OPC_RX_CONFIRM 0x7

#include <network.h>
#include <serial_port.h>
#include <thread>
#include <map>
#include <mutex>
#include <string>
#include <condition_variable>

using namespace std;

class SOEClient;

class SOEPort {

public:
	SOEPort(SOEClient* client, SerialPort &port, const char* portName);
	~SOEPort();

	bool isOpen();
	bool send(unsigned int txid, const char* buffer, size_t length);
	bool read(unsigned int* rxid, const char** buffer, size_t* length);
	void confirmReception(unsigned int rxid);

private:
	void handlePortTX();
	void handlePortRX();

	string portName;
	SOEClient* client;
	SerialPort* port;

	thread thread_tx;
	unsigned int next_txid;
	mutex tx_stackm;
	mutex tx_waitm;
	condition_variable tx_waitc;
	map<unsigned int, pair<size_t, char*>> tx_stack;

	thread thread_rx;
	unsigned int next_rxid;
	unsigned int first_rxid;
	mutex rx_stackm;
	map<unsigned int, pair<size_t, char*>> rx_stack;

};

class SOEClient {

public:
	SOEClient(Socket &socket);
	~SOEClient();

	bool isActive();
	void notifySerialData();
	bool sendFrame(char opc, const char* payload, unsigned int length);
	void sendError(const char* portName, const char* msg);
	bool sendClaimStatus(bool claimed, const char* portName);
	bool sendTransmissionConfirm(const char* portName, unsigned int txid);
	bool sendStream(const char* portName, unsigned int rxid, const char* payload, size_t length);

private:
	void handleClientRX();
	void handleClientTX();

	Socket* socket;
	map<string, SOEPort*> ports;

	thread thread_rx;
	char op_code;
	char* pckg_buf;
	unsigned int pckg_len;
	unsigned int pckg_recv;

	thread thread_tx;
	mutex tx_waitm;
	condition_variable tx_waitc;

};

#endif /* SOEIMPL_H_ */
