/*
 * SOEClient.h
 *
 *  Created on: 04.02.2025
 *      Author: marvi
 */

#ifndef SOEIMPL_H_
#define SOEIMPL_H_

#include <network.h>
#include <serial_port.h>
#include <thread>
#include <map>
#include <mutex>
#include <string>

using namespace std;

class SOEClient;

class SOEPort {

public:
	SOEPort(SOEClient* client, SerialPort &port, const char* portName);
	~SOEPort();

	bool send(unsigned int txid, const char* buffer, size_t length);

private:
	void handlePortTX();

	string portName;
	SOEClient* client;
	SerialPort* port;

	thread thread_tx;
	unsigned long next_txid;
	mutex tx_stackm;
	mutex tx_waitm;
	map<unsigned int, pair<size_t, char*>> tx_stack;

};

class SOEClient {

public:
	SOEClient(Socket &socket);
	~SOEClient();

	bool isActive();
	bool sendFrame(char opc, const char* payload, unsigned int length);
	void sendError(const char* portName, const char* msg);
	bool sendClaimStatus(bool claimed, const char* portName);
	bool sendTransmissionConfirm(const char* portName, unsigned int txid);

private:
	void handleClientRX();

	Socket* socket;
	map<string, SOEPort*> ports;

	thread thread_rx;
	char op_code;
	char* pckg_buf;
	unsigned int pckg_len;
	unsigned int pckg_recv;

};

#endif /* SOEIMPL_H_ */
