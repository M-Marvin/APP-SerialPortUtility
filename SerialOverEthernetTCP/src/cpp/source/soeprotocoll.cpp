/*
 * soeprotocoll.cpp
 *
 * Implements the actual Serial over Ethernet/IP protocoll.
 * Implements all the required control messages.
 *
 *  Created on: 01.07.2025
 *      Author: Marvin Koehler (M_Marvin)
 */

#include <string.h>
#include "soeconnection.hpp"
#include "dbgprintf.h"

#define SOE_TCP_OPC_ERROR 0x0
#define SOE_TCP_OPC_CONFIRM 0x1
#define SOE_TCP_OPC_OPEN_PORT 0x10
#define SOE_TCP_OPC_CLOSE_PORT 0x20
#define SOE_TCP_OPC_CONFIGURE_PORT 0x30
#define SOE_TCP_OPC_STREAM_SERIAL 0x40
#define SOE_TCP_OPC_FLOW_CONTROL 0x50

bool SerialOverEthernet::SOELinkHandler::processPackage(const char* package, unsigned int packageLen) {

	if (packageLen == 0)
		return sendError("no package payload");

	switch (package[0]) {
	case SOE_TCP_OPC_STREAM_SERIAL:		return processSerialData(package, packageLen);
	case SOE_TCP_OPC_ERROR: 			return processError(package, packageLen);
	case SOE_TCP_OPC_CONFIRM:			return processConfirm(package, packageLen);
	case SOE_TCP_OPC_OPEN_PORT: 		return processRemoteOpen(package, packageLen);
	case SOE_TCP_OPC_CLOSE_PORT: 		return processRemoteClose(package, packageLen);
	case SOE_TCP_OPC_CONFIGURE_PORT: 	return processRemoteConfig(package, packageLen);
	case SOE_TCP_OPC_FLOW_CONTROL:		return processFlowControl(package, packageLen);
	default: 							return sendError("undefined package code: " + std::to_string(package[0]));
	}

}

bool SerialOverEthernet::SOELinkHandler::sendError(const std::string& message) {
	char package[message.length() + 1] {0};
	package[0] = SOE_TCP_OPC_ERROR;
	memcpy(package + 1, message.c_str(), (size_t) message.length());

	return transmitPackage(package, (unsigned int) message.length() + 1);
}

bool SerialOverEthernet::SOELinkHandler::processError(const char* package, unsigned int packageLen) {
	if (packageLen < 1) return false;
	std::string message(package + 1, packageLen - 1);

	printf("[!] remote error frame: %s\n", message.c_str());
	return true;
}

bool SerialOverEthernet::SOELinkHandler::sendConfirm(bool status) {
	char package[] = { SOE_TCP_OPC_CONFIRM, status ? (char) 0x1 : (char) 0x0 };

	return transmitPackage(package, 2);
}

bool SerialOverEthernet::SOELinkHandler::processConfirm(const char* package, unsigned int packageLen) {
	if (packageLen < 2) return false;
	std::unique_lock<std::mutex> lock(this->m_remoteReturn);
	this->remoteReturn = package[1] == 0x1;
	lock.unlock();
	this->cv_remoteReturn.notify_all();
	return true;
}

bool SerialOverEthernet::SOELinkHandler::sendRemoteOpen(const std::string& remoteSerial) {
	char package[remoteSerial.length() + 1] {0};
	package[0] = SOE_TCP_OPC_OPEN_PORT;
	memcpy(package + 1, remoteSerial.c_str(), (size_t) remoteSerial.length());

	return transmitPackage(package, (unsigned int) remoteSerial.length() + 1);
}

bool SerialOverEthernet::SOELinkHandler::processRemoteOpen(const char* package, unsigned int packageLen) {
	if (packageLen < 2) return false;
	std::string portName(package + 1, packageLen - 1);

	printf("[i] open port from remote: %s\n", portName.c_str());
	bool opened = openLocalPort(portName);
	if (!opened)
		printf("[!] unable to open port from remote: %s\n", portName.c_str());
	if (!sendConfirm(opened)) {
		dbgprintf("[DBG] unable to send confirm response\n");
		return false;
	}
	return true;
}

bool SerialOverEthernet::SOELinkHandler::sendRemoteClose() {
	char package = SOE_TCP_OPC_CLOSE_PORT;

	return transmitPackage(&package, 1);
}

bool SerialOverEthernet::SOELinkHandler::processRemoteClose(const char* package, unsigned int packageLen) {
	printf("[i] close port from remote: %s\n", this->localPortName.c_str());
	bool closed = closeLocalPort();
	if (!closed)
		printf("[!] unable to close port from remote: %s\n", this->localPortName.c_str());
	if (!sendConfirm(closed)) {
		dbgprintf("[DBG] unable to send confirm response\n");
		return false;
	}
	return true;
}

bool SerialOverEthernet::SOELinkHandler::sendRemoteConfig(const SerialAccess::SerialPortConfiguration& remoteSerial) {
	char package[18] {0};
	package[0] = SOE_TCP_OPC_CONFIGURE_PORT;
	package[1] = (remoteSerial.baudRate >> 24) & 0xFF;
	package[2] = (remoteSerial.baudRate >> 16) & 0xFF;
	package[3] = (remoteSerial.baudRate >> 8) & 0xFF;
	package[4] = (remoteSerial.baudRate >> 0) & 0xFF;
	package[5] = remoteSerial.dataBits & 0xFF;
	package[6] = (remoteSerial.stopBits >> 24) & 0xFF;
	package[7] = (remoteSerial.stopBits >> 16) & 0xFF;
	package[8] = (remoteSerial.stopBits >> 8) & 0xFF;
	package[9] = (remoteSerial.stopBits >> 0) & 0xFF;
	package[10] = (remoteSerial.parity >> 24) & 0xFF;
	package[11] = (remoteSerial.parity >> 16) & 0xFF;
	package[12] = (remoteSerial.parity >> 8) & 0xFF;
	package[13] = (remoteSerial.parity >> 0) & 0xFF;
	package[14] = (remoteSerial.flowControl >> 24) & 0xFF;
	package[15] = (remoteSerial.flowControl >> 16) & 0xFF;
	package[16] = (remoteSerial.flowControl >> 8) & 0xFF;
	package[17] = (remoteSerial.flowControl >> 0) & 0xFF;

	return transmitPackage(package, 18);
}

bool SerialOverEthernet::SOELinkHandler::processRemoteConfig(const char* package, unsigned int packageLen) {
	if (packageLen < 18) return false;
	SerialAccess::SerialPortConfiguration config = {
		(unsigned long) ( // baud rate
				(package[1] & 0xFF) << 24 |
				(package[2] & 0xFF) << 16 |
				(package[3] & 0xFF) << 8 |
				(package[4] & 0xFF) << 0),
		(unsigned char) ( // data bits
				(package[5] & 0xFF)),
		(SerialAccess::SerialPortStopBits) (
				(package[6] & 0xFF) << 24 |
				(package[7] & 0xFF) << 16 |
				(package[8] & 0xFF) << 8 |
				(package[9] & 0xFF) << 0),
		(SerialAccess::SerialPortParity) (
				(package[10] & 0xFF) << 24 |
				(package[11] & 0xFF) << 16 |
				(package[12] & 0xFF) << 8 |
				(package[13] & 0xFF) << 0),
		(SerialAccess::SerialPortFlowControl) (
				(package[14] & 0xFF) << 24 |
				(package[15] & 0xFF) << 16 |
				(package[16] & 0xFF) << 8 |
				(package[17] & 0xFF) << 0)
	};

	printf("[i] change port configuration from remote: %s (baud %lu)\n", this->localPortName.c_str(), config.baudRate);
	bool changed = setLocalConfig(config);
	if (!changed)
		printf("[!] unable to change configuration from remote: %s\n", this->localPortName.c_str());
	if (!sendConfirm(changed)) {
		dbgprintf("[DBG] unable to send config confirm\n");
		return false;
	}
	return true;
}

bool SerialOverEthernet::SOELinkHandler::sendSerialData(const char* data, unsigned int len) {
	char package[len + 1] {0};
	package[0] = SOE_TCP_OPC_STREAM_SERIAL;
	memcpy(package + 1, data, len);

	return transmitPackage(package, len + 1);
}

bool SerialOverEthernet::SOELinkHandler::processSerialData(const char* package, unsigned int packageLen) {
	if (packageLen < 1) return false;

	transmitSerialData(package + 1, packageLen - 1);

	return true;
}

bool SerialOverEthernet::SOELinkHandler::sendFlowControl(bool status) {
	char package[] = { SOE_TCP_OPC_FLOW_CONTROL, status ? (char) 0x1 : (char) 0x0 };

	return transmitPackage(package, 2);
}

bool SerialOverEthernet::SOELinkHandler::processFlowControl(const char* package, unsigned int packageLen) {
	if (packageLen < 2) return false;

	updateFlowControl(package[1] == 0x1);

	return true;
}
