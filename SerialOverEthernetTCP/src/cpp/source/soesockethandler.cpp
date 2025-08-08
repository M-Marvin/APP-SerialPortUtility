/*
 * soesockethandler.cpp
 *
 * Handles an single Serial over Ethernet/IP connection/link.
 * This file implements the generic code required, such as opening sockets and ports and the RX/TX threads.
 *
 *  Created on: 04.02.2025
 *      Author: Marvin Koehler (M_Marvin)
 */

#include <string>
#include "soeconnection.hpp"
#include "dbgprintf.h"

SerialOverEthernet::SOESocketHandler::SOESocketHandler(NetSocket::Socket* socket, std::string& hostName, std::string& hostPort, std::function<void(SOESocketHandler*)> onDeath) {
	this->onDeath = onDeath;
	this->remoteHostName = hostName;
	this->remoteHostPort = hostPort;
	this->socket.reset(socket);
	this->socket->setTimeouts(0, 0);
	this->thread_rx = std::thread([this]() -> void {
		this->handleClientRX();
	});
	this->thread_tx = std::thread([this]() -> void {
		this->handleClientTX();
	});
}

SerialOverEthernet::SOESocketHandler::~SOESocketHandler() {
	shutdown();
	printf("[DBG] joining RX thread ...\n");
	this->thread_rx.join();
	printf("[DBG] joined\n");
	printf("[DBG] joining TX thread ...\n");
	this->thread_tx.join();
	printf("[DBG] joined\n");
}

bool SerialOverEthernet::SOESocketHandler::shutdown() {
	if (isAlive()) {
		printf("[i] link shutting down: %s <-> %s @ %s/%s\n", this->localPortName.c_str(), this->remotePortName.c_str(), this->remoteHostName.c_str(), this->remoteHostPort.c_str());
		closeLocalPort();
		this->socket->close();
		this->remoteReturn = false;
		this->cv_remoteReturn.notify_all();
		this->cv_openLocalPort.notify_all();
		this->onDeath(this);
		dbgprintf("[DBG] client handler terminated\n");
		return true;
	}
	return false;
}

bool SerialOverEthernet::SOESocketHandler::isAlive() {
	return this->socket->isOpen();
}

bool SerialOverEthernet::SOESocketHandler::openLocalPort(const std::string& localSerial) {
	closeLocalPort();
	std::unique_lock<std::mutex> lock(this->m_localPort);
	this->localPort.reset(SerialAccess::newSerialPortS(localSerial));
	this->localPortName = localSerial;
	dbgprintf("[DBG] opening local port: %s\n", this->localPortName.c_str());
	bool opened = this->localPort->openPort();
	if (opened) {
		if (!this->localPort->setTimeouts(-1, 0, -1)) {
			dbgprintf("[DBG] failed to configure timeouts when opening port\n");
			this->localPort->closePort();
			return false;
		}
		this->cv_openLocalPort.notify_all();
	}
	return opened;
}

bool SerialOverEthernet::SOESocketHandler::closeLocalPort() {
	if (this->localPort == 0 || !this->localPort->isOpen()) return true;
	std::unique_lock<std::mutex> lock(this->m_localPort);
	this->localPort->closePort();
	this->localPort.release();
	dbgprintf("[DBG] local port closed: %s\n", this->localPortName.c_str());
	return true;
}

bool SerialOverEthernet::SOESocketHandler::setLocalConfig(const SerialAccess::SerialPortConfiguration& localConfig) {
	if (this->localPort == 0 || !this->localPort->isOpen()) return false;
	std::lock_guard<std::mutex> lock(this->m_localPort);
	dbgprintf("[DBG] changing local port configuration: %s\n", this->localPortName.c_str());
	return this->localPort->setConfig(localConfig);
}

bool SerialOverEthernet::SOESocketHandler::openRemotePort(const std::string& remoteSerial) {
	std::unique_lock<std::mutex> lock(this->m_remoteReturn);
	this->remotePortName = remoteSerial;
	dbgprintf("[DBG] opening remote port: %s\n", this->remotePortName.c_str());
	if (!sendRemoteOpen(remoteSerial)) {
		printf("[!] failed to send open request for remote port: %s\n", this->remotePortName.c_str());
		return false;
	}
	if (this->cv_remoteReturn.wait_for(lock, std::chrono::milliseconds(SOE_TCP_HANDSHAKE_TIMEOUT)) == std::cv_status::timeout) {
		printf("[!] handshake timed out, failed to open port: %s\n", this->remotePortName.c_str());
		return false;
	}
	return this->remoteReturn;
}

bool SerialOverEthernet::SOESocketHandler::closeRemotePort() {
	if (this->remotePortName.empty()) return true;
	std::unique_lock<std::mutex> lock(this->m_remoteReturn);
	dbgprintf("[DBG] close remote port: %s\n", this->remotePortName.c_str());
	if (!sendRemoteClose()) {
		printf("[!] failed to send close request for remote port: %s\n", this->remotePortName.c_str());
		return false;
	}
	if (this->cv_remoteReturn.wait_for(lock, std::chrono::milliseconds(SOE_TCP_HANDSHAKE_TIMEOUT)) == std::cv_status::timeout) {
		printf("[!] handshake timed out, failed to close port: %s\n", this->remotePortName.c_str());
		return false;
	}
	return this->remoteReturn;
}

bool SerialOverEthernet::SOESocketHandler::setRemoteConfig(const SerialAccess::SerialPortConfiguration& remoteConfig) {
	std::unique_lock<std::mutex> lock(this->m_remoteReturn);
	dbgprintf("[DBG] changing remote port configuration: %s\n", this->remotePortName.c_str());
	if (!sendRemoteConfig(remoteConfig)) {
		printf("[!] failed to send configuration request for remote port: %s\n", this->remotePortName.c_str());
		return false;
	}
	if (this->cv_remoteReturn.wait_for(lock, std::chrono::milliseconds(SOE_TCP_HANDSHAKE_TIMEOUT)) == std::cv_status::timeout) {
		printf("[!] handshake timed out, failed to change configuration: %s\n", this->remotePortName.c_str());
		return false;
	}
	return this->remoteReturn;
}

void SerialOverEthernet::SOESocketHandler::handleClientTX() {

	char serialData[SOE_SERIAL_BUFFER_LEN] {0};

	while (isAlive()) {

		if (this->localPort == 0 || !this->localPort->isOpen()) {
			std::unique_lock<std::mutex> lock(this->m_localPort);
			this->cv_openLocalPort.wait(lock, [this]() {
				return (this->localPort != 0 && this->localPort->isOpen()) || !isAlive();
			});
			if (!isAlive()) break;
		}

		unsigned long read = this->localPort->readBytes(serialData, SOE_SERIAL_BUFFER_LEN);
		if (read == 0) continue; // when port closed / timed out

		dbgprintf("[DBG] stream data: |serial| -> [network] : >%.*s<\n", (int) read, serialData);

		if (!sendSerialData(serialData, read)) {
			printf("[!] frame error, unable to transmit serial data\n");
			break;
		}

	}

	dbgprintf("[DBG] client socket TX terminated, shutting down ...\n");
	shutdown();

}

void SerialOverEthernet::SOESocketHandler::transmitSerialData(const char* data, unsigned int len) {

	if (this->localPort == 0 || !this->localPort->isOpen()) return;

	dbgprintf("[DBG] stream data: [serial] <- |network| : >%.*s<\n", len, data);

	unsigned int written = this->localPort->writeBytes(data, len);

}

void SerialOverEthernet::SOESocketHandler::handleClientRX() {

	char packageFrame[SOE_TCP_FRAME_MAX_LEN] {0};
	unsigned int headerLen = 0;

	while (isAlive()) {

		if (!this->socket->receive(packageFrame, SOE_TCP_HEADER_LEN, &headerLen)) {
			if (socket->lastError() == 0)
				printf("[DBG] client socket returned EOF\n");
			else
				printf("[DBG] client socket header RX returned with error code: %d\n", this->socket->lastError());
			break;
		}

		// check for header
		if (headerLen < SOE_TCP_HEADER_LEN) {
			printf("[!] frame error, receivied incomplete frame header\n");
			break;
		}

		// check protocol identifier
		for (unsigned char i = 0; i < SOE_TCP_PROTO_IDENT_LEN; i++)
			if (packageFrame[i] != ((SOE_TCP_PROTO_IDENT >> i * 8) & 0xFF)) {
				printf("[!] frame error, received package with unknown identifier: %*.s\n", SOE_TCP_PROTO_IDENT_LEN, packageFrame);
				break;
			}

		// read package len
		unsigned int payloadLen = 0;
		for (unsigned char i = 0; i < SOE_TCP_FRAME_LEN_BYTES; i++)
			payloadLen |= (((unsigned char) packageFrame[SOE_TCP_PROTO_IDENT_LEN + i]) << (i * 8));
		if (payloadLen > SOE_TCP_FRAME_MAX_LEN - SOE_TCP_HEADER_LEN) {
			printf("[!] frame error, received package with oversize payload: %u\n", payloadLen);
			break;
		}

		// read remaining payload
		unsigned int received = 0;
		while (received < payloadLen && isAlive()) {
			if (!this->socket->receive(packageFrame + headerLen, payloadLen, &received)) {
				printf("[DBG] client socket payload RX returned with error code: %d\n", this->socket->lastError());
			}
		}

		// attempt to process the package
		if (!processPackage(packageFrame + headerLen, payloadLen)) {
			printf("[!] frame error, package response failed\n");
			break;
		}

	}

	dbgprintf("[DBG] client socket RX terminated, shutting down ...\n");
	shutdown();

}

bool SerialOverEthernet::SOESocketHandler::transmitPackage(const char* package, unsigned int packageLen) {

	// assemble frame header
	char frameHeader[SOE_TCP_HEADER_LEN] {0};
	for (unsigned char i = 0; i < SOE_TCP_PROTO_IDENT_LEN; i++)
		frameHeader[i] = (SOE_TCP_PROTO_IDENT >> i * 8) & 0xFF;
	for (unsigned char i = 0; i < SOE_TCP_FRAME_LEN_BYTES; i++)
		frameHeader[SOE_TCP_PROTO_IDENT_LEN + i] = (packageLen >> i * 8) & 0xFF;

	// acquire mutex for transmission
	std::unique_lock<std::mutex> lock(this->m_socketTX);

	if (!this->socket->send(frameHeader, SOE_TCP_HEADER_LEN)) {
		printf("[!] transmission error, unable to transmit frame header\n");
		return false;
	}
	if (!this->socket->send(package, packageLen)) {
		printf("[!] transmission error, unable to transmit package payload\n");
		return false;
	}

	return true;
}
