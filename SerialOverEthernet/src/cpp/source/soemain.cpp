/*
 * soemain.cpp
 *
 * Main entry point for Serial Over Ethernet
 * Handles command line parsing and terminal input as well as network access.
 *
 *  Created on: 22.02.2025
 *      Author: Marvin Koehler
 */

#include <iostream>
#include <string.h>
#include <filesystem>
#include "soeimpl.hpp"

void openPort(SOESocketHandler& handler, const string& host, const string& port, const string& remotePort, const string& localPort, unsigned int baud) {
	vector<INetAddress> addresses;
	resolve_inet(host, port, true, addresses);
	for (INetAddress address : addresses) {
		SerialPortConfiguration config = DEFAULT_PORT_CONFIGURATION;
		config.baudRate = baud;
		if (handler.openRemotePort(address, remotePort, config, localPort)) break;
	}
}

int main(int argc, const char** argv) {

	// Disable output caching
	setbuf(stdout, NULL);

	// Print help when no arguments supplied
	if (argc == 1) {
		filesystem::path executable(argv[0]);
		string executableName = executable.filename().string();
		printf("%s (-addr [local IP]) (-port [local port])\n", executableName.c_str());
		return 1;
	}

	// Default configuration
	string port = to_string(DEFAULT_SOE_PORT);
	string host = "localhost";

	// Parse arguments for network connection
	for (int i = 0; i < argc; i++) {
		if (strcmp(argv[i], "-port") == 0) {
			if (i == argc - 1) return 1;
			port = string(argv[i + 1]);
		} else if (strcmp(argv[i], "-addr") == 0) {
			if (i == argc - 1) return 1;
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
			printf("serial over ethernet/IP, open client port on: %s %d\n", localAddress.c_str(), localPort);

			{
				// Start socket handler
				SOESocketHandler handler(socket);

				// Read console
				printf("type ...\n");
				printf(" exit - to force terminate this client\n");
				printf(" link - to establish an serial connection to an server\n");
				// TODO missing functions
//				printf(" unlink - to close an existing connection\n");
//				printf(" close - to close all connections\n");
				string cmd;
				while (true) {
					getline(std::cin, cmd);
					if (cmd == "exit" || cmd == "stop") break;
					if (cmd.rfind("link", 0) == 0) {
						// TODO better pasing and input validation
						int del1 = cmd.find(" ", 0);
						int del2 = cmd.find(" ", del1 + 1);
						int del3 = cmd.find(" ", del2 + 1);
						int del4 = cmd.find(" ", del3 + 1);
						int del5 = cmd.find(" ", del4 + 1);
						if (del1 > 0 && del2 > del1 && del3 > del2 && del4 > del3 && del5 > del4) {
							string hostStr = cmd.substr(del1 + 1, del2 - del1 - 1);
							string portStr = cmd.substr(del2 + 1, del3 - del2 - 1);
							string localPort = cmd.substr(del3 + 1, del4 - del3 - 1);
							string remotePort = cmd.substr(del4 + 1, del5 - del4 - 1);
							string baudRateStr = cmd.substr(del5 + 1, cmd.length() - del5 - 1);
							openPort(handler, hostStr, portStr, localPort, remotePort, stoul(baudRateStr));
						} else {
							printf("link [remote address] [port] [remote serial] [local serial] [baud rate]\n");
						}
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
