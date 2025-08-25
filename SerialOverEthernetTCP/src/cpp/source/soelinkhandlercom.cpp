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

SerialOverEthernet::SOELinkHandlerCOM::SOELinkHandlerCOM(NetSocket::Socket* socket, std::string& hostName, std::string& hostPort, std::function<void(SOELinkHandler*)> onDeath) {
	this->onDeath = onDeath;
	this->remoteHostName = hostName;
	this->remoteHostPort = hostPort;
	this->socket.reset(socket);
	this->socket->setTimeouts(0, 0);
	this->socket->setNagle(false);
	this->thread_rx = std::thread([this]() -> void {
		this->handleClientRX();
	});
	this->thread_tx = std::thread([this]() -> void {
		this->handleClientTX();
	});
}

SerialOverEthernet::SOELinkHandlerCOM::~SOELinkHandlerCOM() {
	shutdown();
	printf("[DBG] joining RX thread ...\n");
	this->thread_rx.join();
	printf("[DBG] joined\n");
	printf("[DBG] joining TX thread ...\n");
	this->thread_tx.join();
	printf("[DBG] joined\n");
}

bool SerialOverEthernet::SOELinkHandlerCOM::shutdown() {
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

bool SerialOverEthernet::SOELinkHandlerCOM::openLocalPort(const std::string& localSerial) {
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

bool SerialOverEthernet::SOELinkHandlerCOM::closeLocalPort() {
	if (this->localPort == 0 || !this->localPort->isOpen()) return true;
	std::unique_lock<std::mutex> lock(this->m_localPort);
	this->localPort->closePort();
	this->localPort.release();
	dbgprintf("[DBG] local port closed: %s\n", this->localPortName.c_str());
	return true;
}

bool SerialOverEthernet::SOELinkHandlerCOM::setLocalConfig(const SerialAccess::SerialPortConfiguration& localConfig) {
	if (this->localPort == 0 || !this->localPort->isOpen()) return false;
	std::lock_guard<std::mutex> lock(this->m_localPort);
	dbgprintf("[DBG] changing local port configuration: %s (baud %lu)\n", this->localPortName.c_str(), localConfig.baudRate);
	return this->localPort->setConfig(localConfig);
}

void SerialOverEthernet::SOELinkHandlerCOM::handleClientTX() {

	char serialData[SOE_SERIAL_BUFFER_LEN] {0};

	while (isAlive()) {

		// check if port open, wait if not
		if (this->localPort == 0 || !this->localPort->isOpen()) {
			std::unique_lock<std::mutex> lock(this->m_localPort);
			this->cv_openLocalPort.wait(lock, [this]() {
				return (this->localPort != 0 && this->localPort->isOpen()) || !isAlive();
			});
			if (!isAlive()) break;
		}

		bool nothingToDo = false;

		// try to write data from ring buffer to serial
		{
			// get how many bytes are available, including the ones which's transmission is already pending
			unsigned long availableBytes = (this->writePtr >= this->pendingPtr ? this->writePtr - this->pendingPtr : SOE_TCP_STREAM_BUFFER_LEN - (this->pendingPtr - this->writePtr));

			// if data available (or pending)
			if (availableBytes > 0) {
				// calculate how much can be transfered in one go (no wrapping at the buffer end)
				unsigned long ptrToEnd = std::min(SOE_TCP_STREAM_BUFFER_LEN - this->pendingPtr - 1, availableBytes);	// bytes that can be copied before hitting the end of the buffer

				// start transfer or (if already pending) check status of last transfer
				long long int written = this->localPort->writeBytes(this->receptionBuffer + this->readPtr, ptrToEnd, false);
				if (written < -1) {
					continue; // when port closed / timed out
				}

				if (written < 0) {
					dbgprintf("[DBG] pending data: [serial] <- |network| : >%.*s<\n", ptrToEnd, this->receptionBuffer + this->readPtr);

					// if transfer (still) pending, update read pointer to where the next operation should start
					this->readPtr = (this->pendingPtr + ptrToEnd) % SOE_TCP_STREAM_BUFFER_LEN;

					// the transmission buffer is full, send flow control signal
					sendFlowControl(this->remoteFlowEnable = false);

					nothingToDo = true; // we need to wait for the serial buffer to be ready to receive more data
				} else {
					dbgprintf("[DBG] stream data: [serial] <- |network| : >%.*s<\n", written, this->receptionBuffer + this->pendingPtr);
					if (this->pendingPtr != this->readPtr) {
						// if pending transfer completed, increment pending pointer
						this->pendingPtr = (this->pendingPtr + written) % SOE_TCP_STREAM_BUFFER_LEN;
					} else {
						// if transfer completed immediately, increment both pending and read pointer
						this->pendingPtr = this->readPtr = (this->readPtr + ptrToEnd) % SOE_TCP_STREAM_BUFFER_LEN;;
					}
				}

			} else {
				// if flow was disabled, reactivate now
				if (!this->remoteFlowEnable) {
					sendFlowControl(this->remoteFlowEnable = true);
				}

				nothingToDo = true; // we need to wait for more data in the ring buffer
			}

		}

		// try to read data from serial
		{

			// start new read operation or check status of pending operation
			long long int read = this->localPort->readBytes(serialData, SOE_SERIAL_BUFFER_LEN, false);
			if (read < -1) {
				continue; // when port closed / timed out
			}

			if (read > 0) {

				dbgprintf("[DBG] stream data: |serial| -> [network] : >%.*s<\n", (unsigned int) read, serialData);

				// send data
				if (!sendSerialData(serialData, (unsigned int) read)) {
					printf("[!] frame error, unable to transmit serial data\n");
					break;
				}

			} else {
				nothingToDo = false; // we do have work, the input is being read, we can pause after it completed
			}

		}

		// check for COM state event and (if no thing else to do) wait for more data
		bool comStateChanged = true;
		bool dataReceived = nothingToDo;
		bool dataTransmitted = nothingToDo;
		if (nothingToDo) dbgprintf("[dbg] >>> ENTER WAIT <<<\n");
		if (!this->localPort->waitForEvents(comStateChanged, dataReceived, dataTransmitted, nothingToDo)) {
			if (nothingToDo) dbgprintf("[dbg] >>> ERROR WAIT <<<\n");
			continue; // when port closed / timed out
		}
		if (nothingToDo) dbgprintf("[dbg] >>> LEAVE WAIT <<<\n");

		if (comStateChanged) {
			// notify remote port about changed COM state
			bool dsrState, ctsState;
			if (!this->localPort->getPortState(dsrState, ctsState)) {
				continue; // when port closed
			}

			dbgprintf("[DBG] stream port state: |serial| -> [network]\n");

			if (!sendPortState(dsrState, ctsState)) {
				printf("[!] frame error, unable to transmit serial port state\n");
				break;
			}
		}

	}

	dbgprintf("[DBG] client socket TX terminated, shutting down ...\n");
	shutdown();

}

void SerialOverEthernet::SOELinkHandlerCOM::transmitSerialData(const char* data, unsigned int len) {

	if (this->localPort == 0 || !this->localPort->isOpen()) return;

	unsigned long freeBytes = (this->writePtr >= this->readPtr ? SOE_TCP_STREAM_BUFFER_LEN - (this->writePtr - this->readPtr) : this->readPtr - this->writePtr) - 1;
	if (freeBytes < len) {
		printf("[!] reception buffer overflow, flow control failed!\n");
		return;
	}

	unsigned long ptrToEnd = std::min(SOE_TCP_STREAM_BUFFER_LEN - this->writePtr - 1, (unsigned long) len);	// bytes that can be copied before hitting the end of the buffer
	unsigned long startToPtr = len - ptrToEnd;												// remaining bytes that then have to be copied to the start of the buffer

	// TODO test tcp reception serial transmission buffer

	memcpy(this->receptionBuffer + this->writePtr, data, ptrToEnd);
	if (startToPtr > 0)
		memcpy(this->receptionBuffer, data + this->writePtr, startToPtr);

	this->writePtr = (this->writePtr + len) % SOE_TCP_STREAM_BUFFER_LEN;

	// kick the TX thread out of waiting state if buffer was empty (since it will be waiting for new data)
	//if (freeBytes == SOE_TCP_STREAM_BUFFER_LEN)
		this->localPort->abortWait();

//	// detect changes in flow control, notify remote port
//	bool state;
//	if (this->localPort->getFlowControl(state) && state != readyToSend) {
//		sendFlowControl(false);
//		readyToSend = state;
//	}
//
//	// write data (blocks until all data send)
//	this->localPort->writeBytes(data, len);

}

void SerialOverEthernet::SOELinkHandlerCOM::updateFlowControl(bool readyToReceive) {

	if (this->localPort == 0 || !this->localPort->isOpen()) return;

	// TODO implement tcp transmission flow control

}
