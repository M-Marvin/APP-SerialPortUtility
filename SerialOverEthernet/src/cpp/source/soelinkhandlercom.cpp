/*
 * soelinkhandlercom.cpp
 *
 * Handles an single Serial over Ethernet/IP connection/link.
 * This file implements the code specific to an serial port, in contrast to an virtual serial port.
 *
 *  Created on: 04.02.2025
 *      Author: Marvin Koehler (M_Marvin)
 */

#include <string>
#include "soeconnection.hpp"
#include "dbgprintf.h"

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
	dbgprintf("[DBG] local port closed: %s\n", this->localPortName.c_str());
	return true;
}

bool SerialOverEthernet::SOELinkHandlerCOM::setLocalConfig(const SerialAccess::SerialPortConfiguration& localConfig) {
	if (this->localPort == 0 || !this->localPort->isOpen()) return false;
	std::lock_guard<std::mutex> lock(this->m_localPort);
	dbgprintf("[DBG] changing local port configuration: %s (baud %lu)\n", this->localPortName.c_str(), localConfig.baudRate);
	return this->localPort->setConfig(localConfig);
}

void SerialOverEthernet::SOELinkHandlerCOM::doSerialReception() {

	char serialData[SOE_SERIAL_BUFFER_LEN] {0};

	while (isAlive()) {

		// check if port closed unexpectedly
		if (this->localPort != 0 && !this->localPort->isOpen()) {
			printf("[!] lost connection to local serial port, closing connection\n");
			shutdown();
		}

		// check if port open, wait if not
		if (this->localPort == 0 || !this->localPort->isOpen()) {
			std::unique_lock<std::mutex> lock(this->m_localPort);
			this->cv_openLocalPort.wait(lock, [this]() {
				return (this->localPort != 0 && this->localPort->isOpen()) || !isAlive();
			});
			if (!isAlive()) break;
		}

		bool nothingToDo = this->serialData.dataAvailable() == 0;

		// try to write data from ring buffer to serial
		{
			// get how many bytes are available for transmission
			unsigned long availableBytes = this->serialData.dataAvailable();

			// if data available (or pending)
			if (availableBytes > 0) {

				// start transfer or (if already pending) check status of last transfer
				long long int written = this->localPort->writeBytes(this->serialData.dataStart(), availableBytes, false);
				if (written < -1) {
					continue; // when port closed / timed out
				}

				if (written < 0) {
					dbgprintf("[DBG] pending data: [serial] <- |network| : >%.*s<\n", availableBytes, this->serialData.dataStart());

					// the transmission buffer is to 75% full, send flow control signal
					if (availableBytes > (SOE_TCP_STREAM_BUFFER_LEN / 4 * 3) && this->remoteFlowEnable) {
						printf("[i] send flow control to remote: txenbl = false\n");
						sendFlowControl(this->remoteFlowEnable = false);
					}
					nothingToDo = true; // we need to wait for the data to be transmitted
				} else {
					dbgprintf("[DBG] stream data: [serial] <- |network| : >%.*s<\n", written, this->serialData.dataStart());

					// increment read position in buffer
					this->serialData.pushRead(written);

					nothingToDo = false; // data was written, its likely there is more to do
				}

			} else {
				// if flow was disabled, reactivate now
				if (!this->remoteFlowEnable) {
					printf("[i] send flow control to remote: txenbl = true\n");
					sendFlowControl(this->remoteFlowEnable = true);
				}
			}

		}

		// try to read data from serial, unless the remote end disabled transmission of more data trough flow control
		if (this->flowEnable) {

			long long int read = this->localPort->readBytes(serialData, SOE_SERIAL_BUFFER_LEN, false);

			if (read < -1) {
				continue; // when port closed / timed out
			}

			if (read < 0) {
				// if the read did not complete, wait for a brief moment and check status again, it might just need a few CPU cycles
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				read = this->localPort->readBytes(serialData, SOE_SERIAL_BUFFER_LEN, false);
			}

			if (read > 0) {

				dbgprintf("[DBG] stream data: |serial| -> [network] : >%.*s<\n", (unsigned int) read, serialData);

				// send data to remote
				if (!sendSerialData(serialData, (unsigned int) read)) {
					printf("[!] frame error, unable to transmit serial data\n");
					break;
				}

				nothingToDo = false; // data was read, its likely there is more to do

			}

		}

		// halt after some cycles with no work
		if (nothingToDo) {
			this->txHaltCycles++;
		} else {
			this->txHaltCycles = 0;
		}
		bool doHalt = this->txHaltCycles > SOE_TX_HALT_CYCLE_LIMIT;

		// check for COM state event and (if no thing else to do) wait for more data
		bool comStateChanged = true;
		bool dataReceived = true;
		bool dataTransmitted = !this->remoteFlowEnable;
		if (!this->localPort->waitForEvents(comStateChanged, dataReceived, dataTransmitted, doHalt)) {
			if (doHalt) this->txHaltCycles = 0;
			continue; // when port closed / timed out / wait aborted
		}
		if (doHalt) this->txHaltCycles = 0;

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

	SOELinkHandler::transmitSerialData(data, len);

	// kick the TX thread out of waiting state if it was waiting
	if (this->txHaltCycles) {
		this->txHaltCycles = 0;
		this->localPort->abortWait();
	}

}

void SerialOverEthernet::SOELinkHandlerCOM::updateFlowControl(bool enableTransmit) {

	printf("[i] received flow control: txenbl = %s\n", enableTransmit ? "true" : "false");

	SOELinkHandler::updateFlowControl(enableTransmit);

	// kick the TX thread out of waiting state if it was waiting
	if (this->txHaltCycles) {
		this->txHaltCycles = 0;
		this->localPort->abortWait();
	}

}

void SerialOverEthernet::SOELinkHandlerCOM::updatePortState(bool dtr, bool rts) {

	if (this->localPort == 0 || !this->localPort->isOpen()) return;

	if (!this->localPort->setManualPortState(dtr, rts)) {
		printf("[!] unable to apply port state from remote!\n");
	}

	// kick the TX thread out of waiting state if it was waiting
	if (this->txHaltCycles) {
		this->txHaltCycles = 0;
		this->localPort->abortWait();
	}

}
