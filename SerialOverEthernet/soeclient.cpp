/*
 * soeclient.cpp
 *
 *  Created on: 22.02.2025
 *      Author: marvi
 */

#ifdef SIDE_CLIENT

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

#include <map>

void testConnect() {

	INetAddress localAddress = INetAddress();
	string local = "0.0.0.0";
	localAddress.fromstr(local, DEFAULT_SOE_PORT);

	INetAddress remoteAddress = INetAddress();
	string host = "192.168.178.59";
	remoteAddress.fromstr(host, DEFAULT_SOE_PORT);

	Socket* socket = new Socket();
	if (!socket->bind(localAddress)) {
		delete socket;
		printf("failed to open socket!\n");
		return;
	}

	printf("connected socket\n");

	SOESocketHandler* soeHandler = new SOESocketHandler(socket);

	printf("created soe handler\n");

	if (!soeHandler->openRemotePort(remoteAddress, "\\\\.\\COM3", 250000, "\\\\.\\COM10", 4000)) {
		printf("failed to establish connection!\n");
	} else {

		while (true) {}

		if (!soeHandler->closeRemotePort(remoteAddress, "\\\\.\\COM10", 4000)) {
			printf("close connection failed!\n");
		}

	}

	socket->close();
	while (soeHandler->isActive()) {}
	delete soeHandler;
	printf("connection closed\n");

}

int main(int argn, const char** argv) {

	// Disable output caching
	setvbuf(stdout, NULL, _IONBF, 0);

	printf("client test\n");

	// Parse arguments
//	const char* serverAddress = 0;
//	for (int i = 0; i < argn; i++) {
//		if (strcmp(argv[i], "-help") == 0) {
//			printf("soa <TODO>");
//			return 0;
//		} else if (strcmp(argv[i], "-port") == 0) {
//			if (i == argn - 1) return 1;
//			port = atoi(argv[i + 1]);
//			if (port == 0) return 1;
//		}
//	}

	// Initialize networking
	if (!InetInit()) {
		printf("failed to initialize network system!\n");
		return -1;
	}

	testConnect();

	// Cleanup and exit
	InetCleanup();
	printf("soa shutdown completed");
	return 0;

}


#endif
