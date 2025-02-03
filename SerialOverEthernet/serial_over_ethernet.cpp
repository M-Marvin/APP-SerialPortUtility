/*
 * serial_over_ethernet.cpp
 *
 *  Created on: 03.02.2025
 *      Author: marvi
 */

#include <stdio.h>
#include <iostream>
#include <string.h>
#include <network.h>
#include <serial_port.h>
#include <thread>
#include <vector>
#include <mutex>
#include <algorithm>
#include "serial_over_ethernet.h"

#define DEFAULT_SOE_PORT 26

using namespace std;

Socket listenSocket;
mutex handlersm;
vector<thread*> handlers = vector<thread*>();

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

	// Wait for incoming requests until termination is requested (usually never)
	while (listenSocket.isOpen()) {

		// Accept incoming connections
		Socket* clientSocket = new Socket();
		if (!listenSocket.accept(*clientSocket)) {
			delete clientSocket;
			continue;
		}

		// Start new client handler
		startNewHandler(clientSocket);

	}

	// Close server port
	listenSocket.close();

	while (handlers.size() > 0);

	// Cleanup and exit
	InetCleanup();
	printf("soa shutdown completed");
	return 0;

}

void shutdown() {
	printf("soa shutdown requested ...\n");
	listenSocket.close();
}

void startNewHandler(Socket* socket) {
	handlersm.lock();
	thread* handlerThread = new thread([handlerThread, socket]() -> void {
		clientHandle(*socket);
		socket->close();
		delete socket;
		handlersm.lock();
		handlers.erase(remove(handlers.begin(), handlers.end(), handlerThread));
		handlersm.unlock();
	});
	handlers.push_back(handlerThread);
	handlersm.unlock();
}

void clientHandle(Socket &socket) {
	char receptionBuffer[256];
	while (socket.isOpen()) {

		memset(receptionBuffer, 0, 256);

		unsigned int received = 0;
		if (!socket.receive(receptionBuffer, 256, &received)) {
			printf("failed to receive data from client socket!\n");
			break;
		}

		if (!socket.isOpen()) break;

		printf("received: %s\n", receptionBuffer);

		// ECHO BACK TEST
		socket.send(receptionBuffer, received);

	}
}
