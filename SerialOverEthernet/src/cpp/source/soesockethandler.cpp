/*
 * soesockethandler.cpp
 *
 * Handles the individual connections going in and out trough an socket.
 * In this implementation there is usually only one socket, but nothing prevents multiple
 * ports from being opened.
 *
 *  Created on: 04.02.2025
 *      Author: Marvin Koehler
 */

#include <stdexcept>
#include <cstring>
#include "soeimpl.hpp"

// Initializes a new client handler for the supplied network socket
SerialOverEthernet::SOESocketHandler::SOESocketHandler(NetSocket::Socket* socket) {
	this->socket.reset(socket);
	this->ports = std::map<std::string, port_claim>();
	this->thread_rx = std::thread([this]() -> void {
		this->handleClientRX();
	});
	this->thread_tx = std::thread([this]() -> void {
		this->handleClientTX();
	});
}

// Shuts down the client handler and frees all resources (including ports opened by the client)
SerialOverEthernet::SOESocketHandler::~SOESocketHandler() {
	this->socket->close();
	{ std::unique_lock<std::mutex> lock(this->tx_waitm); }
	this->tx_waitc.notify_all();
	this->thread_rx.join();
	this->thread_tx.join();
}

// Returns true if the clients network connection is still open
bool SerialOverEthernet::SOESocketHandler::isActive() {
	return this->socket->isOpen();
}

void SerialOverEthernet::SOESocketHandler::handleClientTX() {

	// Start tx loop
	while (this->socket->isOpen()) {

		bool immediateWork = false;
		{
			std::shared_lock<std::shared_timed_mutex> lock(this->portsm);

			std::chrono::time_point<std::chrono::steady_clock> now = std::chrono::steady_clock::now();
			for (auto entry = this->ports.begin(); entry != this->ports.end(); entry++) {

				NetSocket::INetAddress remoteAddress = entry->second.remote_address;
				std::string remotePortName = entry->second.remote_port;
				std::string localPortName = entry->first;

				// Check if timeout has expired
				if (entry->second.point_of_timeout < now) {

					std::string address;
					unsigned int port = 0;
					remoteAddress.tostr(address, &port);
					printf("[!] connection timed out, close port: local %s : remote %s %u\n", localPortName.c_str(), address.c_str(), port);

					// Close port
					lock.unlock();
					{
						std::unique_lock<std::shared_timed_mutex> lock(this->portsm);
						this->ports.erase(entry--);
						this->remote2localPort.erase(std::make_pair(remoteAddress, remotePortName));
					}
					if (!sendClaimStatus(remoteAddress, false, localPortName)) {
						// If the error report fails too ... don't care at this point ...
						sendError(remoteAddress, localPortName, "failed to transmit CLOSE notification");
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
					if (entry->second.last_send + std::chrono::milliseconds(INET_KEEP_ALIVE_INTERVAL) < now) {
						length = 0;
						rxid = 0;
#ifdef DEBUG_PRINTS
						printf("DEBUG: send keep alive: %s -> %s\n", localPortName.c_str(), remotePortName.c_str());
#endif
					} else {
						continue;
					}
				}

				// Send payload stream frame
				if (!sendStream(remoteAddress, localPortName, rxid, payload, length)) {
					sendError(remoteAddress, localPortName, "failed to transmit STREAM frame, close port");

					// Close port
					lock.unlock();
					{
						std::unique_lock<std::shared_timed_mutex> lock(this->portsm);
						this->ports.erase(localPortName);
						this->remote2localPort.erase(make_pair(remoteAddress, remotePortName));
					}
					if (!sendClaimStatus(remoteAddress, false, localPortName)) {
						// If the error report fails too ... don't care at this point ...
						sendError(remoteAddress, localPortName, "failed to transmit CLOSE notification");
					}

					std::string address;
					unsigned int port = 0;
					remoteAddress.tostr(address, &port);
					printf("[!] transmission error, close port: remote %s @ %s %u\n", remotePortName.c_str(), address.c_str(), port);

				}

				// Update last send timeout
				entry->second.last_send = now + std::chrono::microseconds(INET_KEEP_ALIVE_INTERVAL);

				immediateWork = true;

			}
		}

		// If no data available, wait for more, but repeat periodically to detect rx stack overflows
		if (!immediateWork) {
			std::unique_lock<std::mutex> lock(this->tx_waitm);
			this->tx_waitc.wait_for(lock, std::chrono::milliseconds(INET_TX_REP_INTERVAL));
		}

	}

}

void SerialOverEthernet::SOESocketHandler::notifySerialData() {
	{ std::unique_lock<std::mutex> lock(this->tx_waitm); }
	this->tx_waitc.notify_all();
}

// Handles the incoming network requests
void SerialOverEthernet::SOESocketHandler::handleClientRX() {

	// Setup rx variables
	NetSocket::INetAddress remoteAddress;
	char pckgBuffer[INET_RX_PCKG_LEN];
	unsigned int pckgLen;
	char* payload = pckgBuffer + SOE_FRAME_HEADER_LEN;
	unsigned int payloadLen = 0;
	char opc = -1;

	// Start rx loop
	while (this->socket->isOpen()) {

		if (!this->socket->receivefrom(remoteAddress, pckgBuffer, INET_RX_PCKG_LEN, &pckgLen)) {
			printf("[!] network socket was closed, shutdown connection\n");
			break;
		}

		if (pckgLen < SOE_FRAME_HEADER_LEN) {
			printf("[!] received incomplete control frame header, discard package\n");
			continue;
		}

		opc = pckgBuffer[0];
		payloadLen = pckgLen - SOE_FRAME_HEADER_LEN;

		switch (opc) {
		case OPC_OPEN: {

			// Check payload length
			if (payloadLen < 19) {
				sendError(remoteAddress, NULL, "received incomplete OPEN control frame");
				break;
			}

			// Decode serial configuration
			SerialAccess::SerialPortConfiguration config = {
				(unsigned long) ( // baud rate
						(payload[0] & 0xFF) << 24 |
						(payload[1] & 0xFF) << 16 |
						(payload[2] & 0xFF) << 8 |
						(payload[3] & 0xFF) << 0),
				(unsigned char) ( // data bits
						(payload[4] & 0xFF)),
				(SerialAccess::SerialPortStopBits) (
						(payload[5] & 0xFF) << 24 |
						(payload[6] & 0xFF) << 16 |
						(payload[7] & 0xFF) << 8 |
						(payload[8] & 0xFF) << 0),
				(SerialAccess::SerialPortParity) (
						(payload[9] & 0xFF) << 24 |
						(payload[10] & 0xFF) << 16 |
						(payload[11] & 0xFF) << 8 |
						(payload[12] & 0xFF) << 0),
				(SerialAccess::SerialPortFlowControl) (
						(payload[13] & 0xFF) << 24 |
						(payload[14] & 0xFF) << 16 |
						(payload[15] & 0xFF) << 8 |
						(payload[16] & 0xFF) << 0)
			};

			// Decode port name length
			unsigned short remotePortStrLen =
					(payload[17] & 0xFF) << 8 |
					(payload[18] & 0xFF) << 0;

			// Check port name length
			if (remotePortStrLen > payloadLen - 19) {
				sendError(remoteAddress, NULL, "received invalid OPEN payload");
				break;
			}

			// Decode claim port name length
			unsigned short localPortStrLen =
					(payload[19 + remotePortStrLen] & 0xFF) << 8 |
					(payload[20 + remotePortStrLen] & 0xFF) << 0;

			// Check claim port name length
			if (localPortStrLen > payloadLen - 20 - remotePortStrLen) {
				sendError(remoteAddress, NULL, "received invalid OPEN payload");
				break;
			}

			// Decode port name strings
			std::string remotePortName(payload + 19, remotePortStrLen);
			std::string localPortName(payload + 21 + remotePortStrLen, localPortStrLen);

			// Check for name conflicts with existing port claims
			if (this->ports.count(localPortName)) {
				sendError(remoteAddress, localPortName, "port name conflict");
				break;
			}

			// Attempt to open port
			SerialAccess::SerialPort* serialPort = SerialAccess::newSerialPort(localPortName.c_str());
			if (!serialPort->openPort()) {
				sendError(remoteAddress, localPortName, "failed to claim port");
				delete serialPort;
				break;
			}
			if (!serialPort->setConfig(config) || !serialPort->setTimeouts(SERIAL_RX_TIMEOUT, SERIAL_TX_TIMEOUT)) {
				sendError(remoteAddress, localPortName, "failed to configure port");
				delete serialPort;
				break;
			}

			SerialOverEthernet::SOEPortHandler* portHandler = new SerialOverEthernet::SOEPortHandler(serialPort, [this] { this->notifySerialData(); }, [this, remoteAddress, localPortName](unsigned int txid) {
				if (!this->sendConfirm(remoteAddress, true, localPortName, txid)) {
					this->sendError(remoteAddress, localPortName, "failed to transmit TX_CONFIRM");
				}
			});

			{
				std::unique_lock<std::shared_timed_mutex> lock(this->portsm);
				this->ports[localPortName] = {
						std::unique_ptr<SOEPortHandler>(portHandler),
						remoteAddress,
						remotePortName,
						std::chrono::steady_clock::now() + std::chrono::milliseconds(INET_KEEP_ALIVE_TIMEOUT),
						std::chrono::steady_clock::now() + std::chrono::milliseconds(INET_KEEP_ALIVE_INTERVAL)
				};
				this->remote2localPort[make_pair(remoteAddress, remotePortName)] = localPortName;
			}

			// Confirm that the port has been opened, close port if this fails, to avoid unused open ports
			if (!sendClaimStatus(remoteAddress, true, localPortName)) {
				sendError(remoteAddress, localPortName, "failed to complete OPENED confirmation, close port");
				{
					std::unique_lock<std::shared_timed_mutex> lock(this->portsm);
					this->ports.erase(localPortName);
					this->remote2localPort.erase(make_pair(remoteAddress, remotePortName));
				}
			}

			std::string address;
			unsigned int port = 0;
			remoteAddress.tostr(address, &port);
			printf("[i] opened port from remote: local %s : remote %s @ %s %u\n", localPortName.c_str(), remotePortName.c_str(), address.c_str(), port);

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
			std::string remotePortName(payload + 2, portStrLen);

			try {
				std::string localPortName = this->remote2localPort.at(make_pair(remoteAddress, remotePortName));

				// Attempt to close port
				bool closed = false;
				{
					std::unique_lock<std::shared_timed_mutex> lock(this->portsm);
					closed = this->ports.count(localPortName);
					this->ports.erase(localPortName);
					this->remote2localPort.erase(make_pair(remoteAddress, remotePortName));
				}
				if (!closed) {
					sendError(remoteAddress, localPortName.c_str(), "port not claimed");
					break;
				}

				// Confirm that the port has been closed
				if (!sendClaimStatus(remoteAddress, false, localPortName)) {
					// If the error report fails too ... don't care at this point ...
					sendError(remoteAddress, localPortName, "failed to transmit CLOSE confirmation");
				}

				std::string address;
				unsigned int port = 0;
				remoteAddress.tostr(address, &port);
				printf("[i] closed port from remote: local %s : remote %s @ %s %u\n", localPortName.c_str(), remotePortName.c_str(), address.c_str(), port);

			} catch (std::out_of_range& e) {
				// This side does not know about this port link
				sendError(remoteAddress, "", "CLOSE: unknown link");
			}

			break;
		}
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
			std::string remotePortName(payload + 2, portStrLen);

			try {
				std::string localPortName = this->remote2localPort.at(make_pair(remoteAddress, remotePortName));

				// Decode transmission id
				unsigned int txid =
						(payload[2 + portStrLen] & 0xFF) << 24 |
						(payload[3 + portStrLen] & 0xFF) << 16 |
						(payload[4 + portStrLen] & 0xFF) << 8 |
						(payload[5 + portStrLen] & 0xFF) << 0;

				// Attempt to get port
				std::shared_lock<std::shared_timed_mutex> lock(this->portsm);
				port_claim* portClaim = &this->ports.at(localPortName);

				// Check if port was closed unexpectedly
				if (!portClaim->handler->isOpen()) {
					sendError(remoteAddress, localPortName, "port is already closed");

					// Close port
					lock.unlock();
					{
						std::unique_lock<std::shared_timed_mutex> lock(this->portsm);
						this->ports.erase(localPortName);
						this->remote2localPort.erase(make_pair(remoteAddress, remotePortName));
					}
					if (!sendClaimStatus(remoteAddress, false, localPortName)) {
						// If the error report fails too ... don't care at this point ...
						sendError(remoteAddress, localPortName, "failed to transmit CLOSE notification");
					}

					std::string address;
					unsigned int port = 0;
					remoteAddress.tostr(address, &port);
					printf("[i] port already closed: local %s : remote %s @ %s %u\n", localPortName.c_str(), remotePortName.c_str(), address.c_str(), port);

					break;
				}

				// Put remaining payload on transmission stack
				const char* serialData = payload + 6 + portStrLen;
				size_t serialLen = payloadLen - 6 - portStrLen;
				if (serialLen > 0) {
					if (!portClaim->handler->send(txid, serialData, serialLen)) {
#ifdef DEBUG_PRINTS
						printf("DEBUG: could not process package, tx stack full: [tx %u] %s\n", txid, localPortName.c_str());
#endif
					}
				}
				else {
#ifdef DEBUG_PRINTS
					printf("DEBUG: received keep alive: %s -> %s\n", remotePortName.c_str(), localPortName.c_str());
#endif
				}

				// Update keep alive timeout
				portClaim->point_of_timeout = std::chrono::steady_clock::now() + std::chrono::milliseconds(INET_KEEP_ALIVE_TIMEOUT);

				sendConfirm(remoteAddress, false, localPortName, txid);

			} catch (std::out_of_range& e) {
				// This side does not know about this port link
				sendError(remoteAddress, "", "STREAM: unknown link");
			}

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
			std::string remotePortName(payload + 2, portStrLen);

			try {
				std::string localPortName = this->remote2localPort.at(make_pair(remoteAddress, remotePortName));

				// Decode transmission id
				unsigned int rxid =
						(payload[2 + portStrLen] & 0xFF) << 24 |
						(payload[3 + portStrLen] & 0xFF) << 16 |
						(payload[4 + portStrLen] & 0xFF) << 8 |
						(payload[5 + portStrLen] & 0xFF) << 0;

				// Attempt to get port
				std::shared_lock<std::shared_timed_mutex> lock(this->portsm);
				port_claim* portClaim = &this->ports.at(localPortName);

				// Confirm reception
				portClaim->handler->confirmReception(rxid);

			} catch (std::out_of_range& e) {
				// This side does not know about this port link
				sendError(remoteAddress, "", "RX_CONFIRM: unknown link");
			}

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
			std::string remotePortName(payload + 2, portStrLen);

			// Decode transmission id
			unsigned int rxid =
					(payload[2 + portStrLen] & 0xFF) << 24 |
					(payload[3 + portStrLen] & 0xFF) << 16 |
					(payload[4 + portStrLen] & 0xFF) << 8 |
					(payload[5 + portStrLen] & 0xFF) << 0;

			try {
				std::string localPortName = this->remote2localPort.at(make_pair(remoteAddress, remotePortName));

				// Attempt to get port
				std::shared_lock<std::shared_timed_mutex> lock(this->portsm);
				port_claim* portClaim = &this->ports.at(localPortName);

				// Confirm reception
				portClaim->handler->confirmTransmission(rxid);

			} catch (std::out_of_range& e) {
#ifdef DEBUG_PRINTS
				printf("DEBUG: received tx confirm for closed port: remote %s [rx %u]\n", remotePortName.c_str(), rxid);
#endif
				// This side does not know about this port link
				sendError(remoteAddress, "", "TX_CONFIRM: unknown link");
			}

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
			std::string remotePortName(payload + 2, portStrLen);

			// Check if additional string available
			unsigned short msgStrLen = 0;
			std::string message;
			if (payloadLen - 2 - portStrLen >= 2) {

				// Decode message length
				msgStrLen =
						(payload[2 + portStrLen + 0] & 0xFF) << 8 |
						(payload[2 + portStrLen + 1] & 0xFF) << 0;

				// Check message length
				if (msgStrLen > payloadLen - portStrLen - 4) {
					printf("[!] received invalid ERROR payload\n");
					break;
				}

				// Decode message string
				message = std::string(payload + 4 + portStrLen, msgStrLen);

			}

			if (!remotePortName.empty()) {

				try {
					std::string localPortName = this->remote2localPort.at(make_pair(remoteAddress, remotePortName));

					std::string address;
					unsigned int port = 0;
					remoteAddress.tostr(address, &port);
					printf("[!] received error frame: local %s : remote %s @ %s %u : %s\n", localPortName.c_str(), remotePortName.c_str(), address.c_str(), port, message.c_str());

					// Check if this matches an ongoing port claim
					if (!this->remote_port_name.empty() && this->remote_port_name == remotePortName && this->remote_address == remoteAddress) {

#ifdef DEBUG_PRINTS
						printf("DEBUG: received error frame for remote port open/close sequence: remote %s : local %s\n", remotePortName.c_str(), localPortName.c_str());
#endif

						// Signal to current open/close sequence (if there is one)
						{
							std::unique_lock<std::mutex> lock(this->remote_port_waitm);
							this->remote_port_status = false;
							this->remote_port_waitc.notify_all();
						}

					}

				} catch (std::out_of_range& e) {
					// This side does not know about this port link
					sendError(remoteAddress, "", "ERROR: unknown link");
				}

			} else {
				std::string address;
				unsigned int port = 0;
				remoteAddress.tostr(address, &port);
				printf("[!] received error frame: local N/A : remote N/A @ %s %u : %s\n", address.c_str(), port, message.c_str());
			}

			break;
		}
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
			std::string remotePortName(payload + 2, portStrLen);

#ifdef DEBUG_PRINTS
			try {
				std::string localPortName = this->remote2localPort.at(make_pair(remoteAddress, remotePortName));
				printf("DEBUG: received open confirm for remote port: local %s : remote %s\n", localPortName.c_str(), remotePortName.c_str());
			} catch (std::out_of_range& e) {
				printf("DEBUG: received open confirm for remote port: local N/A : remote %s\n", remotePortName.c_str());
			}
#endif

			// Signal to current open/close sequence (if there is one)
			if (!this->remote_port_name.empty()) {
				std::unique_lock<std::mutex> lock(this->remote_port_waitm);
				this->remote_port_status = this->remote_port_name == remotePortName && this->remote_address == remoteAddress;
				this->remote_port_waitc.notify_all();
			}

			break;
		}
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
			std::string remotePortName(payload + 2, portStrLen);

			try {
				std::string localPortName = this->remote2localPort.at(make_pair(remoteAddress, remotePortName));

				// If a open/close sequence is currently pending for this port
				if (!this->remote_port_name.empty() && this->remote_port_name == remotePortName && this->remote_address == remoteAddress) {
#ifdef DEBUG_PRINTS
					printf("DEBUG: received close confirm for remote port: local %s : remote %s\n", localPortName.c_str(), remotePortName.c_str());
#endif
					// Signal to current open/close sequence (if there is one)
					{
						std::unique_lock<std::mutex> lock(this->remote_port_waitm);
						this->remote_port_status = true;
						this->remote_port_waitc.notify_all();
					}

				// If not, this just tells us the other end was closed and we should close too
				} else {
#ifdef DEBUG_PRINTS
					printf("DEBUG: received close notification for remote port: local %s : remote %s\n", localPortName.c_str(), remotePortName.c_str());
#endif
					// Attempt to get and close local port
					{
						// From the server we get the remote host name, so we need to map it to the local port
						std::unique_lock<std::shared_timed_mutex> lock(this->portsm);
						this->ports.erase(localPortName);
						this->remote2localPort.erase(make_pair(remoteAddress, remotePortName));

						std::string address;
						unsigned int port = 0;
						remoteAddress.tostr(address, &port);
						printf("[!] remote port closed, close local: local %s : remote %s @ %s %u1\n", localPortName.c_str(), remotePortName.c_str(), address.c_str(), port);
					}

				}

			} catch (std::out_of_range& e) {
				// This side does not know about this port link
				sendError(remoteAddress, "", "CLOSED: unknown link");
			}

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
	std::unique_lock<std::shared_timed_mutex> lock(this->portsm);
	for (auto entry = this->ports.cbegin(); entry != this->ports.cend(); entry++) {
		printf("[i] auto close port: %s\n", entry->first.c_str());
	}
	this->ports.clear();

}

bool SerialOverEthernet::SOESocketHandler::openRemotePort(const NetSocket::INetAddress& remoteAddress, const std::string& remotePortName, const SerialAccess::SerialPortConfiguration& config, const std::string& localPortName) {

	// Check for name conflicts with existing port claims
	if (this->ports.count(localPortName)) {
		printf("[!] failed to initialize connection, conflict with existing link: %s\n", remotePortName.c_str());
		return false;
	}

	// Register local port mapping
	this->remote2localPort[std::make_pair(remoteAddress, remotePortName)] = localPortName;

	// Attempt and wait to for port open
	std::cv_status status;
	{
		std::unique_lock<std::mutex> lock(this->remote_port_waitm);
		this->remote_port_status = false;
		this->remote_address = remoteAddress;
		this->remote_port_name = remotePortName;
		if (!sendOpenRequest(remoteAddress, localPortName, remotePortName, config)) return false;
		status = this->remote_port_waitc.wait_for(lock, std::chrono::milliseconds(INET_KEEP_ALIVE_INTERVAL));
		this->remote_port_name.clear();
	}

	// If no response, attempt close port
	if (status == std::cv_status::timeout) {
		printf("[!] failed to claim remote port %s, connection timed out\n", remotePortName.c_str());
		if (!closeRemotePort(remoteAddress, remotePortName))
			printf("[!] failed to close remote port %s, port might still be open on server\n", remotePortName.c_str());
		return false;
	}

	// If remote claim failed, abbort
	if (!this->remote_port_status) {
		this->remote2localPort.erase(std::make_pair(remoteAddress, remotePortName));
		return false;
	}

	// Attempt to open local port
	SerialAccess::SerialPort* port = SerialAccess::newSerialPortS(localPortName);
	bool error = false;
	if (!port->openPort()) {
		printf("[!] failed to claim local port %s, close remote port %s\n", localPortName.c_str(), remotePortName.c_str());
		error = true;
	}
	if (!error && !port->setConfig(config)) {
		printf("[!] failed to configure local port %s, close remote port %s\n", localPortName.c_str(), remotePortName.c_str());
		error = true;
	}
	if (!error && !port->setTimeouts(SERIAL_RX_TIMEOUT, SERIAL_TX_TIMEOUT)) {
		error = true;
	}
	if (error) {
		delete port;
		if (!closeRemotePort(remoteAddress, remotePortName))
			printf("[!] close remote port %s failed, port might still be open on server\n", remotePortName.c_str());
		return false;
	}

	// Register new port link handler
	SOEPortHandler* portHandler = new SOEPortHandler(port, [this] { this->notifySerialData(); }, [this, remoteAddress, localPortName](unsigned int txid) {
		if (!this->sendConfirm(remoteAddress, true, localPortName, txid))
			this->sendError(remoteAddress, localPortName, "failed to transmit TX_CONFIRM");
	});
	std::unique_lock<std::shared_timed_mutex> lock(this->portsm);
	this->ports[localPortName] = {
			std::unique_ptr<SOEPortHandler>(portHandler),
			remoteAddress,
			remotePortName,
			std::chrono::steady_clock::now() + std::chrono::milliseconds(INET_KEEP_ALIVE_TIMEOUT)
	};

	return true;

}

void SerialOverEthernet::SOESocketHandler::listAllPorts() {

	for (auto entry = this->ports.begin(); entry != this->ports.end(); entry++) {

		std::string address;
		unsigned int port = 0;
		entry->second.remote_address.tostr(address, &port);
		printf("[i] open link: local %s <-> remote %s @ %s %u\n", entry->first.c_str(), entry->second.remote_port.c_str(), address.c_str(), port);

	}

}

bool SerialOverEthernet::SOESocketHandler::closeAllPorts() {

	bool error = false;
	std::vector<std::string> localPorts;
	for (auto entry = this->ports.begin(); entry != this->ports.end(); entry++)
		localPorts.emplace_back(entry->first);
	for (auto port = localPorts.begin(); port != localPorts.end(); port++) {
		if (!closeLocalPort(*port))
			error = true;
	}
	return !error;

}

bool SerialOverEthernet::SOESocketHandler::closeLocalPort(const std::string& localPortName) {

	try {
		auto portClaim = &this->ports.at(localPortName);
		std::string remotePortName = portClaim->remote_port;
		NetSocket::INetAddress remoteAddress = portClaim->remote_address;

		return closeRemotePort(remoteAddress, remotePortName);
	} catch (std::out_of_range& e) {
		return false;
	}

}

bool SerialOverEthernet::SOESocketHandler::closeRemotePort(const NetSocket::INetAddress& remoteAddress, const std::string& remotePortName) {

	// Attempt and wait for remote port close
	std::cv_status status;
	std::string localPortName;
	try {
		localPortName = this->remote2localPort.at(make_pair(remoteAddress, remotePortName));
		std::unique_lock<std::mutex> lock(this->remote_port_waitm);
		this->remote_port_status = false;
		this->remote_address = remoteAddress;
		this->remote_port_name = remotePortName;
		if (!sendCloseRequest(remoteAddress, localPortName)) return false;
		status = this->remote_port_waitc.wait_for(lock, std::chrono::milliseconds(INET_KEEP_ALIVE_INTERVAL));
		this->remote_port_name.clear();
	} catch (std::out_of_range& e) {
		printf("[!] the remote port %s is unknown\n", remotePortName.c_str());
		return false;
	}

	// If error response received, the port is still closed, so just log it and continue
	if (!this->remote_port_status) {
		printf("[!] remote port %s close failed on server, port probably already closed\n", remotePortName.c_str());
	}

	// Attempt to find and close local port, if no port registered, skip this step
	{
		std::unique_lock<std::shared_timed_mutex> lock(this->portsm);
		try {
			this->ports.erase(localPortName);
		} catch (const std::out_of_range& e) {} // for whatever reason the entry was already deleted, no reason to panic
		try {
			this->remote2localPort.erase(make_pair(remoteAddress, remotePortName));
		} catch (const std::out_of_range& e) {} // for whatever reason the entry was already deleted, no reason to panic
	}

	return true;

}

// Sends an response frame
bool SerialOverEthernet::SOESocketHandler::sendFrame(const NetSocket::INetAddress& remoteAddress, char opc, const char* payload, unsigned int length) {

	if (!this->socket->isOpen()) return false;

	// Allocate frame buffer
	size_t frameLen = SOE_FRAME_HEADER_LEN + length;
	char buffer[frameLen] = {0};

	// Assemble frame
	buffer[0] = opc;
	memcpy(buffer + SOE_FRAME_HEADER_LEN, payload, length);

	// Transmit frame
	if (!this->socket->sendto(remoteAddress, buffer, frameLen)) {
		printf("[!] FRAME ERROR: failed to transmit frame\n");
		return false;
	}

	return true;
}

// Sends an error message response frame
void SerialOverEthernet::SOESocketHandler::sendError(const NetSocket::INetAddress& remoteAddress, const std::string& remotePortName, const std::string& msg) {

	if (!this->socket->isOpen()) return;

	// Get message and portName name length
	unsigned int portLen = remotePortName.length();
	unsigned int msgLen = msg.length();
	if (portLen > 0xFFFF) portLen = 0xFFFF;
	if (msgLen > 0xFFFF) msgLen = 0xFFFF;

	// Allocate payload buffer
	unsigned int payloadLen = (2 + portLen) + (msgLen > 0 ? 2 + msgLen : 0);
	char buffer[payloadLen] = {0};

	// Encode port name
	buffer[0] = (portLen >> 8) & 0xFF;
	buffer[1] = (portLen >> 0) & 0xFF;
	memcpy(buffer + 2, remotePortName.c_str(), portLen);

	// Encode message
	if (msgLen > 0) {
		buffer[2 + portLen] = (msgLen >> 8) & 0xFF;
		buffer[3 + portLen] = (msgLen >> 0) & 0xFF;
		memcpy(buffer + 4 + portLen, msg.c_str(), msgLen);
	}

	// Transmit payload in ERROR frame, ignore result, we don't care about an error-error ...
	sendFrame(remoteAddress, OPC_ERROR, buffer, payloadLen);

}

// Sends an port open/close reponse frame
bool SerialOverEthernet::SOESocketHandler::sendClaimStatus(const NetSocket::INetAddress& remoteAddress, bool claimed, const std::string& remotePortName) {

	if (!this->socket->isOpen()) return false;

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
bool SerialOverEthernet::SOESocketHandler::sendConfirm(const NetSocket::INetAddress& remoteAddress, bool transmission, const std::string& remotePortName, unsigned int txid) {

	if (!this->socket->isOpen()) return false;

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
bool SerialOverEthernet::SOESocketHandler::sendStream(const NetSocket::INetAddress& remoteAddress, const std::string& remotePortName, unsigned int rxid, const char* payload, unsigned long length) {

	if (!this->socket->isOpen()) return false;

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

// Sends an port open request frame
bool SerialOverEthernet::SOESocketHandler::sendOpenRequest(const NetSocket::INetAddress& remoteAddress, const std::string& portName, const std::string& remotePortName, const SerialAccess::SerialPortConfiguration& config) {

	if (!this->socket->isOpen()) return false;

	// Get port name length
	unsigned int portLen = portName.length();
	if (portLen > 0xFFFF) portLen = 0xFFFF;

	// Get remote port name length
	unsigned int remotePortLen = remotePortName.length();
	if (remotePortLen > 0xFFFF) remotePortLen = 0xFFFF;

	// Allocate payload buffer
	unsigned int payloadLen = 21 + portLen + remotePortLen;
	char buffer[payloadLen] = {0};

	// Encode port configuration
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

	// Encode remote port name
	buffer[19 + portLen] = (remotePortLen >> 8) & 0xFF;
	buffer[20 + portLen] = (remotePortLen >> 0) & 0xFF;
	memcpy(buffer + 21 + portLen, remotePortName.c_str(), remotePortLen);

	// Transmit payload in OPENED or ERROR frame
	return sendFrame(remoteAddress, OPC_OPEN, buffer, payloadLen);

}

// Sends an close request frame
bool SerialOverEthernet::SOESocketHandler::sendCloseRequest(const NetSocket::INetAddress& remoteAddress, const std::string& portName) {

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
