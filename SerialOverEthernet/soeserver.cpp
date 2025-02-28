/*
 * serial_over_ethernet.cpp
 *
 *  Created on: 03.02.2025
 *      Author: marvi
 */

#ifdef SIDE_SERVER

#include "soeimpl.hpp"

using namespace std;

int main(int argn, const char** argv) {

	// Disable output caching
	setvbuf(stdout, NULL, _IONBF, 0);

	// Default configuration
	string port = to_string(DEFAULT_SOE_PORT);
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
				}
			}

			InetCleanup();
			printf("server shutdown complete\n");
			return 0;

		}

	}

	printf("unable to bind to address: %s %s\n", host.c_str(), port.c_str());

	InetCleanup();
	return -1;

}

#endif
