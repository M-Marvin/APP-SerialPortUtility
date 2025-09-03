/*
 * soelinkhandlervcom.cpp
 *
 * Handles an single Serial over Ethernet/IP connection/link.
 * This file implements the code specific to an virtual serial port, in contrast to an normal serial port.
 *
 *  Created on: 04.02.2025
 *      Author: Marvin Koehler (M_Marvin)
 */

#ifdef PLATFORM_WIN

#include <string>
#include "soeconnection.hpp"
#include "dbgprintf.h"

bool SerialOverEthernet::SOELinkHandlerVCOM::openLocalPort(const std::string& localSerial) {
	closeLocalPort();
	std::unique_lock<std::mutex> lock(this->m_localPort);
	this->localPort.reset(SerialAccess::newVirtualSerialPortS(localSerial));
	this->localPortName = localSerial;
	dbgprintf("[DBG] opening local port: %s\n", this->localPortName.c_str());
	bool opened = this->localPort->createPort();
	if (opened) {
		this->cv_openLocalPort.notify_all();
	}
	return opened;
}

bool SerialOverEthernet::SOELinkHandlerVCOM::closeLocalPort() {
	if (this->localPort == 0 || !this->localPort->isCreated()) return true;
	std::unique_lock<std::mutex> lock(this->m_localPort);
	this->localPort->removePort();
	this->localPort.release();
	dbgprintf("[DBG] local port closed: %s\n", this->localPortName.c_str());
	return true;
}

bool SerialOverEthernet::SOELinkHandlerVCOM::setLocalConfig(const SerialAccess::SerialPortConfiguration& localConfig) {
	if (this->localPort == 0 || !this->localPort->isCreated()) return false;
	return true; // the configuration is set by the application
}

void SerialOverEthernet::SOELinkHandlerVCOM::doSerialReception() {

	char serialData[SOE_SERIAL_BUFFER_LEN] {0};

	while (isAlive()) {

		// check if port open, wait if not
		if (this->localPort == 0 || !this->localPort->isCreated()) {
			std::unique_lock<std::mutex> lock(this->m_localPort);
			this->cv_openLocalPort.wait(lock, [this]() {
				return (this->localPort != 0 && this->localPort->isCreated()) || !isAlive();
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
					dbgprintf("[DBG] pending data: [serial] <- |network| : read segment: %lu bytes, free buffer: %lu bytes\n", availableBytes, this->serialData.free());

					// the transmission buffer is to 75% full, send flow control signal
					if (this->serialData.free() < (SOE_TCP_STREAM_BUFFER_LEN / 4) && this->remoteFlowEnable) {
						dbgprintf("[DBG] send flow control to remote: txenbl = false\n");
						sendFlowControl(this->remoteFlowEnable = false);
					}

					nothingToDo = true; // we need to wait for the data to be transmitted
				} else {
					dbgprintf("[DBG] stream data: [serial] <- |network| : >%.*s<\n", written, this->serialData.dataStart());

					// increment read position in buffer
					this->serialData.pushRead(written);

					nothingToDo = false;
				}

			} else {
				// if flow was disabled, reactivate now
				if (!this->remoteFlowEnable) {
					dbgprintf("[DBG] send flow control to remote: txenbl = true\n");
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

				nothingToDo = false;
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
		bool configChanged = true;
		bool timeoutChanged = false;
		bool comStateChanged = true;
		bool dataReceived = true;
		bool dataTransmitted = !this->remoteFlowEnable;
		if (!this->localPort->waitForEvents(configChanged, timeoutChanged, comStateChanged, dataReceived, dataTransmitted, doHalt)) {
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

		if (configChanged) {

			// we need to make sure all config changes are applied before reding them
			// the event is fired as soon as the first changed is made
			std::this_thread::sleep_for(std::chrono::milliseconds(100));

			// notify remote port about new configuration
			SerialAccess::SerialPortConfiguration config;
			if (!this->localPort->getConfig(config)) {
				continue; // when port closed
			}

			dbgprintf("[DBG] stream port config: |serial| -> [network] (baud %u)\n", config.baudRate);

			if (!sendRemoteConfig(config)) {
				printf("[!] frame error, unable to transmit serial configuration\n");
				break;
			}
		}

	}

	dbgprintf("[DBG] client socket TX terminated, shutting down ...\n");
	shutdown();

}

void SerialOverEthernet::SOELinkHandlerVCOM::transmitSerialData(const char* data, unsigned int len) {

	SOELinkHandler::transmitSerialData(data, len);

	// kick the TX thread out of waiting state if it was waiting
	if (this->txHaltCycles) {
		this->txHaltCycles = 0;
		this->localPort->abortWait();
	}

}

void SerialOverEthernet::SOELinkHandlerVCOM::updateFlowControl(bool enableTransmit) {

	dbgprintf("[DBG] received flow control: txenbl = %s\n", enableTransmit ? "true" : "false");

	SOELinkHandler::updateFlowControl(enableTransmit);

	// kick the TX thread out of waiting state if it was waiting
	if (this->txHaltCycles) {
		this->txHaltCycles = 0;
		this->localPort->abortWait();
	}

}

void SerialOverEthernet::SOELinkHandlerVCOM::updatePortState(bool dtr, bool rts) {

	if (this->localPort == 0 || !this->localPort->isCreated()) return;

	if (!this->localPort->setManualPortState(dtr, rts)) {
		printf("[!] unable to apply port state from remote!\n");
	}

	// kick the TX thread out of waiting state if it was waiting
	if (this->txHaltCycles) {
		this->txHaltCycles = 0;
		this->localPort->abortWait();
	}

}

#endif
