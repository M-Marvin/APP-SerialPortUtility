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
#include "dbgprintf.h"

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
			for (auto entry = this->ports.begin(); entry != this->ports.end(); ) {

				NetSocket::INetAddress remoteAddress = entry->second.remote_address;
				std::string remotePortName = entry->second.remote_port;
				std::string localPortName = entry->first;

				// Check if timeout has expired
				if (entry->second.point_of_timeout < now) {

					std::string address;
					unsigned int port = 0;
					remoteAddress.tostr(address, &port);
					printf("[!] connection timed out, close port: local %s : remote %s @ %s %u\n", localPortName.c_str(), remotePortName.c_str(), address.c_str(), port);

					// Close port
					lock.unlock();
					{
						std::unique_lock<std::shared_timed_mutex> lock(this->portsm);
						this->ports.erase(entry++);
						this->remote2localPort.erase(std::make_pair(remoteAddress, remotePortName));
					}
					if (!sendClaimStatus(remoteAddress, false, localPortName)) {
						// If the error report fails too ... don't care at this point ...
						sendError(remoteAddress, localPortName, "failed to transmit CLOSE notification");
					}
					//immediateWork = true;
					//break;
					continue;
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
						dbgprintf("[DBG] send keep alive: %s -> %s\n", localPortName.c_str(), remotePortName.c_str());
					} else {
						entry++;
						continue;
					}
				}

				// Send payload stream frame
				if (!sendStream(remoteAddress, localPortName, rxid, payload, length)) {
					sendError(remoteAddress, localPortName, "failed to transmit STREAM frame, close port");

					std::string address;
					unsigned int port = 0;
					remoteAddress.tostr(address, &port);
					printf("[!] transmission error, close port: remote %s @ %s %u\n", remotePortName.c_str(), address.c_str(), port);

					// Close port
					lock.unlock();
					{
						std::unique_lock<std::shared_timed_mutex> lock(this->portsm);
						this->ports.erase(entry++);
						this->remote2localPort.erase(make_pair(remoteAddress, remotePortName));
					}
					if (!sendClaimStatus(remoteAddress, false, localPortName)) {
						// If the error report fails too ... don't care at this point ...
						sendError(remoteAddress, localPortName, "failed to transmit CLOSE notification");
					}
					continue;
				}

				// Update last send timeout
				entry->second.last_send = now + std::chrono::microseconds(INET_KEEP_ALIVE_INTERVAL);

				// Data was send, so skip waiting, more data is likely to be available
				immediateWork = true;
				entry++;
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
			printf("[!] error while receiving package\n");
			continue;
		}

		if (pckgLen < SOE_FRAME_HEADER_LEN) {
			printf("[!] received incomplete control frame header, discard package\n");
			continue;
		}

		opc = pckgBuffer[0];
		payloadLen = pckgLen - SOE_FRAME_HEADER_LEN;

		// Check payload length
		if (payloadLen < 2) {
			sendError(remoteAddress, "N/A", "received incomplete control frame");
			continue;
		}

		// Decode port name length
		unsigned short portStrLen =
				(payload[0] & 0xFF) << 8 |
				(payload[1] & 0xFF) << 0;

		// Check port name length
		if (portStrLen > payloadLen - 2) {
			sendError(remoteAddress, "N/A", "received invalid payload");
			continue;
		}

		// Decode port name string
		std::string remotePortName(payload + 2, portStrLen);

		handleRequest(opc, remoteAddress, remotePortName, payload + 2 + portStrLen, payloadLen - 2 - portStrLen);

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
		if (!sendOpen(remoteAddress, localPortName, remotePortName, config)) return false;
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
	if (!error && !port->setTimeouts(0, SERIAL_TX_TIMEOUT)) {
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
		if (!sendClose(remoteAddress, localPortName)) return false;
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
