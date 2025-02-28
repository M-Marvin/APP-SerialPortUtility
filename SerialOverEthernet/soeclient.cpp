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
#include <network.hpp>
#include <serial_port.hpp>
#include <thread>
#include <vector>
#include <mutex>
#include <algorithm>
#include "soeimpl.hpp"

#include <map>

void testConnect() {

	INetAddress localAddress = INetAddress();
	string local = "0.0.0.0";
	localAddress.fromstr(local, 0);

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

	SerialPortConfiguration config = DEFAULT_PORT_CONFIGURATION;
	config.baudRate = 250000;

	if (!soeHandler->openRemotePort(remoteAddress, "\\\\.\\COM3", config, "\\\\.\\COM10", 4000)) {
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

	// Default configuration
	string port = "0"; // Assign dynamic port
	string host = "localhost";

	// Parse arguments
	for (int i = 0; i < argn; i++) {
		if (strcmp(argv[i], "-help") == 0) {
			printf("soa (-port *soe-port-number*) (-addr *soe-local-ip*)");
			return 0;
		} else if (strcmp(argv[i], "-port") == 0) {
			if (i == argn - 1) return 1;
			port = string(argv[i + 1]);
		} else if (strcmp(argv[i], "-addr") == 0) {
			if (i == argn - 1) return 1;
			host = string(argv[i + 1]);
		}
	}

	// Initialize networking
	if (!InetInit()) {
		printf("failed to initialize network!\n");
		return -1;
	}

	// Resolve supplied host string
	vector<INetAddress> localAddresses;
	resolve_inet(host, port, true, localAddresses);

	// Attempt to bind to first available local host address
	Socket* socket = new Socket();
	for (INetAddress& address : localAddresses) {

		if (socket->bind(address)) {

			string localAddress;
			unsigned int localPort;
			address.tostr(localAddress, &localPort);
			printf("serial over ethernet/IP, open server port on: %s %d\n", localAddress.c_str(), localPort);

			{
				// Start socket handler
				SOESocketHandler handler(socket);

				// Read console
				string cmd;
				while (true) {
					getline(std::cin, cmd);
					if (cmd == "exit" || cmd == "stop") break;

					if (cmd.rfind("open", 0) == 0) {



						printf("%s\n", cmd.c_str());
					}
				}
			}

			InetCleanup();
			printf("client shutdown complete\n");
			return 0;

		}

	}

	printf("unable to bind to address: %s %s\n", host.c_str(), port.c_str());

	InetCleanup();
	return -1;

}


#endif
