/*
 * soelinkhandler.cpp
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

bool SerialOverEthernet::SOELinkHandler::isAlive() {
	return this->socket->isOpen();
}

bool SerialOverEthernet::SOELinkHandler::openRemotePort(const std::string& remoteSerial) {
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

bool SerialOverEthernet::SOELinkHandler::closeRemotePort() {
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

bool SerialOverEthernet::SOELinkHandler::setRemoteConfig(const SerialAccess::SerialPortConfiguration& remoteConfig) {
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

void SerialOverEthernet::SOELinkHandler::handleClientRX() {

	char packageFrame[SOE_TCP_FRAME_MAX_LEN] {0};
	unsigned int headerLen = 0;

	while (isAlive()) {

		if (!this->socket->receive(packageFrame, SOE_TCP_HEADER_LEN, &headerLen)) {
			if (socket->lastError() == 0)
				printf("[DBG] client socket returned EOF\n");
			else
				printf("[DBG] client socket RX returned with error code: %d\n", this->socket->lastError());
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

bool SerialOverEthernet::SOELinkHandler::transmitPackage(const char* package, unsigned int packageLen) {

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
