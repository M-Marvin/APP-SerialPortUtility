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

#define SOE_TCP_FRAME_MAX_LEN 512
#define SOE_TCP_FRAME_LEN_BYTES 3
#define SOE_TCP_PROTO_IDENT_LEN 4
#define SOE_TCP_PROTO_IDENT 0x534F4950U
#define SOE_TCP_HEADER_LEN SOE_TCP_PROTO_IDENT_LEN + SOE_TCP_FRAME_LEN_BYTES
#define SOE_TCP_HANDSHAKE_TIMEOUT 4

SerialOverEthernet::SOESocketHandler::SOESocketHandler(NetSocket::Socket* socket, std::function<void(SOESocketHandler*)> onDeath) {
	this->onDeath = onDeath;
	this->socket.reset(socket);
	this->thread_rx = std::thread([this]() -> void {
		this->handleClientRX();
	});
	this->thread_tx = std::thread([this]() -> void {
		this->handleClientTX();
	});
	dbgprintf("[DBG] new client handler created\n");
}

SerialOverEthernet::SOESocketHandler::~SOESocketHandler() {
	shutdown();
	this->thread_rx.join();
	this->thread_tx.join();
}

bool SerialOverEthernet::SOESocketHandler::shutdown() {
	if (isAlive()) {
		printf("[i] link shutting down: %s <-> %s\n", this->localPortName.c_str(), this->remotePortName.c_str());
		closeLocalPort();
		this->socket->close();

		{
			std::unique_lock<std::mutex> lock(this->m_remoteReturn);
			this->remoteReturn = false;
		}
		this->cv_remoteReturn.notify_all();
		dbgprintf("[DBG] client handler terminated\n");
		this->onDeath(this);
		dbgprintf("[DBG] client handler onDeath return\n");
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
	return this->localPort->openPort();
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
	std::unique_lock<std::mutex> lock(this->m_localPort);
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
	if (this->cv_remoteReturn.wait_for(lock, std::chrono::seconds(SOE_TCP_HANDSHAKE_TIMEOUT)) == std::cv_status::timeout) {
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
	if (this->cv_remoteReturn.wait_for(lock, std::chrono::seconds(SOE_TCP_HANDSHAKE_TIMEOUT)) == std::cv_status::timeout) {
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
	if (this->cv_remoteReturn.wait_for(lock, std::chrono::seconds(SOE_TCP_HANDSHAKE_TIMEOUT)) == std::cv_status::timeout) {
		printf("[!] handshake timed out, failed to change configuration: %s\n", this->remotePortName.c_str());
		return false;
	}
	return this->remoteReturn;
}

void SerialOverEthernet::SOESocketHandler::handleClientTX() {

	while (isAlive()) {



	}

}

void SerialOverEthernet::SOESocketHandler::handleClientRX() {

	char packageFrame[SOE_TCP_FRAME_MAX_LEN] {0};
	unsigned int headerLen = 0;

	while (isAlive()) {

		if (!this->socket->receive(packageFrame, SOE_TCP_HEADER_LEN, &headerLen)) {
			printf("[DBG] client socket header RX returned with error code: %d\n", this->socket->lastError());
			break;
		}

		// wait for all bytes of the protocol header
		if (headerLen < SOE_TCP_HEADER_LEN) continue;

		// check protocol identifier
		for (unsigned char i = 0; i < SOE_TCP_PROTO_IDENT_LEN; i++)
			if (packageFrame[i] != ((SOE_TCP_PROTO_IDENT >> i * 8) & 0xFF)) {
				printf("[!] frame error, received package with unknown identifier: %*.s\n", SOE_TCP_PROTO_IDENT_LEN, packageFrame);
				break;
			}

		// read package len
		unsigned int payloadLen = 0;
		for (unsigned char i = 0; i < SOE_TCP_FRAME_LEN_BYTES; i++)
			payloadLen |= (packageFrame[SOE_TCP_PROTO_IDENT_LEN + i] << i * 8);
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
