/*
 * serial_over_ethernet.cpp
 *
 *  Created on: 03.02.2025
 *      Author: marvi
 */

#include "soeserver.h"

#include <stdio.h>
#include <iostream>
#include <string.h>
#include <network.h>
#include <serial_port.h>
#include <thread>
#include <vector>
#include <mutex>
#include <algorithm>
#include "soeimpl.h"

#define DEFAULT_SOE_PORT 26

using namespace std;

Socket listenSocket;
vector<SOEClient*> clients = vector<SOEClient*>();

int main(int argn, const char** argv) {

	// Disable output caching
	setvbuf(stdout, NULL, _IONBF, 0);

	// Default configuration
	unsigned int port = DEFAULT_SOE_PORT;

	// Parse arguments
	for (int i = 0; i < argn; i++) {
		if (strcmp(argv[i], "-help") == 0) {
			printf("soa (-port *soe-port-number*)");
			return 0;
		} else if (strcmp(argv[i], "-port") == 0) {
			if (i == argn - 1) return 1;
			port = atoi(argv[i + 1]);
			if (port == 0) return 1;
		}
	}

	// Initialize networking
	if (!InetInit()) {
		printf("failed to initialize network system!\n");
		return -1;
	}

	// Open server port
	printf("open soa port: %d\n", port);
	if (!listenSocket.listen(port)) {
		printf("failed to claim port!\n");
		return -1;
	}

	handleClientReception();

	// Close server port
	listenSocket.close();

	// Wait for all clients to terminate
	while (clients.size() > 0)
		cleanupClosedClients();

	// Cleanup and exit
	InetCleanup();
	printf("soa shutdown completed");
	return 0;

}

void shutdown() {
	printf("soa shutdown requested ...\n");
	listenSocket.close();
}

void cleanupClosedClients() {

	// Cleanup and set to NULL all close/inactive clients
	for_each(clients.begin(), clients.end(), [](SOEClient* &client) -> void {
		if (!client->isActive()) {
			delete client;
			client = 0;
		}
	});

	// Delete all NULL entries from the list of clients
	clients.erase(remove(clients.begin(), clients.end(), (SOEClient*) 0), clients.end());

}

void handleClientReception() {
	while (listenSocket.isOpen()) {

		// Accept incoming connections
		Socket* clientSocket = new Socket();
		if (!listenSocket.accept(*clientSocket)) {
			delete clientSocket;
			continue;
		}

		cleanupClosedClients();

		// Start new client handler
		SOEClient* client = new SOEClient(*clientSocket);
		clients.push_back(client);

	}
}
