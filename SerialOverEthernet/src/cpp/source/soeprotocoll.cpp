/*
 * soeprotocoll.cpp
 *
 * Implements the SOE protocol OPCs
 *
 *  Created on: 01.07.2025
 *      Author: marvi
 */

#include <cstring>
#include <stdexcept>
#include "soeimpl.hpp"
#include "dbgprintf.h"

void SerialOverEthernet::SOESocketHandler::handleRequest(char opc, NetSocket::INetAddress& remoteAddress, std::string& remotePortName, const char* payload, unsigned int payloadLen) {

	switch (opc) {
	case OPC_OPEN: {

		// Check payload length
		if (payloadLen < 19) {
			sendError(remoteAddress, remotePortName, "received incomplete OPEN control frame");
			return;
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

		// Decode claim port name length
		unsigned short localPortStrLen =
				(payload[17] & 0xFF) << 8 |
				(payload[18] & 0xFF) << 0;

		// Check claim port name length
		if (localPortStrLen > payloadLen - 19) {
			sendError(remoteAddress, remotePortName, "received invalid OPEN payload");
			return;
		}

		// Decode port name strings
		std::string localPortName(payload + 19, localPortStrLen);

		// Check for name conflicts with existing port claims
		if (this->ports.count(localPortName)) {
			sendError(remoteAddress, localPortName, "port name conflict");
			return;
		}

		// Attempt to open port
		SerialAccess::SerialPort* serialPort = SerialAccess::newSerialPort(localPortName.c_str());
		if (!serialPort->openPort()) {
			sendError(remoteAddress, localPortName, "failed to claim port");
			delete serialPort;
			return;
		}
		if (!serialPort->setConfig(config) || !serialPort->setTimeouts(0, SERIAL_TX_TIMEOUT)) {
			sendError(remoteAddress, localPortName, "failed to configure port");
			delete serialPort;
			return;
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

		std::string address;
		unsigned int port = 0;
		remoteAddress.tostr(address, &port);

		// Confirm that the port has been opened, close port if this fails, to avoid unused open ports
		if (!sendClaimStatus(remoteAddress, true, localPortName)) {
			sendError(remoteAddress, localPortName, "failed to complete OPENED confirmation, close port");
			{
				std::unique_lock<std::shared_timed_mutex> lock(this->portsm);
				this->ports.erase(localPortName);
				this->remote2localPort.erase(make_pair(remoteAddress, remotePortName));
			}
			printf("[!] unable to complete handshake, ports not opened: local %s : remote %s @ %s %u\n", localPortName.c_str(), remotePortName.c_str(), address.c_str(), port);
			return;
		}

		printf("[i] opened port from remote: local %s : remote %s @ %s %u\n", localPortName.c_str(), remotePortName.c_str(), address.c_str(), port);

		return;
	}
	case OPC_CLOSE: {

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
				return;
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

		return;
	}
	case OPC_STREAM: {

		// Check payload length
		if (payloadLen < 4) {
			sendError(remoteAddress, "", "received incomplete STREAM control frame");
			return;
		}

		try {
			std::string localPortName = this->remote2localPort.at(make_pair(remoteAddress, remotePortName));

			// Decode transmission id
			unsigned int txid =
					(payload[0] & 0xFF) << 24 |
					(payload[1] & 0xFF) << 16 |
					(payload[2] & 0xFF) << 8 |
					(payload[3] & 0xFF) << 0;

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

				return;
			}

			// Update keep alive timeout
			portClaim->point_of_timeout = std::chrono::steady_clock::now() + std::chrono::milliseconds(INET_KEEP_ALIVE_TIMEOUT);

			// Put remaining payload on transmission stack
			const char* serialData = payload + 4;
			size_t serialLen = payloadLen - 4;
			if (serialLen > 0) {
				if (!portClaim->handler->send(txid, serialData, serialLen)) {
					dbgprintf("[DBG] could not process package, tx stack full: [tx %u] %s\n", txid, localPortName.c_str());
					sendError(remoteAddress, localPortName, "STREAM: could not process package, tx stack full");
					return;
				}
			} else {
				dbgprintf("[DBG] received keep alive: %s -> %s\n", remotePortName.c_str(), localPortName.c_str());
			}

			sendConfirm(remoteAddress, false, localPortName, txid);

		} catch (std::out_of_range& e) {
			// This side does not know about this port link
			sendError(remoteAddress, "", "STREAM: unknown link");
		}

		return;
	}
	case OPC_RX_CONFIRM: {

		// Check payload length
		if (payloadLen < 4) {
			sendError(remoteAddress, "", "received incomplete RX_CONFIRM control frame");
			return;
		}

		try {
			std::string localPortName = this->remote2localPort.at(make_pair(remoteAddress, remotePortName));

			// Decode transmission id
			unsigned int rxid =
					(payload[0] & 0xFF) << 24 |
					(payload[1] & 0xFF) << 16 |
					(payload[2] & 0xFF) << 8 |
					(payload[3] & 0xFF) << 0;

			// Attempt to get port
			std::shared_lock<std::shared_timed_mutex> lock(this->portsm);
			port_claim* portClaim = &this->ports.at(localPortName);

			// Confirm reception
			portClaim->handler->confirmReception(rxid);

		} catch (std::out_of_range& e) {
			// This side does not know about this port link
			sendError(remoteAddress, "", "RX_CONFIRM: unknown link");
		}

		return;
	}
	case OPC_TX_CONFIRM: {

		// Check payload length
		if (payloadLen < 4) {
			sendError(remoteAddress, "", "received incomplete TX_CONFIRM control frame");
			return;
		}

		// Decode transmission id
		unsigned int rxid =
				(payload[0] & 0xFF) << 24 |
				(payload[1] & 0xFF) << 16 |
				(payload[2] & 0xFF) << 8 |
				(payload[3] & 0xFF) << 0;

		try {
			std::string localPortName = this->remote2localPort.at(make_pair(remoteAddress, remotePortName));

			// Attempt to get port
			std::shared_lock<std::shared_timed_mutex> lock(this->portsm);
			port_claim* portClaim = &this->ports.at(localPortName);

			// Confirm reception
			portClaim->handler->confirmTransmission(rxid);

		} catch (std::out_of_range& e) {
			dbgprintf("[DBG] received tx confirm for closed port: remote %s [rx %u]\n", remotePortName.c_str(), rxid);
			// This side does not know about this port link
			sendError(remoteAddress, "", "TX_CONFIRM: unknown link");
		}

		return;
	}
	case OPC_ERROR: {

		// Check payload length
		if (payloadLen < 2) {
			sendError(remoteAddress, "", "received incomplete ERROR control frame");
			return;
		}

		// Decode message length
		unsigned short msgStrLen =
				(payload[0] & 0xFF) << 8 |
				(payload[1] & 0xFF) << 0;

		// Check message length
		if (msgStrLen > payloadLen - 2) {
			sendError(remoteAddress, "", "received incomplete ERROR control frame");
			return;
		}

		// Decode message string
		std::string message(payload + 2, msgStrLen);

		if (!remotePortName.empty()) {

			try {
				std::string localPortName = this->remote2localPort.at(make_pair(remoteAddress, remotePortName));

				std::string address;
				unsigned int port = 0;
				remoteAddress.tostr(address, &port);
				printf("[!] received error frame: local %s : remote %s @ %s %u : %s\n", localPortName.c_str(), remotePortName.c_str(), address.c_str(), port, message.c_str());

				// Check if this matches an ongoing port claim
				if (!this->remote_port_name.empty() && this->remote_port_name == remotePortName && this->remote_address == remoteAddress) {
					dbgprintf("[DBG] received error frame for remote port open/close sequence: remote %s : local %s\n", remotePortName.c_str(), localPortName.c_str());

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

		return;
	}
	case OPC_OPENED: {
		try {
			std::string localPortName = this->remote2localPort.at(make_pair(remoteAddress, remotePortName));
			dbgprintf("[DBG] received open confirm for remote port: local %s : remote %s\n", localPortName.c_str(), remotePortName.c_str());
		} catch (std::out_of_range& e) {
			dbgprintf("[DBG] received open confirm for remote port: local N/A : remote %s\n", remotePortName.c_str());
		}

		// Signal to current open/close sequence (if there is one)
		if (!this->remote_port_name.empty()) {
			std::unique_lock<std::mutex> lock(this->remote_port_waitm);
			this->remote_port_status = this->remote_port_name == remotePortName && this->remote_address == remoteAddress;
			this->remote_port_waitc.notify_all();
		}

		return;
	}
	case OPC_CLOSED: {
		try {
			std::string localPortName = this->remote2localPort.at(make_pair(remoteAddress, remotePortName));

			// If a open/close sequence is currently pending for this port
			if (!this->remote_port_name.empty() && this->remote_port_name == remotePortName && this->remote_address == remoteAddress) {
				dbgprintf("[DBG] received close confirm for remote port: local %s : remote %s\n", localPortName.c_str(), remotePortName.c_str());

				// Signal to current open/close sequence (if there is one)
				{
					std::unique_lock<std::mutex> lock(this->remote_port_waitm);
					this->remote_port_status = true;
					this->remote_port_waitc.notify_all();
				}

			// If not, this just tells us the other end was closed and we should close too
			} else {
				dbgprintf("[DBG] received close notification for remote port: local %s : remote %s\n", localPortName.c_str(), remotePortName.c_str());

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

		return;
	}
	default:
		dbgprintf("[DBG] received invalid control frame: %x\n", opc);
		sendError(remoteAddress, "", "received invalid control frame");
	}

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
	if (!sendFrame(remoteAddress, OPC_ERROR, buffer, payloadLen)) {
		printf("[!] FRAME ERROR: failed to transmit ERROR frame\n");
	}

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
	if (!sendFrame(remoteAddress, claimed ? OPC_OPENED : OPC_CLOSED, buffer, payloadLen)) {
		printf("[!] FRAME ERROR: failed to transmit OPENED/CLOSED frame\n");
		return false;
	}
	return true;

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
	if (!sendFrame(remoteAddress, transmission ? OPC_TX_CONFIRM : OPC_RX_CONFIRM, buffer, payloadLen)) {
		printf("[!] FRAME ERROR: failed to transmit TX/RX_CONFIRM frame\n");
		return false;
	}
	return true;
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
	if (!sendFrame(remoteAddress, OPC_STREAM, buffer, payloadLen)) {
		printf("[!] FRAME ERROR: failed to transmit STREAM frame\n");
		return false;
	}
	return true;

}

// Sends an port open request frame
bool SerialOverEthernet::SOESocketHandler::sendOpen(const NetSocket::INetAddress& remoteAddress, const std::string& portName, const std::string& remotePortName, const SerialAccess::SerialPortConfiguration& config) {

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

	// Encode port name
	buffer[0] = (portLen >> 8) & 0xFF;
	buffer[1] = (portLen >> 0) & 0xFF;
	memcpy(buffer + 2, portName.c_str(), portLen);

	// Encode port configuration
	buffer[2 + portLen] = (config.baudRate >> 24) & 0xFF;
	buffer[3 + portLen] = (config.baudRate >> 16) & 0xFF;
	buffer[4 + portLen] = (config.baudRate >> 8) & 0xFF;
	buffer[5 + portLen] = (config.baudRate >> 0) & 0xFF;
	buffer[6 + portLen] = config.dataBits & 0xFF;
	buffer[7 + portLen] = (config.stopBits >> 24) & 0xFF;
	buffer[8 + portLen] = (config.stopBits >> 16) & 0xFF;
	buffer[9 + portLen] = (config.stopBits >> 8) & 0xFF;
	buffer[10 + portLen] = (config.stopBits >> 0) & 0xFF;
	buffer[11 + portLen] = (config.parity >> 24) & 0xFF;
	buffer[12 + portLen] = (config.parity >> 16) & 0xFF;
	buffer[13 + portLen] = (config.parity >> 8) & 0xFF;
	buffer[14 + portLen] = (config.parity >> 0) & 0xFF;
	buffer[15 + portLen] = (config.flowControl >> 24) & 0xFF;
	buffer[16 + portLen] = (config.flowControl >> 16) & 0xFF;
	buffer[17 + portLen] = (config.flowControl >> 8) & 0xFF;
	buffer[18 + portLen] = (config.flowControl >> 0) & 0xFF;

	// Encode remote port name
	buffer[19 + portLen] = (remotePortLen >> 8) & 0xFF;
	buffer[20 + portLen] = (remotePortLen >> 0) & 0xFF;
	memcpy(buffer + 21 + portLen, remotePortName.c_str(), remotePortLen);

	// Transmit payload in OPENED or ERROR frame
	if (!sendFrame(remoteAddress, OPC_OPEN, buffer, payloadLen)) {
		printf("[!] FRAME ERROR: failed to transmit OPEN frame\n");
		return false;
	}
	return true;

}

// Sends an close request frame
bool SerialOverEthernet::SOESocketHandler::sendClose(const NetSocket::INetAddress& remoteAddress, const std::string& portName) {

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
	if (!sendFrame(remoteAddress, OPC_CLOSE, buffer, payloadLen)) {
		printf("[!] FRAME ERROR: failed to transmit CLOSE frame\n");
		return false;
	}
	return true;

}
