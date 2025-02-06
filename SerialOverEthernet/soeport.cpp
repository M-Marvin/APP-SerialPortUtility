/*
 * soeport.cpp
 *
 *  Created on: 04.02.2025
 *      Author: marvi
 */

#include <corecrt.h>
#include <serial_port.h>
#include <cstdio>
#include <cwchar>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include "soeimpl.h"

using namespace std;

SOEPort::SOEPort(SOEClient* client, SerialPort &port, const char* portName) {
	this->portName = string(portName);
	this->client = client;
	this->port = &port;

	this->thread_tx = thread([this]() -> void {
		handlePortTX();
	});
	this->thread_rx = thread([this]() -> void {
		handlePortRX();
	});
}

SOEPort::~SOEPort() {
	this->port->closePort();
	{ unique_lock<mutex> lock(this->tx_waitm); } // TODO do we need this ?
	this->tx_waitc.notify_all();
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
	{
		lock_guard<mutex> lock(this->tx_stackm);
		this->tx_stack[txid] = pair<size_t, char*>(length, stackBuffer);
	}

	// Notify about new data
	{ unique_lock<mutex> lock(this->tx_waitm); }
	this->tx_waitc.notify_all();

	return true;

}

bool SOEPort::read(unsigned int* rxid, const char** buffer, size_t* length) {

	// Nothing to send if port is closed
	if (!this->port->isOpen()) return false;

	// Abort if no data available
	if (this->first_rxid == this->next_rxid) return false;

	// Get element from rx stack
	*rxid = this->first_rxid;
	try {
		pair<size_t, char*> stackEntry = this->rx_stack.at(*rxid);
		*buffer = stackEntry.second;
		*length = stackEntry.first;
		this->first_rxid++;
		return true;
	} catch (out_of_range &e) {
		return false;
	}

}

void SOEPort::confirmReception(unsigned int rxid) {

	// Delete entry from payload stack
	lock_guard<mutex> lock(this->rx_stackm);
	if (this->rx_stack.find(rxid) != this->rx_stack.end()) {
		this->rx_stack.erase(rxid);
	}

}

void SOEPort::handlePortTX() {
	// Init tx variables
	this->tx_stack = map<unsigned int, pair<size_t, char*>>();
	this->next_txid = 0;

	// Start tx loop
	while (this->port->isOpen() && this->client->isActive()) {

		// If not available, wait for more data
		if (this->tx_stack.find(this->next_txid) == this->tx_stack.end()) {
			unique_lock<mutex> lock(this->tx_waitm);
			this->tx_waitc.wait(lock);
			continue;
		}

		// Get next element from tx stack
		pair<size_t, char*> stackEntry = this->tx_stack.at(this->next_txid);

		// Remove entry from tx stack and increment last txid
		{
			lock_guard<mutex> lock(this->tx_stackm);
			this->tx_stack.erase(next_txid);
			this->next_txid++;
		}

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
	// Init rx variables
	this->first_rxid = 0;
	this->next_rxid = 0;
	this->rx_stack = map<unsigned int, pair<size_t, char*>>();

	// Start rx loop
	char* payload = new char[SERIAL_RX_BUF];
	while (this->port->isOpen() && this->client->isActive()) {

		// Wait for more payload
		size_t received = this->port->readBytes(payload, SERIAL_RX_BUF);

		// Continue if nothing was received
		if (received == 0) continue;

		// Put payload on reception stack
		{
			lock_guard<mutex> lock(this->rx_stackm);
			this->rx_stack[this->next_rxid++] = pair<size_t, char*>(received, payload);
		}

		// Allocate buffer for next payload
		payload = new char[SERIAL_RX_BUF];

#ifdef DEBUG_PRINTS
		printf("DEBUG: rx stack count: %llu len: %llu\n", this->rx_stack.size(), received);
#endif

		// Notify client TX thread that new data is available
		this->client->notifySerialData();

	}

	// Delete payload buffer
	delete[] payload;

	// Delete rx stack
	for (auto entry = this->rx_stack.begin(); entry != this->rx_stack.end(); entry++) {
		delete[] entry->second.second;
	}
	this->rx_stack.clear();
}
