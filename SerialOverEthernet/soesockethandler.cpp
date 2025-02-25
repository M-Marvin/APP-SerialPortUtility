/*
 * soeclient.cpp
 *
 *  Created on: 04.02.2025
 *      Author: marvi
 */

#include <corecrt.h>
#include <stdio.h>
#include <string.h>
#include <stdexcept>
#include <string>
#include "soeimpl.h"

using namespace std;

// Initializes a new client handler for the supplied network socket
SOESocketHandler::SOESocketHandler(Socket &socket) {
	this->socket = &socket;
	this->ports = map<string, SOEPortHandler*>();
	this->thread_rx = thread([this]() -> void {
		this->handleClientRX();
	});
	this->thread_tx = thread([this]() -> void {
		this->handleClientTX();
	});
}

// Shuts down the client handler and frees all resources (including ports opened by the client)
SOESocketHandler::~SOESocketHandler() {
	this->socket->close();
	{ unique_lock<mutex> lock(this->tx_waitm); }
	this->tx_waitc.notify_all();
	this->thread_rx.join();
	this->thread_tx.join();
#ifdef SIDE_CLIENT
	if (this->remote_port_name != 0)
		delete[] this->remote_port_name;
#endif
	delete this->socket;
}

// Returns true if the clients network connection is still open
bool SOESocketHandler::isActive() {
	return this->socket->isOpen();
}

void SOESocketHandler::handleClientTX() {

	// Start tx loop
	while (this->socket->isOpen()) {

		bool dataAvailable = false;
		for (auto entry = this->ports.begin(); entry != this->ports.end(); entry++) {

			// Request data from stack
			unsigned int rxid = 0;
			const char* payload = 0;
			unsigned long length = 0;
			if (!entry->second->read(&rxid, &payload, &length))
				continue;

			// Send payload stream frame
			if (!sendStream(entry->first.c_str(), rxid, payload, length)) {
				sendError(entry->first.c_str(), "failed to transmit STREAM frame, close port");

				// Close port
				delete entry->second;
				this->ports.erase(entry->first);

				if (!sendClaimStatus(false, entry->first.c_str())) {
					// If the error report fails too ... don't care at this point ...
					sendError(entry->first.c_str(), "failed to transmit CLOSE notification");
				}

		 		printf("closed port: %s\n", entry->first.c_str());

			}

			dataAvailable = true;
		}

		// If no data available, wait for more, but repeat periodically to detect rx stack overflows
		if (!dataAvailable) {
			unique_lock<mutex> lock(this->tx_waitm);
			this->tx_waitc.wait_for(lock, std::chrono::milliseconds(INET_TX_REP_INTERVAL));
		}

	}

}

void SOESocketHandler::notifySerialData() {
	{ unique_lock<mutex> lock(this->tx_waitm); }
	this->tx_waitc.notify_all();
}

// Handles the incoming network requests
void SOESocketHandler::handleClientRX() {

	// Setup rx variables
	this->op_code = -1;
	this->pckg_buf = 0;
	this->pckg_len = 0;
	this->pckg_recv = 0;

	// Start rx loop
	char rxbuf[INET_RX_BUF];
	unsigned int received = 0;
	while (this->socket->isOpen()) {

		// If no more data remaining, wait for new data from the network
		if (received == 0) {
			if (this->op_code == -1) {
				do {
					unsigned int rxlen = 0;
					if (!this->socket->receive(rxbuf + received, FRAME_HEADER_LEN - received, &rxlen)) {
						printf("network socket was closed, shutdown connection\n");
						goto rxend;
					}
					received += rxlen;
				} while (received < FRAME_HEADER_LEN);
			} else {
				unsigned int missing = this->pckg_len - this->pckg_recv;
				if (INET_RX_BUF < missing) missing = INET_RX_BUF;
				if (!this->socket->receive(rxbuf, missing, &received)) {
					printf("network socket was closed, shutdown connection\n");
					goto rxend;
				}
			}
		}

		// Exit if the socket was closed
		if (!socket->isOpen()) break;

		// If no frame is currently being received, start new frame
		if (this->op_code == -1) {

			// Decode op code
			this->op_code = rxbuf[0];

			// Decode payload length field
			this->pckg_len = 0;
			this->pckg_len = 0;
			this->pckg_len |= (rxbuf[4] & 0xFF) << 0;
			this->pckg_len |= (rxbuf[3] & 0xFF) << 8;
			this->pckg_len |= (rxbuf[2] & 0xFF) << 16;
			this->pckg_len |= (rxbuf[1] & 0xFF) << 24;

			// Allocate buffer for payload
			this->pckg_buf = new char[this->pckg_len];
			if (this->pckg_buf == 0) {
				printf("FRAME ERROR: package to large, could not allocate!");
				goto rxend;
			}

			// Calculate length to copy
			unsigned int rempcklen = this->pckg_len;
			if (rempcklen > received - FRAME_HEADER_LEN)
				rempcklen = received - FRAME_HEADER_LEN;

			// Copy received payload
			memcpy(this->pckg_buf, rxbuf + FRAME_HEADER_LEN, rempcklen);
			this->pckg_recv = rempcklen;

			// Data of next frame remaining, move to front and process later
			unsigned int reminder = received - rempcklen - FRAME_HEADER_LEN;
			if (reminder > 0) {
				memcpy(rxbuf, rxbuf + rempcklen + FRAME_HEADER_LEN, reminder);
			}
			received = reminder;

		// Otherwise, add received data to current frame payload buffer
		} else {

			// Calculate length to copy
			unsigned int rempcklen = this->pckg_len - this->pckg_recv;
			if (rempcklen > received)
				rempcklen = received;

			// Copy received payload
			memcpy(this->pckg_buf + this->pckg_recv, rxbuf, rempcklen);
			this->pckg_recv += rempcklen;
			received -= rempcklen;

			// Data of next frame remaining, move to front and process later
			if (received > 0) {
				memcpy(rxbuf, rxbuf + rempcklen, received);
			}

		}

		// Check if all payload was received
		if (this->pckg_recv >= this->pckg_len) {

			switch (this->op_code) {
#ifdef SIDE_SERVER
			case OPC_OPEN: {

				// Check payload length
				if (this->pckg_len < 6) {
					sendError(NULL, "received incomplete OPEN control frame");
					break;
				}

				// Decode baud
				unsigned int portBaud =
						(this->pckg_buf[0] & 0xFF) << 24 |
						(this->pckg_buf[1] & 0xFF) << 16 |
						(this->pckg_buf[2] & 0xFF) << 8 |
						(this->pckg_buf[3] & 0xFF) << 0;

				// Decode port name length
				unsigned short portStrLen =
						(this->pckg_buf[4] & 0xFF) << 8 |
						(this->pckg_buf[5] & 0xFF) << 0;

				// Check port name length
				if (portStrLen > this->pckg_recv - 2) {
					sendError(NULL, "received invalid OPEN payload");
					break;
				}

				// Decode port name string
				char portName[portStrLen + 1] = {0};
				memcpy(portName, this->pckg_buf + 6, portStrLen);

				// Attempt to open port
				SerialPort* port = new SerialPort(portName);
				if (!port->openPort()) {
					sendError(portName, "failed to claim port");
					delete port;
					break;
				}
				port->setBaud(portBaud);
				port->setTimeouts(SERIAL_RX_TIMEOUT, SERIAL_TX_TIMEOUT);
				SOEPortHandler* portHandler = new SOEPortHandler(this, *port, portName);
				this->ports[string(portName)] = portHandler;

				// Confirm that the port has been opened, close port if this fails, to avoid unused open ports
				if (!sendClaimStatus(true, portName)) {
					sendError(portName, "failed to complete OPENED confirmation, close port");
					delete port;
					this->ports.erase(string(portName));
				}

		 		printf("opened port: %s\n", portName);

				break;
			}
			case OPC_CLOSE: {

				// Check payload length
				if (this->pckg_len < 2) {
					sendError(NULL, "received incomplete CLOSE control frame");
					break;
				}

				// Decode port name length
				unsigned short portStrLen =
						(this->pckg_buf[0] & 0xFF) << 8 |
						(this->pckg_buf[1] & 0xFF) << 0;

				// Check port name length
				if (portStrLen > this->pckg_recv - 2) {
					sendError(NULL, "received invalid CLOSE payload");
					break;
				}

				// Decode port name string
				char portName[portStrLen + 1] = {0};
				memcpy(portName, this->pckg_buf + 2, portStrLen);

				// Attempt to close port
				string sPortName = string(portName);
				try {
					SOEPortHandler* portHandler = this->ports.at(sPortName);
					delete portHandler;
					this->ports.erase(sPortName);
				} catch (out_of_range &e) {
					sendError(portName, "port not claimed");
					break;
				}

				// Confirm that the port has been closed
				if (!sendClaimStatus(false, portName)) {
					// If the error report fails too ... don't care at this point ...
					sendError(portName, "failed to transmit CLOSE confirmation");
				}

		 		printf("closed port: %s\n", portName);

				break;
			}
#endif
			case OPC_STREAM: {

				// Check payload length
				if (this->pckg_len < 6) {
					sendError(NULL, "received incomplete STREAM control frame");
					break;
				}

				// Decode port name length
				unsigned short portStrLen =
						(this->pckg_buf[0] & 0xFF) << 8 |
						(this->pckg_buf[1] & 0xFF) << 0;

				// Check port name length
				if (portStrLen > this->pckg_recv - 2) {
					sendError(NULL, "received invalid STREAM payload");
					break;
				}

				// Decode port name string
				char portName[portStrLen + 1] = {0};
				memcpy(portName, this->pckg_buf + 2, portStrLen);

				// Decode transmission id
				unsigned int txid =
						(this->pckg_buf[2 + portStrLen] & 0xFF) << 24 |
						(this->pckg_buf[3 + portStrLen] & 0xFF) << 16 |
						(this->pckg_buf[4 + portStrLen] & 0xFF) << 8 |
						(this->pckg_buf[5 + portStrLen] & 0xFF) << 0;

				// Attempt to get port
				SOEPortHandler* portHandler = 0;
				try {
#ifdef SIDE_CLIENT
					// From the server we get the remote host name, so we need to map it to the local port
					string localPortName = this->remote2localPort.at(string(portName));
					portHandler = this->ports.at(localPortName);
#else
					portHandler = this->ports.at(string(portName));
#endif
				} catch (const std::out_of_range& e) {
					sendError(portName, "port not claimed");
					break;
				}

				// Check if port was closed unexpectedly
				if (!portHandler->isOpen()) {
					sendError(portName, "port is already closed");

					// Close port
					delete portHandler;
					this->ports.erase(string(portName));
					sendClaimStatus(false, portName);

					if (!sendClaimStatus(false, portName)) {
						// If the error report fails too ... don't care at this point ...
						sendError(portName, "failed to transmit CLOSE notification");
					}

			 		printf("closed port: %s\n", portName);

					break;
				}

				// Put remaining payload on transmission stack
				const char* payload = this->pckg_buf + 6 + portStrLen;
				size_t payloadLen = this->pckg_recv - 6 - portStrLen;
				if (!portHandler->send(txid, payload, payloadLen)) {
					printf("could not process package: [tx %u] %s\n", txid, portName);
				}

				sendConfirm(false, portName, txid);
				break;
			}
			case OPC_RX_CONFIRM: {

#ifdef DEBUG_PRINTS

				// Check payload length
				if (this->pckg_len < 6) {
					sendError(NULL, "received incomplete TX_CONFIRM control frame");
					break;
				}

				// Decode port name length
				unsigned short portStrLen =
						(this->pckg_buf[0] & 0xFF) << 8 |
						(this->pckg_buf[1] & 0xFF) << 0;

				// Check port name length
				if (portStrLen > this->pckg_recv - 2) {
					sendError(NULL, "received invalid TX_CONFIRM payload");
					break;
				}

				// Decode port name string
				char portName[portStrLen + 1] = {0};
				memcpy(portName, this->pckg_buf + 2, portStrLen);

				// Decode transmission id
				unsigned int rxid =
						(this->pckg_buf[2 + portStrLen] & 0xFF) << 24 |
						(this->pckg_buf[3 + portStrLen] & 0xFF) << 16 |
						(this->pckg_buf[4 + portStrLen] & 0xFF) << 8 |
						(this->pckg_buf[5 + portStrLen] & 0xFF) << 0;

				printf("DEBUG: rx confirm: %s [rx %u]\n", portName, rxid);

#endif

				break;
			}
			case OPC_TX_CONFIRM: {

				// Check payload length
				if (this->pckg_len < 6) {
					sendError(NULL, "received incomplete TX_CONFIRM control frame");
					break;
				}

				// Decode port name length
				unsigned short portStrLen =
						(this->pckg_buf[0] & 0xFF) << 8 |
						(this->pckg_buf[1] & 0xFF) << 0;

				// Check port name length
				if (portStrLen > this->pckg_recv - 2) {
					sendError(NULL, "received invalid TX_CONFIRM payload");
					break;
				}

				// Decode port name string
				char portName[portStrLen + 1] = {0};
				memcpy(portName, this->pckg_buf + 2, portStrLen);

				// Decode transmission id
				unsigned int rxid =
						(this->pckg_buf[2 + portStrLen] & 0xFF) << 24 |
						(this->pckg_buf[3 + portStrLen] & 0xFF) << 16 |
						(this->pckg_buf[4 + portStrLen] & 0xFF) << 8 |
						(this->pckg_buf[5 + portStrLen] & 0xFF) << 0;

				// Attempt to get port
				SOEPortHandler* portHandler = 0;
				try {
#ifdef SIDE_CLIENT
					// From the server we get the remote host name, so we need to map it to the local port
					string localPortName = this->remote2localPort.at(string(portName));
					portHandler = this->ports.at(localPortName);
#else
					portHandler = this->ports.at(string(portName));
#endif
				} catch (const std::out_of_range& e) {
#ifdef DEBUG_PRINTS
				printf("DEBUG: received tx confirm for closed port: %s [rx %u]\n", portName, rxid);
#endif
					break;
				}

				// Confirm reception
				portHandler->confirmTransmission(rxid);

#ifdef DEBUG_PRINTS
				printf("DEBUG: tx confirm: %s [rx %u]\n", portName, rxid);
#endif

				break;
			}
			case OPC_ERROR: {

				// Check payload length
				if (this->pckg_len < 2) {
					sendError(NULL, "received incomplete ERROR control frame");
					break;
				}

				// Decode port name length
				unsigned short portStrLen =
						(this->pckg_buf[0] & 0xFF) << 8 |
						(this->pckg_buf[1] & 0xFF) << 0;

				// Check port name length
				if (portStrLen > this->pckg_recv - 2) {
					sendError(NULL, "received invalid ERROR payload");
					break;
				}

				// Decode port name string
				char portName[portStrLen + 1] = {0};
				memcpy(portName, this->pckg_buf + 2, portStrLen);

				// Check if additional string available
				unsigned short msgStrLen = 0;
				if (this->pckg_len - 2 - portStrLen >= 2) {

					// Decode message length
					msgStrLen =
							(this->pckg_buf[2 + portStrLen + 0] & 0xFF) << 8 |
							(this->pckg_buf[2 + portStrLen + 1] & 0xFF) << 0;

					// Check message length
					if (msgStrLen > this->pckg_recv - portStrLen - 4) {
						printf("received invalid ERROR payload!\n");
						break;
					}

				}

				// Decode message string
				char message[msgStrLen + 1] = {0};
				memcpy(message, this->pckg_buf + portStrLen + 4, msgStrLen);

				if (msgStrLen == 0) {
					// If the second (message) string is empty, the first one (portName) is the message
					printf("received error frame: N/A : %s\n", portName);
				} else {
					printf("received error frame: %s : %s\n", portName, message);

#ifdef SIDE_CLIENT
					if (this->remote_port_name != 0 && strcmp(this->remote_port_name, portName) == 0) {

#ifdef DEBUG_PRINTS
						printf("DEBUG: received error frame for remot port open/close sequence: %s\n", portName);
#endif

						// Signal to current open/close sequence (if there is one)
						{
							unique_lock<mutex> lock(this->remote_port_waitm);
							this->remote_port_status = false;
							this->remote_port_waitc.notify_all();
						}

					}
#endif

				}

				break;
			}
#ifdef SIDE_CLIENT
			case OPC_OPENED: {

				// Check payload length
				if (this->pckg_len < 2) {
					sendError(NULL, "received incomplete ERROR control frame");
					break;
				}

				// Decode port name length
				unsigned short portStrLen =
						(this->pckg_buf[0] & 0xFF) << 8 |
						(this->pckg_buf[1] & 0xFF) << 0;

				// Check port name length
				if (portStrLen > this->pckg_recv - 2) {
					sendError(NULL, "received invalid ERROR payload");
					break;
				}

				// Decode port name string
				char portName[portStrLen + 1] = {0};
				memcpy(portName, this->pckg_buf + 2, portStrLen);

#ifdef DEBUG_PRINTS
				printf("DEBUG: received open confirm for remote port: %s\n", portName);
#endif

				// Signal to current open/close sequence (if there is one)
				if (this->remote_port_name != 0) {
					unique_lock<mutex> lock(this->remote_port_waitm);
					this->remote_port_status = strcmp(this->remote_port_name, portName) == 0;
					this->remote_port_waitc.notify_all();
				}

				break;
			}
#endif
			case OPC_CLOSED: {

				// Check payload length
				if (this->pckg_len < 2) {
					sendError(NULL, "received incomplete ERROR control frame");
					break;
				}

				// Decode port name length
				unsigned short portStrLen =
						(this->pckg_buf[0] & 0xFF) << 8 |
						(this->pckg_buf[1] & 0xFF) << 0;

				// Check port name length
				if (portStrLen > this->pckg_recv - 2) {
					sendError(NULL, "received invalid ERROR payload");
					break;
				}

				// Decode port name string
				char portName[portStrLen + 1] = {0};
				memcpy(portName, this->pckg_buf + 2, portStrLen);

#ifdef SIDE_CLIENT

				// If a open/close sequence is currently pending for this port
				if (this->remote_port_name != 0 && strcmp(this->remote_port_name, portName) == 0) {
#ifdef DEBUG_PRINTS
					printf("DEBUG: received close confirm for remote port: %s\n", portName);
#endif
					// Signal to current open/close sequence (if there is one)
					{
						unique_lock<mutex> lock(this->remote_port_waitm);
						this->remote_port_status = true;
						this->remote_port_waitc.notify_all();
					}

				} else {
#ifdef DEBUG_PRINTS
					printf("DEBUG: received close notification for remote port: %s\n", portName);
#endif
					// Attempt to get and close local port
					try {
						// From the server we get the remote host name, so we need to map it to the local port
						string localPortName = this->remote2localPort.at(string(portName));
						SOEPortHandler* portHandler = this->ports.at(localPortName);

						delete portHandler;
						this->ports.erase(localPortName);
						this->remote2localPort.erase(string(portName));
						printf("closed local port: %s\n", localPortName.c_str());
					} catch (const std::out_of_range& e) {
#ifdef DEBUG_PRINTS
					printf("DEBUG: received close notification for remote port which's local port is already closed: %s\n", portName);
#endif
						break;
					}

				}

#endif
#ifdef SIDE_SERVER
#ifdef DEBUG_PRINTS
				printf("DEBUG: received close notification: %s\n", portName);
#endif

				// Attempt to get and close port
				try {
					SOEPortHandler* portHandler = this->ports.at(string(portName));

					delete portHandler;
					this->ports.erase(string(portName));
					printf("closed port: %s\n", portName);
				} catch (const std::out_of_range& e) {
#ifdef DEBUG_PRINTS
				printf("DEBUG: received close notification for already closed port: %s\n", portName);
#endif
					break;
				}

#endif

				break;
			}
			default:
#ifdef DEBUG_PRINTS
				printf("DEBUG: received invalid control frame: %x\n", op_code);
#endif
				sendError(NULL, "received invalid control frame");
			}

			// Free payload buffer and reset frame, if data of an another frame remained, move it to the front and process in next loop
			delete[] this->pckg_buf;
			this->pckg_buf = 0;
			this->pckg_recv = 0;
			this->pckg_len = 0;
			this->op_code = -1;

		}

	}

	rxend:

	// Release all ports
	for (auto entry = this->ports.begin(); entry != this->ports.end(); entry++) {
		printf("auto close port: %s\n", entry->first.c_str());
		delete entry->second;
	}
	this->ports.clear();

	// Free payload buffer
	if (this->pckg_buf != 0) {
		delete[] this->pckg_buf;
		this->pckg_buf = 0;
	}

	// Reset frame state
	this->op_code = -1;
	this->pckg_len = 0;
	this->pckg_recv = 0;
}

#ifdef SIDE_CLIENT

bool SOESocketHandler::openRemotePort(const char* remotePortName, unsigned int baud, const char* localPortName, unsigned int timeoutms) {

	// Attempt and wait to for port open
	cv_status status;
	{
		unique_lock<mutex> lock(this->remote_port_waitm);
		this->remote_port_status = false;
		this->remote_port_name = new char[strlen(remotePortName) + 1] {0};
		strcpy(this->remote_port_name, remotePortName);
		if (!sendOpenRequest(remotePortName, baud)) return false;
		status = this->remote_port_waitc.wait_for(lock, std::chrono::milliseconds(timeoutms));
		delete[] this->remote_port_name;
		this->remote_port_name = 0;
	}

	// If no response, attempt close port
	if (status == std::cv_status::timeout) {
		printf("failed to claim remote port %s, connection timed out\n", remotePortName);
		if (!closeRemotePort(remotePortName, timeoutms)) {
			printf("close remote port %s failed, port might still be open on server!\n", remotePortName);
		}
		return false;
	}

	// If remote claim failed, abbort
	if (!this->remote_port_status) return false;

	// Attempt to open port
	SerialPort* port = new SerialPort(localPortName);
	if (!port->openPort()) {
		printf("failed to claim local port %s, close remote port %s!\n", localPortName, remotePortName);
		delete port;

		if (!closeRemotePort(remotePortName, timeoutms)) {
			printf("close remote port %s failed, port might still be open on server!\n", remotePortName);
		}
		return false;
	}
	port->setBaud(baud);
	port->setTimeouts(SERIAL_RX_TIMEOUT, SERIAL_TX_TIMEOUT);
	SOEPortHandler* portHandler = new SOEPortHandler(this, *port, localPortName);
	this->ports[string(localPortName)] = portHandler;
	this->remote2localPort[string(remotePortName)] = string(localPortName);

	return true;

}

bool SOESocketHandler::closeRemotePort(const char* remotePortName, unsigned int timeoutms) {

	// Attempt and wait for port close
	cv_status status;
	{
		unique_lock<mutex> lock(this->remote_port_waitm);
		this->remote_port_status = false;
		this->remote_port_name = new char[strlen(remotePortName) + 1] {0};
		strcpy(this->remote_port_name, remotePortName);
		if (!sendCloseRequest(remotePortName)) return false;
		status = this->remote_port_waitc.wait_for(lock, std::chrono::milliseconds(timeoutms));
		delete[] this->remote_port_name;
		this->remote_port_name = 0;
	}

	// If no response, failed
	if (status == std::cv_status::timeout) return false;

	// If error response received, the port is still closed, so just log it and return success
	if (!this->remote_port_status) {
		printf("remote port %s close failed on server, port probably already closed!\n", remotePortName);
	}

	// Attempt to find and close local port, if no port registered, skip this step
	try {
		string localPortName = this->remote2localPort.at(string(remotePortName));
		SOEPortHandler* portHandler = this->ports.at(localPortName);

		// Close port
		delete portHandler;
		this->ports.erase(localPortName);
		this->remote2localPort.erase(string(remotePortName));

		return true;
	} catch (const std::out_of_range& e) {
		return true;
	}

}

#endif

// Sends an response frame
bool SOESocketHandler::sendFrame(char opc, const char* payload, unsigned int length) {

	if (!this->socket->isOpen()) return false;

	// Allocate frame buffer
	size_t frameLen = FRAME_HEADER_LEN + length;
	char buffer[frameLen] = {0};

	// Assemble frame header
	buffer[0] = opc;
	buffer[4] = (length >> 0) & 0xFF;
	buffer[3] = (length >> 8) & 0xFF;
	buffer[2] = (length >> 16) & 0xFF;
	buffer[1] = (length >> 24) & 0xFF;

	// Copy payload
	memcpy(buffer + FRAME_HEADER_LEN, payload, length);

	// Transmit frame
	if (!this->socket->send(buffer, frameLen)) {
		printf("FRAME ERROR: failed to transmit frame!\n");
		return false;
	}

	return true;
}

// Sends an error message response frame
void SOESocketHandler::sendError(const char* portName, const char* msg) {

	if (!this->socket->isOpen()) return;

#ifdef SIDE_CLIENT
	// On the client we usualy use the local port name, so we have to map it to the remote port name
	if (portName != 0) {
		string portNameS = string(portName);
		for (const auto& kv : this->remote2localPort) {
			if (kv.second == portNameS) {
				portName = kv.first.c_str();
				break;
			}
		}
	}
#endif

	// Get message and portName name length
	unsigned int portLen = portName != 0 ? strlen(portName) : 0;
	unsigned int msgLen = msg != 0 ? strlen(msg) : 0;
	if (portLen > 0xFFFF) portLen = 0xFFFF;
	if (msgLen > 0xFFFF) msgLen = 0xFFFF;

	// Allocate payload buffer
	unsigned int payloadLen = (portLen > 0 ? 2 + portLen : 0) + (msgLen > 0 ? 2 + msgLen : 0);
	char buffer[payloadLen] = {0};

	// Encode port name
	if (portLen > 0) {
		buffer[0] = (portLen >> 8) & 0xFF;
		buffer[1] = (portLen >> 0) & 0xFF;
		memcpy(buffer + 2, portName, portLen);
		portLen += 2; // This allows us to make the next statements simpler
	}

	// Encode message
	if (msgLen > 0) {
		buffer[0 + portLen] = (msgLen >> 8) & 0xFF;
		buffer[1 + portLen] = (msgLen >> 0) & 0xFF;
		memcpy(buffer + 2 + portLen, msg, msgLen);
	}

	// Transmit payload in ERROR frame, ignore result, we don't care about an error-error ...
	sendFrame(OPC_ERROR, buffer, payloadLen);

}

// Sends an port open/close reponse frame
bool SOESocketHandler::sendClaimStatus(bool claimed, const char* portName) {

	if (!this->socket->isOpen()) return false;

#ifdef SIDE_CLIENT
	// On the client we usualy use the local port name, so we have to map it to the remote port name
	string portNameS = string(portName);
	for (const auto& kv : this->remote2localPort) {
		if (kv.second == portNameS) {
			portName = kv.first.c_str();
			break;
		}
	}
#endif

	// Get port name length
	unsigned int portLen = strlen(portName);
	if (portLen > 0xFFFF) portLen = 0xFFFF;

	// Allocate payload buffer
	unsigned int payloadLen = 2 + portLen;
	char buffer[payloadLen] = {0};

	// Encode port name
	buffer[0] = (portLen >> 8) & 0xFF;
	buffer[1] = (portLen >> 0) & 0xFF;
	memcpy(buffer + 2, portName, portLen);

	// Transmit payload in OPENED or ERROR frame
	return sendFrame(claimed ? OPC_OPENED : OPC_CLOSED, buffer, payloadLen);

}

// Sends an transmission confirm frame
bool SOESocketHandler::sendConfirm(bool transmission, const char* portName, unsigned int txid) {

	if (!this->socket->isOpen()) return false;

#ifdef SIDE_CLIENT
	// On the client we usualy use the local port name, so we have to map it to the remote port name
	string portNameS = string(portName);
	for (const auto& kv : this->remote2localPort) {
		if (kv.second == portNameS) {
			portName = kv.first.c_str();
			break;
		}
	}
#endif

	// Get port name length
	unsigned int portLen = strlen(portName);
	if (portLen > 0xFFFF) portLen = 0xFFFF;

	// Allocate payload buffer
	unsigned int payloadLen = 6 + portLen;
	char buffer[payloadLen] = {0};

	// Encode port name
	buffer[0] = (portLen >> 8) & 0xFF;
	buffer[1] = (portLen >> 0) & 0xFF;
	memcpy(buffer + 2, portName, portLen);

	// Encode txid
	buffer[2 + portLen] = (txid >> 24) & 0xFF;
	buffer[3 + portLen] = (txid >> 16) & 0xFF;
	buffer[4 + portLen] = (txid >> 8) & 0xFF;
	buffer[5 + portLen] = (txid >> 0) & 0xFF;

	// Transmit payload in RX_CONFIRM or TX_CONFIRM frame
	return sendFrame(transmission ? OPC_TX_CONFIRM : OPC_RX_CONFIRM, buffer, payloadLen);

}

// Send payload stream frame
bool SOESocketHandler::sendStream(const char* portName, unsigned int rxid, const char* payload, unsigned long length) {

	if (!this->socket->isOpen()) return false;

#ifdef SIDE_CLIENT
	// On the client we usualy use the local port name, so we have to map it to the remote port name
	string portNameS = string(portName);
	for (const auto& kv : this->remote2localPort) {
		if (kv.second == portNameS) {
			portName = kv.first.c_str();
			break;
		}
	}
#endif

	// Get port name length
	unsigned int portLen = strlen(portName);
	if (portLen > 0xFFFF) portLen = 0xFFFF;

	// Allocate payload buffer
	unsigned int payloadLen = 6 + portLen + length;
	char buffer[payloadLen] = {0};

	// Encode port name
	buffer[0] = (portLen >> 8) & 0xFF;
	buffer[1] = (portLen >> 0) & 0xFF;
	memcpy(buffer + 2, portName, portLen);

	// Encode rxid
	buffer[2 + portLen] = (rxid >> 24) & 0xFF;
	buffer[3 + portLen] = (rxid >> 16) & 0xFF;
	buffer[4 + portLen] = (rxid >> 8) & 0xFF;
	buffer[5 + portLen] = (rxid >> 0) & 0xFF;

	// Copy payload in buffer
	memcpy(buffer + 6 + portLen, payload, length);

	// Transmit payload in STREAM frame
	return sendFrame(OPC_STREAM, buffer, payloadLen);

}

#ifdef SIDE_CLIENT

// Sends an port open request frame
bool SOESocketHandler::sendOpenRequest(const char* portName, unsigned int baud) {

	if (!this->socket->isOpen()) return false;

	// Get port name length
	unsigned int portLen = strlen(portName);
	if (portLen > 0xFFFF) portLen = 0xFFFF;

	// Allocate payload buffer
	unsigned int payloadLen = 6 + portLen;
	char buffer[payloadLen] = {0};

	// Encode baud rate
	buffer[0] = (baud >> 24) & 0xFF;
	buffer[1] = (baud >> 16) & 0xFF;
	buffer[2] = (baud >> 8) & 0xFF;
	buffer[3] = (baud >> 0) & 0xFF;

	// Encode port name
	buffer[4] = (portLen >> 8) & 0xFF;
	buffer[5] = (portLen >> 0) & 0xFF;
	memcpy(buffer + 6, portName, portLen);

	// Transmit payload in OPENED or ERROR frame
	return sendFrame(OPC_OPEN, buffer, payloadLen);

}

// Sends an close request frame
bool SOESocketHandler::sendCloseRequest(const char* portName) {

	if (!this->socket->isOpen()) return false;

	// Get port name length
	unsigned int portLen = strlen(portName);
	if (portLen > 0xFFFF) portLen = 0xFFFF;

	// Allocate payload buffer
	unsigned int payloadLen = 2 + portLen;
	char buffer[payloadLen] = {0};

	// Encode port name
	buffer[0] = (portLen >> 8) & 0xFF;
	buffer[1] = (portLen >> 0) & 0xFF;
	memcpy(buffer + 2, portName, portLen);

	// Transmit payload in OPENED or ERROR frame
	return sendFrame(OPC_CLOSE, buffer, payloadLen);

}

#endif
