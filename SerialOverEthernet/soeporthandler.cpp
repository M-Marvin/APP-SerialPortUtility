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
	if (txid < this->next_txid && this->next_txid - txid < 0x7FFFFFFF) {
		return false;
	}

	// If tx stack has reached limit, abort operation, only make an exception if this is the next package that should be send
	if (this->tx_stack.size() >= SERIAL_RX_STACK_LIMIT && txid != this->next_txid) return false;

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
	if (this->rx_stack.size() >= SERIAL_RX_STACK_LIMIT && this->next_transmit_rxid == this->next_free_rxid) {
		if (this->is_repeating) {
			this->is_repeating = false;
			return false;
		}

		this->next_transmit_rxid = this->last_transmitted_rxid;
		this->is_repeating = true;
	}

	// Get element from rx stack
	try {
		lock_guard<mutex> lock(this->rx_stackm);
		pair<unsigned long, char*> stackEntry = this->rx_stack.at(this->next_transmit_rxid);
		if (stackEntry.first == 0) return false; // No more data
		*rxid = this->next_transmit_rxid++;
		*buffer = stackEntry.second;
		*length = stackEntry.first;
		if (this->next_free_rxid < this->next_transmit_rxid)
			this->next_free_rxid = this->next_transmit_rxid;

		return true;
	} catch (out_of_range &e) {
		return false; // No more data
	}

}

void SOEPortHandler::confirmTransmission(unsigned int rxid) {

	// Delete entry from payload stack and release reception hold
	lock_guard<mutex> lock(this->rx_stackm);

	// Remove all packages before and including rxid
	for (unsigned int i = this->last_transmitted_rxid; i != rxid + 1; i++) {
		delete[] this->rx_stack[i].second;
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
	this->next_free_rxid = 0;
	this->next_transmit_rxid = 0;
	this->last_transmitted_rxid = 0;
	this->is_repeating = false;
	this->rx_stack = map<unsigned int, pair<unsigned long, char*>>();

	// Start rx loop
	while (this->port->isOpen() && this->client->isActive()) {

		// Try read payload from serial, append on next free rx stack entry
		{
			unique_lock<mutex> lock(this->rx_stackm);
			pair<unsigned long, char*>& stackEntry = this->rx_stack[this->next_free_rxid];

			// If entry if full, allocate new one, unles stack reached limit, then hold reception
			if (stackEntry.first >= SERIAL_RX_ENTRY_LEN) {
				if (this->rx_stack.size() >= SERIAL_RX_STACK_LIMIT) {
#ifdef DEBUG_PRINTS
					printf("DEBUG: %s rx stack limit reached, reception hold: %llu entries\n", this->portName.c_str(), this->rx_stack.size());
#endif
					this->rx_waitc.wait(lock, [this]() { return this->rx_stack.size() < SERIAL_RX_STACK_LIMIT || !(this->port->isOpen() && this->client->isActive()); });
				}
				stackEntry = this->rx_stack[++this->next_free_rxid];
			}

			// If newly created entry, initialize first
			if (stackEntry.second == 0) {
				stackEntry.second = new char[SERIAL_RX_ENTRY_LEN] {0};
			}

			unsigned long received = this->port->readBytes(stackEntry.second + stackEntry.first, SERIAL_RX_ENTRY_LEN - stackEntry.first);

#ifdef DEBUG_PRINTS
			if (received != 0) printf("DEBUG: serial -> %s rx stack: [rx %u] size %llu len: %lu + %lu\n", this->portName.c_str(), this->next_free_rxid, this->rx_stack.size(), stackEntry.first, received);
#endif

			stackEntry.first += received;

		}

		// Notify client TX thread that new data is available
		this->client->notifySerialData();

	}

	// Delete rx stack
	for (auto entry = this->rx_stack.begin(); entry != this->rx_stack.end(); entry++) {
		delete[] entry->second.second;
	}
	this->rx_stack.clear();
}
