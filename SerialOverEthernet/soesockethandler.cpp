/*
 * soeclient.cpp
 *
 *  Created on: 04.02.2025
 *      Author: marvi
 */

#include <stdexcept>
#include "soeimpl.hpp"

using namespace std;

// Initializes a new client handler for the supplied network socket
SOESocketHandler::SOESocketHandler(Socket* socket) {
	this->socket.reset(socket);
	this->ports = map<string, port_claim>();
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
}

// Returns true if the clients network connection is still open
bool SOESocketHandler::isActive() {
	return this->socket->isOpen();
}

void SOESocketHandler::handleClientTX() {

	// Start tx loop
	while (this->socket->isOpen()) {

		bool immediateWork = false;
		{	shared_lock<shared_timed_mutex> lock(this->portsm);

			chrono::time_point<chrono::steady_clock> now = chrono::steady_clock::now();
			for (auto entry = this->ports.begin(); entry != this->ports.end(); entry++) {

				INetAddress remoteAddress = entry->second.remote_address;
				string portName = entry->first;

				// Check if timeout has expired
				if (entry->second.point_of_timeout < now) {

					string address;
					unsigned int port = 0;
					remoteAddress.tostr(address, &port);
					printf("connection timed out, close port: %s %u : %s\n", address.c_str(), port, portName.c_str());

					// Close port
					lock.unlock();
					{ unique_lock<shared_timed_mutex> lock(this->portsm); this->ports.erase(entry--); }
					if (!sendClaimStatus(remoteAddress, false, portName)) {
						// If the error report fails too ... don't care at this point ...
						sendError(remoteAddress, portName, "failed to transmit CLOSE notification");
					}
					immediateWork = true;
					break;
				}

				// Request data from stack
				unsigned int rxid = 0;
				const char* payload = 0;
				unsigned long length = 0;
				if (!entry->second.handler->read(&rxid, &payload, &length)) {

					// If no data send keep alive
					if (entry->second.last_send + chrono::milliseconds(INET_KEEP_ALIVE_INTERVAL) < now) {
						length = 0;
						rxid = 0;
	#ifdef DEBUG_PRINTS
						printf("DEBUG: send keep alive: %s\n", entry->first.c_str());
	#endif
					} else {
						continue;
					}
				}

				// Send payload stream frame
				if (!sendStream(remoteAddress, portName.c_str(), rxid, payload, length)) {
					sendError(remoteAddress, portName.c_str(), "failed to transmit STREAM frame, close port");

					// Close port
					lock.unlock();
					{ unique_lock<shared_timed_mutex> lock(this->portsm); this->ports.erase(portName); }
					if (!sendClaimStatus(remoteAddress, false, portName.c_str())) {
						// If the error report fails too ... don't care at this point ...
						sendError(remoteAddress, portName, "failed to transmit CLOSE notification");
					}

					printf("closed port: %s\n", portName.c_str());

				}

				// Update last send timeout
				entry->second.last_send = now + chrono::microseconds(INET_KEEP_ALIVE_INTERVAL);

				immediateWork = true;

			}
		}

		// If no data available, wait for more, but repeat periodically to detect rx stack overflows
		if (!immediateWork) {
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
	INetAddress remoteAddress;
	char pckgBuffer[INET_RX_PCKG_LEN];
	unsigned int pckgLen;
	char* payload = pckgBuffer + SOE_FRAME_HEADER_LEN;
	unsigned int payloadLen = 0;
	char opc = -1;

	// Start rx loop
	while (this->socket->isOpen()) {

		if (!this->socket->receivefrom(remoteAddress, pckgBuffer, INET_RX_PCKG_LEN, &pckgLen)) {
			printf("network socket was closed, shutdown connection\n");
			break;
		}

		if (pckgLen < SOE_FRAME_HEADER_LEN) {
			printf("received incomplete control frame header, discard package!\n");
			continue;
		}

		opc = pckgBuffer[0];
		payloadLen = pckgLen - SOE_FRAME_HEADER_LEN;

		switch (opc) {
#ifdef SIDE_SERVER
		case OPC_OPEN: {

			// Check payload length
			if (payloadLen < 19) {
				sendError(remoteAddress, NULL, "received incomplete OPEN control frame");
				break;
			}

			// Decode serial configuration
			SerialPortConfiguration config = {
				(unsigned long) ( // baud rate
						(payload[0] & 0xFF) << 24 |
						(payload[1] & 0xFF) << 16 |
						(payload[2] & 0xFF) << 8 |
						(payload[3] & 0xFF) << 0),
				(unsigned char) ( // data bits
						(payload[4] & 0xFF)),
				(SerialPortStopBits) (
						(payload[5] & 0xFF) << 24 |
						(payload[6] & 0xFF) << 16 |
						(payload[7] & 0xFF) << 8 |
						(payload[8] & 0xFF) << 0),
				(SerialPortParity) (
						(payload[9] & 0xFF) << 24 |
						(payload[10] & 0xFF) << 16 |
						(payload[11] & 0xFF) << 8 |
						(payload[12] & 0xFF) << 0),
				(SerialPortFlowControl) (
						(payload[13] & 0xFF) << 24 |
						(payload[14] & 0xFF) << 16 |
						(payload[15] & 0xFF) << 8 |
						(payload[16] & 0xFF) << 0)
			};

			// Decode port name length
			unsigned short portStrLen =
					(payload[17] & 0xFF) << 8 |
					(payload[18] & 0xFF) << 0;

			// Check port name length
			if (portStrLen > payloadLen - 19) {
				sendError(remoteAddress, NULL, "received invalid OPEN payload");
				break;
			}

			// Decode port name string
			string portName = string(payload + 19, portStrLen);

			// Attempt to open port
			SerialPort* serialPort = new SerialPort(portName.c_str());
			if (!serialPort->openPort()) {
				sendError(remoteAddress, portName, "failed to claim port");
				delete serialPort;
				break;
			}

			serialPort->setConfig(config);
			serialPort->setTimeouts(SERIAL_RX_TIMEOUT, SERIAL_TX_TIMEOUT);

			SOEPortHandler* portHandler = new SOEPortHandler(serialPort, [this] { this->notifySerialData(); }, [this, remoteAddress, portName](unsigned int txid) {
				if (!this->sendConfirm(remoteAddress, true, portName, txid)) {
					this->sendError(remoteAddress, portName, "failed to transmit TX_CONFIRM");

				}
			});

			{
				unique_lock<shared_timed_mutex> lock(this->portsm);
				this->ports[portName] = {
						std::unique_ptr<SOEPortHandler>(portHandler),
						remoteAddress,
						chrono::steady_clock::now() + chrono::milliseconds(INET_KEEP_ALIVE_TIMEOUT),
						chrono::steady_clock::now() + chrono::milliseconds(INET_KEEP_ALIVE_INTERVAL)
				};
			}

			// Confirm that the port has been opened, close port if this fails, to avoid unused open ports
			if (!sendClaimStatus(remoteAddress, true, portName)) {
				sendError(remoteAddress, portName, "failed to complete OPENED confirmation, close port");
				{ unique_lock<shared_timed_mutex> lock(this->portsm); this->ports.erase(portName); }
			}

			string address;
			unsigned int port = 0;
			remoteAddress.tostr(address, &port);
			printf("opened port: %s %u : %s\n", address.c_str(), port, portName.c_str());

			break;
		}
		case OPC_CLOSE: {

			// Check payload length
			if (payloadLen < 2) {
				sendError(remoteAddress, NULL, "received incomplete CLOSE control frame");
				break;
			}

			// Decode port name length
			unsigned short portStrLen =
					(payload[0] & 0xFF) << 8 |
					(payload[1] & 0xFF) << 0;

			// Check port name length
			if (portStrLen > payloadLen - 2) {
				sendError(remoteAddress, NULL, "received invalid CLOSE payload");
				break;
			}

			// Decode port name string
			string portName = string(payload + 2, portStrLen);

			// Attempt to close port
			bool closed = false;
			{
				unique_lock<shared_timed_mutex> lock(this->portsm);
				closed = this->ports.count(portName);
				this->ports.erase(portName);
			}
			if (!closed) {
				sendError(remoteAddress, portName.c_str(), "port not claimed");
				break;
			}

			// Confirm that the port has been closed
			if (!sendClaimStatus(remoteAddress, false, portName)) {
				// If the error report fails too ... don't care at this point ...
				sendError(remoteAddress, portName, "failed to transmit CLOSE confirmation");
			}

			printf("closed port: %s\n", portName.c_str());

			break;
		}
#endif
		case OPC_STREAM: {

			// Check payload length
			if (payloadLen < 6) {
				sendError(remoteAddress, "", "received incomplete STREAM control frame");
				break;
			}

			// Decode port name length
			unsigned short portStrLen =
					(payload[0] & 0xFF) << 8 |
					(payload[1] & 0xFF) << 0;

			// Check port name length
			if (portStrLen > payloadLen - 6) {
				sendError(remoteAddress, "", "received invalid STREAM payload");
				break;
			}

			// Decode port name string
			char portName[portStrLen + 1] = {0};
			memcpy(portName, payload + 2, portStrLen);

			// Decode transmission id
			unsigned int txid =
					(payload[2 + portStrLen] & 0xFF) << 24 |
					(payload[3 + portStrLen] & 0xFF) << 16 |
					(payload[4 + portStrLen] & 0xFF) << 8 |
					(payload[5 + portStrLen] & 0xFF) << 0;

			// Attempt to get port
			shared_lock<shared_timed_mutex> lock(this->portsm);
			port_claim* portClaim = 0;
			try {
#ifdef SIDE_CLIENT
				// From the server we get the remote host name, so we need to map it to the local port
				string localPortName = this->remote2localPort.at(portName);
				portClaim = &this->ports.at(localPortName);
#else
				portClaim = &this->ports.at(portName);
#endif
			} catch (const std::out_of_range& e) {
				sendError(remoteAddress, portName, "port not claimed");
				break;
			}

			// Check if port was closed unexpectedly
			if (!portClaim->handler->isOpen()) {
				sendError(remoteAddress, portName, "port is already closed");

				// Close port
				lock.unlock();
				{ unique_lock<shared_timed_mutex> lock(this->portsm); this->ports.erase(portName); }
				if (!sendClaimStatus(remoteAddress, false, portName)) {
					// If the error report fails too ... don't care at this point ...
					sendError(remoteAddress, portName, "failed to transmit CLOSE notification");
				}

				printf("closed port: %s\n", portName);

				break;
			}

			// Put remaining payload on transmission stack
			const char* serialData = payload + 6 + portStrLen;
			size_t serialLen = payloadLen - 6 - portStrLen;
			if (serialLen > 0) {
				if (!portClaim->handler->send(txid, serialData, serialLen)) {
					printf("DEBUG: could not process package, tx stack full: [tx %u] %s\n", txid, portName);
				}
			}
#ifdef DEBUG_PRINTS
			else {
				printf("DEBUG: received keep alive: %s\n", portName);
			}
#endif

			// Update keep alive timeout
			portClaim->point_of_timeout = chrono::steady_clock::now() + chrono::milliseconds(INET_KEEP_ALIVE_TIMEOUT);

			sendConfirm(remoteAddress, false, portName, txid);
			break;
		}
		case OPC_RX_CONFIRM: {

			// Check payload length
			if (payloadLen < 6) {
				sendError(remoteAddress, "", "received incomplete TX_CONFIRM control frame");
				break;
			}

			// Decode port name length
			unsigned short portStrLen =
					(payload[0] & 0xFF) << 8 |
					(payload[1] & 0xFF) << 0;

			// Check port name length
			if (portStrLen > payloadLen - 6) {
				sendError(remoteAddress, "", "received invalid TX_CONFIRM payload");
				break;
			}

			// Decode port name string
			string portName = string(payload + 2, portStrLen);

			// Decode transmission id
			unsigned int rxid =
					(payload[2 + portStrLen] & 0xFF) << 24 |
					(payload[3 + portStrLen] & 0xFF) << 16 |
					(payload[4 + portStrLen] & 0xFF) << 8 |
					(payload[5 + portStrLen] & 0xFF) << 0;

			// Attempt to get port
			shared_lock<shared_timed_mutex> lock(this->portsm);
			port_claim* portClaim = 0;
			try {
#ifdef SIDE_CLIENT
				// From the server we get the remote host name, so we need to map it to the local port
				string localPortName = this->remote2localPort.at(portName);
				portClaim = &this->ports.at(localPortName);
#else
				portClaim = &this->ports.at(portName);
#endif
			} catch (const std::out_of_range& e) {
				break;
			}

			// Confirm reception
			portClaim->handler->confirmReception(rxid);

			break;
		}
		case OPC_TX_CONFIRM: {

			// Check payload length
			if (payloadLen < 6) {
				sendError(remoteAddress, "", "received incomplete TX_CONFIRM control frame");
				break;
			}

			// Decode port name length
			unsigned short portStrLen =
					(payload[0] & 0xFF) << 8 |
					(payload[1] & 0xFF) << 0;

			// Check port name length
			if (portStrLen > payloadLen - 6) {
				sendError(remoteAddress, "", "received invalid TX_CONFIRM payload");
				break;
			}

			// Decode port name string
			string portName = string(payload + 2, portStrLen);

			// Decode transmission id
			unsigned int rxid =
					(payload[2 + portStrLen] & 0xFF) << 24 |
					(payload[3 + portStrLen] & 0xFF) << 16 |
					(payload[4 + portStrLen] & 0xFF) << 8 |
					(payload[5 + portStrLen] & 0xFF) << 0;

			// Attempt to get port
			shared_lock<shared_timed_mutex> lock(this->portsm);
			port_claim* portClaim = 0;
			try {
#ifdef SIDE_CLIENT
				// From the server we get the remote host name, so we need to map it to the local port
				string localPortName = this->remote2localPort.at(portName);
				portClaim = &this->ports.at(localPortName);
#else
				portClaim = &this->ports.at(portName);
#endif
			} catch (const std::out_of_range& e) {
#ifdef DEBUG_PRINTS
				printf("DEBUG: received tx confirm for closed port: %s [rx %u]\n", portName.c_str(), rxid);
#endif
				break;
			}

			// Confirm reception
			portClaim->handler->confirmTransmission(rxid);

			break;
		}
		case OPC_ERROR: {

			// Check payload length
			if (payloadLen < 2) {
				sendError(remoteAddress, "", "received incomplete ERROR control frame");
				break;
			}

			// Decode port name length
			unsigned short portStrLen =
					(payload[0] & 0xFF) << 8 |
					(payload[1] & 0xFF) << 0;

			// Check port name length
			if (portStrLen > payloadLen - 2) {
				sendError(remoteAddress, "", "received invalid ERROR payload");
				break;
			}

			// Decode port name string
			string portName = string(payload + 2, portStrLen);

			// Check if additional string available
			unsigned short msgStrLen = 0;
			if (payloadLen - 2 - portStrLen >= 2) {

				// Decode message length
				msgStrLen =
						(payload[2 + portStrLen + 0] & 0xFF) << 8 |
						(payload[2 + portStrLen + 1] & 0xFF) << 0;

				// Check message length
				if (msgStrLen > payloadLen - portStrLen - 4) {
					printf("received invalid ERROR payload!\n");
					break;
				}

			}

			// Decode message string
			string message = string(payload + 4 + portStrLen, msgStrLen);

			if (msgStrLen == 0) {
				// If the second (message) string is empty, the first one (portName) is the message
				printf("received error frame: N/A : %s\n", portName.c_str());
			} else {
				printf("received error frame: %s : %s\n", portName.c_str(), message.c_str());

#ifdef SIDE_CLIENT
				if (this->remote_port_name.length() > 0 && this->remote_port_name == portName) {

#ifdef DEBUG_PRINTS
					printf("DEBUG: received error frame for remot port open/close sequence: %s\n", portName.c_str());
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
			if (payloadLen < 2) {
				sendError(remoteAddress, "", "received incomplete OPENED control frame");
				break;
			}

			// Decode port name length
			unsigned short portStrLen =
					(payload[0] & 0xFF) << 8 |
					(payload[1] & 0xFF) << 0;

			// Check port name length
			if (portStrLen > payloadLen - 2) {
				sendError(remoteAddress, "", "received invalid OPENED payload");
				break;
			}

			// Decode port name string
			string portName = string(payload + 2, portStrLen);

#ifdef DEBUG_PRINTS
			printf("DEBUG: received open confirm for remote port: %s\n", portName.c_str());
#endif

			// Signal to current open/close sequence (if there is one)
			if (this->remote_port_name.length() > 0) {
				unique_lock<mutex> lock(this->remote_port_waitm);
				this->remote_port_status = this->remote_port_name == portName;
				this->remote_port_waitc.notify_all();
			}

			break;
		}
#endif
		case OPC_CLOSED: {

			// Check payload length
			if (payloadLen < 2) {
				sendError(remoteAddress, "", "received incomplete CLOSED control frame");
				break;
			}

			// Decode port name length
			unsigned short portStrLen =
					(payload[0] & 0xFF) << 8 |
					(payload[1] & 0xFF) << 0;

			// Check port name length
			if (portStrLen > payloadLen - 2) {
				sendError(remoteAddress, "", "received invalid CLOSED payload");
				break;
			}

			// Decode port name string
			string portName = string(payload + 2, portStrLen);

#ifdef SIDE_CLIENT

			// If a open/close sequence is currently pending for this port
			if (this->remote_port_name == portName) {
#ifdef DEBUG_PRINTS
				printf("DEBUG: received close confirm for remote port: %s\n", portName.c_str());
#endif
				// Signal to current open/close sequence (if there is one)
				{
					unique_lock<mutex> lock(this->remote_port_waitm);
					this->remote_port_status = true;
					this->remote_port_waitc.notify_all();
				}

			} else {
#ifdef DEBUG_PRINTS
				printf("DEBUG: received close notification for remote port: %s\n", portName.c_str());
#endif
				// Attempt to get and close local port
				{
					// From the server we get the remote host name, so we need to map it to the local port
					unique_lock<shared_timed_mutex> lock(this->portsm);
					string localPortName = this->remote2localPort.at(portName);
					this->ports.erase(localPortName);
					this->remote2localPort.erase(portName);
					printf("closed local port: %s\n", localPortName.c_str());
				}

			}

#endif
#ifdef SIDE_SERVER
#ifdef DEBUG_PRINTS
			printf("DEBUG: received close notification: %s\n", portName.c_str());
#endif

			// Attempt to get and close port
			{ unique_lock<shared_timed_mutex> lock(this->portsm); this->ports.erase(portName); }
			printf("closed port: %s\n", portName.c_str());

#endif

			break;
		}
		default:
#ifdef DEBUG_PRINTS
			printf("DEBUG: received invalid control frame: %x\n", opc);
#endif
			sendError(remoteAddress, "", "received invalid control frame");
		}

	}

	// Release all ports
	unique_lock<shared_timed_mutex> lock(this->portsm);
	for (auto entry = this->ports.cbegin(); entry != this->ports.cend(); entry++) {
		printf("auto close port: %s\n", entry->first.c_str());
	}
	this->ports.clear();

}

#ifdef SIDE_CLIENT

bool SOESocketHandler::openRemotePort(const INetAddress& remoteAddress, const string& remotePortName, const SerialPortConfiguration config, const string& localPortName, unsigned int timeoutms) {

	// Attempt and wait to for port open
	cv_status status;
	{
		unique_lock<mutex> lock(this->remote_port_waitm);
		this->remote_port_status = false;
		this->remote_port_name = remotePortName;
		if (!sendOpenRequest(remoteAddress, remotePortName, config)) return false;
		status = this->remote_port_waitc.wait_for(lock, std::chrono::milliseconds(timeoutms));
		this->remote_port_name.clear();
	}

	// If no response, attempt close port
	if (status == std::cv_status::timeout) {
		printf("failed to claim remote port %s, connection timed out\n", remotePortName.c_str());
		if (!closeRemotePort(remoteAddress, remotePortName, timeoutms)) {
			printf("failed to close remote port %s, port might still be open on server!\n", remotePortName.c_str());
		}
		return false;
	}

	// If remote claim failed, abbort
	if (!this->remote_port_status) return false;

	// Attempt to open local port
	SerialPort* port = new SerialPort(localPortName.c_str());
	if (!port->openPort()) {
		printf("failed to claim local port %s, close remote port %s!\n", localPortName.c_str(), remotePortName.c_str());
		delete port;

		if (!closeRemotePort(remoteAddress, remotePortName, timeoutms)) {
			printf("close remote port %s failed, port might still be open on server!\n", remotePortName.c_str());
		}
		return false;
	}
	port->setConfig(config);
	port->setTimeouts(SERIAL_RX_TIMEOUT, SERIAL_TX_TIMEOUT);
	SOEPortHandler* portHandler = new SOEPortHandler(port, [this] { this->notifySerialData(); }, [this, remoteAddress, localPortName](unsigned int txid) {
		if (!this->sendConfirm(remoteAddress, true, localPortName, txid)) {
			this->sendError(remoteAddress, localPortName, "failed to transmit TX_CONFIRM");
		}
	});
	unique_lock<shared_timed_mutex> lock(this->portsm);
	this->ports[localPortName] = {std::unique_ptr<SOEPortHandler>(portHandler), remoteAddress, chrono::steady_clock::now() + chrono::milliseconds(INET_KEEP_ALIVE_TIMEOUT)};
	this->remote2localPort[remotePortName] = localPortName;

	return true;

}

bool SOESocketHandler::closeRemotePort(const INetAddress& remoteAddress, const string& remotePortName, unsigned int timeoutms) {

	// Attempt and wait for port close
	cv_status status;
	{
		unique_lock<mutex> lock(this->remote_port_waitm);
		this->remote_port_status = false;
		this->remote_port_name = remotePortName;
		if (!sendCloseRequest(remoteAddress, remotePortName)) return false;
		status = this->remote_port_waitc.wait_for(lock, std::chrono::milliseconds(timeoutms));
		this->remote_port_name.clear();
	}

	// If no response, failed
	if (status == std::cv_status::timeout) return false;

	// If error response received, the port is still closed, so just log it and return success
	if (!this->remote_port_status) {
		printf("remote port %s close failed on server, port probably already closed!\n", remotePortName.c_str());
	}

	// Attempt to find and close local port, if no port registered, skip this step
	try {
		unique_lock<shared_timed_mutex> lock(this->portsm);
		string localPortName = this->remote2localPort.at(remotePortName);
		this->ports.erase(localPortName);
		this->remote2localPort.erase(remotePortName);

		return true;
	} catch (const std::out_of_range& e) {
		return true;
	}

}

#endif

#ifdef SIDE_CLIENT
	const string& SOESocketHandler::local2remotePort(const string& name) {
		if (name.empty()) return name;
		for (const auto& kv : this->remote2localPort) {
			if (kv.second == name) {
				return kv.first;
			}
		}
		return name;
	}
#endif

// Sends an response frame
bool SOESocketHandler::sendFrame(const INetAddress& remoteAddress, char opc, const char* payload, unsigned int length) {

	if (!this->socket->isOpen()) return false;

	// Allocate frame buffer
	size_t frameLen = SOE_FRAME_HEADER_LEN + length;
	char buffer[frameLen] = {0};

	// Assemble frame
	buffer[0] = opc;
	memcpy(buffer + SOE_FRAME_HEADER_LEN, payload, length);

	// Transmit frame
	if (!this->socket->sendto(remoteAddress, buffer, frameLen)) {
		printf("FRAME ERROR: failed to transmit frame!\n");
		return false;
	}

	return true;
}

// Sends an error message response frame
void SOESocketHandler::sendError(const INetAddress& remoteAddress, const string& portName, const string& msg) {

	if (!this->socket->isOpen()) return;

#ifdef SIDE_CLIENT
	string remotePortName = local2remotePort(portName);
#else
	string remotePortName = portName;
#endif

	// Get message and portName name length
	unsigned int portLen = remotePortName.length();
	unsigned int msgLen = msg.length();
	if (portLen > 0xFFFF) portLen = 0xFFFF;
	if (msgLen > 0xFFFF) msgLen = 0xFFFF;

	// Allocate payload buffer
	unsigned int payloadLen = (portLen > 0 ? 2 + portLen : 0) + (msgLen > 0 ? 2 + msgLen : 0);
	char buffer[payloadLen] = {0};

	// Encode port name
	if (portLen > 0) {
		buffer[0] = (portLen >> 8) & 0xFF;
		buffer[1] = (portLen >> 0) & 0xFF;
		memcpy(buffer + 2, remotePortName.c_str(), portLen);
		portLen += 2; // This allows us to make the next statements simpler
	}

	// Encode message
	if (msgLen > 0) {
		buffer[0 + portLen] = (msgLen >> 8) & 0xFF;
		buffer[1 + portLen] = (msgLen >> 0) & 0xFF;
		memcpy(buffer + 2 + portLen, msg.c_str(), msgLen);
	}

	// Transmit payload in ERROR frame, ignore result, we don't care about an error-error ...
	sendFrame(remoteAddress, OPC_ERROR, buffer, payloadLen);

}

// Sends an port open/close reponse frame
bool SOESocketHandler::sendClaimStatus(const INetAddress& remoteAddress, bool claimed, const string& portName) {

	if (!this->socket->isOpen()) return false;

#ifdef SIDE_CLIENT
	string remotePortName = local2remotePort(portName);
#else
	string remotePortName = portName;
#endif

	// Get port name length
	unsigned int portLen = remotePortName.length();
	if (portLen > 0xFFFF) portLen = 0xFFFF;

	// Allocate payload buffer
	unsigned int payloadLen = 2 + portLen;
	char buffer[payloadLen] = {0};

	// Encode port name
	buffer[0] = (portLen >> 8) & 0xFF;
	buffer[1] = (portLen >> 0) & 0xFF;
	memcpy(buffer + 2, remotePortName.c_str(), portLen);

	// Transmit payload in OPENED or ERROR frame
	return sendFrame(remoteAddress, claimed ? OPC_OPENED : OPC_CLOSED, buffer, payloadLen);

}

// Sends an transmission confirm frame
bool SOESocketHandler::sendConfirm(const INetAddress& remoteAddress, bool transmission, const string& portName, unsigned int txid) {

	if (!this->socket->isOpen()) return false;

#ifdef SIDE_CLIENT
	string remotePortName = local2remotePort(portName);
#else
	string remotePortName = portName;
#endif

	// Get port name length
	unsigned int portLen = remotePortName.length();
	if (portLen > 0xFFFF) portLen = 0xFFFF;

	// Allocate payload buffer
	unsigned int payloadLen = 6 + portLen;
	char buffer[payloadLen] = {0};

	// Encode port name
	buffer[0] = (portLen >> 8) & 0xFF;
	buffer[1] = (portLen >> 0) & 0xFF;
	memcpy(buffer + 2, remotePortName.c_str(), portLen);

	// Encode txid
	buffer[2 + portLen] = (txid >> 24) & 0xFF;
	buffer[3 + portLen] = (txid >> 16) & 0xFF;
	buffer[4 + portLen] = (txid >> 8) & 0xFF;
	buffer[5 + portLen] = (txid >> 0) & 0xFF;

	// Transmit payload in RX_CONFIRM or TX_CONFIRM frame
	return sendFrame(remoteAddress, transmission ? OPC_TX_CONFIRM : OPC_RX_CONFIRM, buffer, payloadLen);

}

// Send payload stream frame
bool SOESocketHandler::sendStream(const INetAddress& remoteAddress, const string& portName, unsigned int rxid, const char* payload, unsigned long length) {

	if (!this->socket->isOpen()) return false;

#ifdef SIDE_CLIENT
	string remotePortName = local2remotePort(portName);
#else
	string remotePortName = portName;
#endif

	// Get port name length
	unsigned int portLen = remotePortName.length();
	if (portLen > 0xFFFF) portLen = 0xFFFF;

	// Allocate payload buffer
	unsigned int payloadLen = 6 + portLen + length;
	char buffer[payloadLen] = {0};

	// Encode port name
	buffer[0] = (portLen >> 8) & 0xFF;
	buffer[1] = (portLen >> 0) & 0xFF;
	memcpy(buffer + 2, remotePortName.c_str(), portLen);

	// Encode rxid
	buffer[2 + portLen] = (rxid >> 24) & 0xFF;
	buffer[3 + portLen] = (rxid >> 16) & 0xFF;
	buffer[4 + portLen] = (rxid >> 8) & 0xFF;
	buffer[5 + portLen] = (rxid >> 0) & 0xFF;

	// Copy payload in buffer
	memcpy(buffer + 6 + portLen, payload, length);

	// Transmit payload in STREAM frame
	return sendFrame(remoteAddress, OPC_STREAM, buffer, payloadLen);

}

#ifdef SIDE_CLIENT

// Sends an port open request frame
bool SOESocketHandler::sendOpenRequest(const INetAddress& remoteAddress, const string& portName, const SerialPortConfiguration& config) {

	if (!this->socket->isOpen()) return false;

	// Get port name length
	unsigned int portLen = portName.length();
	if (portLen > 0xFFFF) portLen = 0xFFFF;

	// Allocate payload buffer
	unsigned int payloadLen = 19 + portLen;
	char buffer[payloadLen] = {0};

	// Encode baud rate
	buffer[0] = (config.baudRate >> 24) & 0xFF;
	buffer[1] = (config.baudRate >> 16) & 0xFF;
	buffer[2] = (config.baudRate >> 8) & 0xFF;
	buffer[3] = (config.baudRate >> 0) & 0xFF;
	buffer[4] = config.dataBits & 0xFF;
	buffer[5] = (config.stopBits >> 24) & 0xFF;
	buffer[6] = (config.stopBits >> 16) & 0xFF;
	buffer[7] = (config.stopBits >> 8) & 0xFF;
	buffer[8] = (config.stopBits >> 0) & 0xFF;
	buffer[9] = (config.parity >> 24) & 0xFF;
	buffer[10] = (config.parity >> 16) & 0xFF;
	buffer[11] = (config.parity >> 8) & 0xFF;
	buffer[12] = (config.parity >> 0) & 0xFF;
	buffer[13] = (config.flowControl >> 24) & 0xFF;
	buffer[14] = (config.flowControl >> 16) & 0xFF;
	buffer[15] = (config.flowControl >> 8) & 0xFF;
	buffer[16] = (config.flowControl >> 0) & 0xFF;

	// Encode port name
	buffer[17] = (portLen >> 8) & 0xFF;
	buffer[18] = (portLen >> 0) & 0xFF;
	memcpy(buffer + 19, portName.c_str(), portLen);

	// Transmit payload in OPENED or ERROR frame
	return sendFrame(remoteAddress, OPC_OPEN, buffer, payloadLen);

}

// Sends an close request frame
bool SOESocketHandler::sendCloseRequest(const INetAddress& remoteAddress, const string& portName) {

	if (!this->socket->isOpen()) return false;

	// Get port name length
	unsigned int portLen = portName.length();
	if (portLen > 0xFFFF) portLen = 0xFFFF;

	// Allocate payload buffer
	unsigned int payloadLen = 2 + portLen;
	char buffer[payloadLen] = {0};

	// Encode port name
	buffer[0] = (portLen >> 8) & 0xFF;
	buffer[1] = (portLen >> 0) & 0xFF;
	memcpy(buffer + 2, portName.c_str(), portLen);

	// Transmit payload in OPENED or ERROR frame
	return sendFrame(remoteAddress, OPC_CLOSE, buffer, payloadLen);

}

#endif
