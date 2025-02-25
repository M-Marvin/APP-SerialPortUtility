/*
 * soeport.cpp
 *
 *  Created on: 04.02.2025
 *      Author: marvi
 */

#include <corecrt.h>
#include <serial_port.h>
#include <condition_variable>
#include <cstdio>
#include <cwchar>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <chrono>

#include "soeimpl.h"

using namespace std;

SOEPortHandler::SOEPortHandler(SOESocketHandler* client, SerialPort &port, const char* portName) {
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

SOEPortHandler::~SOEPortHandler() {
	this->port->closePort();
	{ unique_lock<mutex> lock(this->tx_stackm); } // TODO do we need this ?
	this->tx_waitc.notify_all();
	{ unique_lock<mutex> lock(this->rx_stackm); } // TODO do we need this ?
	this->rx_waitc.notify_all();
	this->thread_tx.join();
	this->thread_rx.join();
	delete this->port;
}

bool SOEPortHandler::isOpen() {
	return this->port->isOpen();
}

bool SOEPortHandler::send(unsigned int txid, const char* buffer, unsigned long length) {

	// Do not insert any data if this port is shutting down
	if (!this->port->isOpen()) return false;

	// Ignore packages which txid is "in the past"
	printf("txid in: %u txid next: %u \n", txid, this->next_txid);
	if (txid < this->next_txid && this->next_txid - txid < 0x7FFFFFFF) {
		printf("ERROR 2 txid in: %u txid next: %u \n", txid, this->next_txid);
		return false;
	}

	// If tx stack has reached limit, abort operation, only make an exception if this is the next package that should be send
	if (this->tx_stack.size() >= RX_STACK_LIMIT && txid != this->next_txid) return false;

	// Copy payload bytes and insert in tx stack at txid
	char* stackBuffer = new char[length];
	memcpy(stackBuffer, buffer, length);
	{
		lock_guard<mutex> lock(this->tx_stackm);
		this->tx_stack[txid] = pair<unsigned long, char*>(length, stackBuffer);
		this->tx_waitc.notify_all();
	}

#ifdef DEBUG_PRINTS
		printf("DEBUG: network -> %s tx stack: [tx %u] size %llu len: %lu\n", this->portName.c_str(), txid, this->tx_stack.size(), length);
#endif

	return true;

}

bool SOEPortHandler::read(unsigned int* rxid, const char** buffer, unsigned long* length) {

	// Nothing to send if port is closed
	if (!this->port->isOpen()) return false;

	// If rx stack at limit, reset rxid to last cached package to resend everything
	if (this->rx_stack.size() >= RX_STACK_LIMIT && this->next_transmit_rxid == this->next_free_rxid) {
		if (this->is_repeating) {
			this->is_repeating = false;
			return false;
		}

		printf("REPEAT\n");
		this->next_transmit_rxid = this->last_transmitted_rxid;
		this->is_repeating = true;
	}

	// Abort if no data available
	if (this->next_transmit_rxid == this->next_free_rxid) return false;

	// Get element from rx stack
	*rxid = this->next_transmit_rxid;
	try {
		pair<unsigned long, char*> stackEntry = this->rx_stack.at(*rxid);
		*buffer = stackEntry.second;
		*length = stackEntry.first;
		this->next_transmit_rxid++;

		printf("rxid out: %u \n", *rxid);
		return true;
	} catch (out_of_range &e) {
		return false;
	}

}

void SOEPortHandler::confirmTransmission(unsigned int rxid) {

	// Delete entry from payload stack and release reception hold
	lock_guard<mutex> lock(this->rx_stackm);

	// Remove all packages before and including rxid
	for (unsigned int i = this->last_transmitted_rxid; i != rxid + 1; i++) {
		this->rx_stack.erase(i);

#ifdef DEBUG_PRINTS
		printf("DEBUG: network <- %s rx stack (tx confirm): [rx %u] size %llu\n", this->portName.c_str(), i, this->rx_stack.size());
#endif

	}
	this->last_transmitted_rxid = rxid + 1;

	// Resume reception, in case it was paused
	this->rx_waitc.notify_all();

}

void SOEPortHandler::handlePortTX() {
	// Init tx variables
	this->tx_stack = map<unsigned int, pair<unsigned long, char*>>();
	this->next_txid = 0;

	// Start tx loop
	while (this->port->isOpen() && this->client->isActive()) {

		// If not available, wait for more data
		if (this->tx_stack.find(this->next_txid) == this->tx_stack.end()) {
			unique_lock<mutex> lock(this->tx_stackm);
			this->tx_waitc.wait(lock);
			continue;
		}

		// Get next element from tx stack
		pair<unsigned long, char*> stackEntry = this->tx_stack.at(this->next_txid);

		// Remove entry from tx stack and increment last txid
		{
			unique_lock<mutex> lock(this->tx_stackm);
			this->tx_stack.erase(next_txid);
			this->next_txid++;
		}

#ifdef DEBUG_PRINTS
		printf("DEBUG: serial <- %s tx stack: [tx %u] size %llu len: %lu\n", this->portName.c_str(), this->next_txid - 1, this->tx_stack.size(), stackEntry.first);
#endif

		// Transmit data over serial
		unsigned long transmitted = 0;
		while (transmitted < stackEntry.first) {
			transmitted += this->port->writeBytes(stackEntry.second + transmitted, stackEntry.first - transmitted);
			if (transmitted == 0) {
				this->client->sendError(this->portName.c_str(), "failed to transmit payload on serial, no listener");
				std::this_thread::sleep_for(std::chrono::milliseconds(SERIAL_TX_REP_INTERVAL));
			}
		}
		delete[] stackEntry.second;

		// Send transmission confirmation
		if (!this->client->sendConfirm(true, this->portName.c_str(), this->next_txid - 1)) {
			this->client->sendError(this->portName.c_str(), "failed to transmit TX_CONFIRM");
		}

	}

	// Delete all remaining tx stack entries
	for (auto entry = this->tx_stack.begin(); entry != this->tx_stack.end(); entry++) {
		delete[] entry->second.second;
	}
	this->tx_stack.clear();
}

void SOEPortHandler::handlePortRX() {
	// Init rx variables
	this->next_transmit_rxid = 0;
	this->next_free_rxid = 0;
	this->last_transmitted_rxid = 0;
	this->rx_stack = map<unsigned int, pair<unsigned long, char*>>();

	// Start rx loop
	char* payload = new char[SERIAL_RX_BUF];
	while (this->port->isOpen() && this->client->isActive()) {

		// Wait for more payload
		unsigned long received = this->port->readBytes(payload, SERIAL_RX_BUF);
		//unsigned long received = this->port->readBytesConsecutive(payload, SERIAL_RX_BUF, SERIAL_CONSEC_DELAY, SERIAL_RX_TIMEOUT);

		// Continue if nothing was received
		if (received == 0) continue;

		// Put payload on reception stack
		{
			unique_lock<mutex> lock(this->rx_stackm);

			// If rx stack exceeds buffer limit, hold reception ! THIS WILL CAUSE DATA LOSS, BUT THIS MEANS THERE IS SOMETHING WRONG WITH THE REMOTE LINK
			if (this->rx_stack.size() >= RX_STACK_LIMIT && this->port->isOpen() && this->client->isActive()) {

#ifdef DEBUG_PRINTS
				printf("DEBUG: %s rx stack limit reached, reception hold: %llu entries\n", this->portName.c_str(), this->rx_stack.size());
#endif

				this->rx_waitc.wait(lock, [this]() { return this->rx_stack.size() < RX_STACK_LIMIT || !(this->port->isOpen() && this->client->isActive()); });
			}

			// TODO better system: try add to last stack until the networking thread anounces that it is being transmitted, only then allocate new entry
			this->rx_stack[this->next_free_rxid++] = pair<unsigned long, char*>(received, payload);
		}

		// Allocate buffer for next payload
		payload = new char[SERIAL_RX_BUF];

#ifdef DEBUG_PRINTS
		printf("DEBUG: serial -> %s rx stack: [rx %u] size %llu len: %lu\n", this->portName.c_str(), this->next_free_rxid - 1, this->rx_stack.size(), received);
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
