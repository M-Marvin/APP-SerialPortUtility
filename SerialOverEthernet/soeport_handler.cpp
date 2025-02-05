/*
 * soeport.cpp
 *
 *  Created on: 04.02.2025
 *      Author: marvi
 */

#include <corecrt.h>
#include <serial_port.h>
#include <cwchar>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include "soeimpl.h"

using namespace std;

SOEPort::SOEPort(SOEClient* client, SerialPort &port, const char* portName) {
	this->portName = string(portName);
	this->client = client;
	this->port = &port;
	this->tx_stack = map<unsigned int, pair<size_t, char*>>();
	this->tx_waitm.lock();
	this->next_txid = 0;
	this->thread_tx = thread([this]() -> void {
		handlePortTX();
	});
	this->thread_rx = thread([this]() -> void {
		handlePortRX();
	});
}

SOEPort::~SOEPort() {
	this->port->closePort();
	this->tx_waitm.unlock();
	this->thread_tx.join();
	this->thread_rx.join();
	delete this->port;
}

bool SOEPort::isOpen() {
	return this->port->isOpen();
}

bool SOEPort::send(unsigned int txid, const char* buffer, size_t length) {

	// Do not insert any data if this port is shutting down
	if (!this->port->isOpen()) return false;

	// Ignore packages which txid is "in the past"
	if (txid < this->next_txid && this->next_txid - txid < 0x7FFFFFFF) return false;

	// Copy payload bytes and insert in tx stack at txid
	char* stackBuffer = new char[length];
	memcpy(stackBuffer, buffer, length);
	this->tx_stackm.lock();
	this->tx_stack[txid] = pair(length, stackBuffer);
	this->tx_stackm.unlock();

	// Notify about new data
	this->tx_waitm.unlock();

	return true;

}

bool SOEPort::read(unsigned int* rxid, const char** buffer, size_t* length) {

	// Nothing to send if port is closed
	if (!this->port->isOpen()) return false;

	// Check if payload available
	if (this->rx_stack.size() == 0) return false;



}

void SOEPort::handlePortTX() {
	while (this->port->isOpen() && this->client->isActive()) {

		// If not available, wait for more data
		if (this->tx_stack.find(this->next_txid) == this->tx_stack.end()) {
			this->tx_waitm.lock();
			continue;
		}

		// Get next element from tx stack
		pair<size_t, char*> stackEntry = this->tx_stack.at(this->next_txid);

		// Remove entry from tx stack and increment last txid
		this->tx_stackm.lock();
		this->tx_stack.erase(next_txid);
		this->next_txid++;
		this->tx_stackm.unlock();

		// Transmit and delete data over serial
		unsigned long transmited = this->port->writeBytes(stackEntry.second, stackEntry.first);
		delete[] stackEntry.second;

		if (transmited != stackEntry.first) {
			this->client->sendError(this->portName.c_str(), "failed to transmit payload on serial");
			break;
		} else {
			if (!this->client->sendTransmissionConfirm(this->portName.c_str(), this->next_txid - 1)) {
				this->client->sendError(this->portName.c_str(), "failed to transmit TX_CONFIRM");
				break;
			}
		}

	}

	// Delete all remaining tx stack entries
	for (auto entry = this->tx_stack.begin(); entry != this->tx_stack.end(); entry++) {
		delete[] entry->second.second;
	}
	this->tx_stack.clear();
}

void SOEPort::handlePortRX() {
	char* payload = new char[SERIAL_RX_BUF];
	while (this->port->isOpen() && this->client->isActive()) {

		// Wait for more payload
		size_t received = this->port->readBytes(payload, SERIAL_RX_BUF);

		// Continue if nothing was received
		if (received == 0) continue;

		// Put payload on reception stack
		this->rx_stackm.lock();
		this->rx_stack[this->next_rxid++] = pair(received, payload);
		this->rx_stackm.unlock();

		// Allocate buffer for next payload
		payload = new char[SERIAL_RX_BUF];

		printf("DEBUG: rx stack count: %llu len: %llu\n", this->rx_stack.size(), received);

	}

	// Delete payload buffer
	delete[] payload;

	// Delete rx stack
	for (auto entry = this->rx_stack.begin(); entry != this->rx_stack.end(); entry++) {
		delete[] entry->second.second;
	}
	this->rx_stack.clear();
}
