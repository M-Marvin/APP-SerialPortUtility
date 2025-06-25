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

bool openPort(SOESocketHandler& handler, const string& host, const string& port, const string& remotePort, const string& localPort, const SerialPortConfiguration& config) {
	vector<INetAddress> addresses;
	resolve_inet(host, port, true, addresses);
	for (INetAddress address : addresses) {
		if (handler.openRemotePort(address, remotePort, config, localPort)) {
			printf("link established: %s %s <-> %s\n", host.c_str(), remotePort.c_str(), localPort.c_str());
			return true;
		}
	}
	return false;
}

void interpretFlags(SOESocketHandler& handler, const vector<string>& args) {

	// Parse arguments for connections
	string remoteHost;
	string remotePort;
	string remoteSerial;
	string localSerial;
	SerialPortConfiguration remoteConfig = DEFAULT_PORT_CONFIGURATION;
	bool link = false;

	for (auto flag = args.begin(); flag != args.end(); flag++) {
		if (flag + 1 != args.end()) {
			// Flags with argument
			if (*flag == "-addr") {
				remoteHost = *++flag;
			} else if (*flag == "-port") {
				remotePort = *++flag;
			} else if (*flag == "-rser") {
				remoteSerial = *++flag;
			} else if (*flag == "-lser") {
				localSerial = *++flag;
			} else if (*flag == "-baud") {
				remoteConfig.baudRate = stoul(*++flag);
			} else if (*flag == "-bits") {
				remoteConfig.dataBits = stoul(*++flag);
			} else if (*flag == "-stops") {
				flag++;
				if (*flag == "one") remoteConfig.stopBits = SPC_STOPB_ONE;
				if (*flag == "one-half") remoteConfig.stopBits = SPC_STOPB_ONE_HALF;
				if (*flag == "two") remoteConfig.stopBits = SPC_STOPB_TWO;
			} else if (*flag == "-parity") {
				flag++;
				if (*flag == "none") remoteConfig.parity = SPC_PARITY_NONE;
				if (*flag == "even") remoteConfig.parity = SPC_PARITY_EVEN;
				if (*flag == "odd") remoteConfig.parity = SPC_PARITY_ODD;
				if (*flag == "mark") remoteConfig.parity = SPC_PARITY_MARK;
				if (*flag == "space") remoteConfig.parity = SPC_PARITY_SPACE;
			} else if (*flag == "-flowctrl") {
				flag++;
				if (*flag == "none") remoteConfig.flowControl = SPC_FLOW_NONE;
				if (*flag == "xonxoff") remoteConfig.flowControl = SPC_FLOW_XON_XOFF;
				if (*flag == "rtscts") remoteConfig.flowControl = SPC_FLOW_RTS_CTS;
				if (*flag == "dsrdtr") remoteConfig.flowControl = SPC_FLOW_DSR_DTR;
			}
		}
		// Flags without arguments
		if (*flag == "-link" || *flag == "-unlink") {
			if (link) {
				if (remoteHost.empty() || remotePort.empty() || remoteSerial.empty() || localSerial.empty()) {
					printf("not enough arguments for connection\n");
					continue;
				}

				openPort(handler, remoteHost, remotePort, remoteSerial, localSerial, remoteConfig);
			}
		}
		if (*flag == "-link") {
			link = true;
		} else if (*flag == "-unlink") {
			//handler.closeRemotePort(remoteAddress, remotePortName);
		} else if (*flag == "-close") {
			link = false; // It makes no sense to apply the previous link call before closing all links again

		}
	}

	if (link) {
		if (remoteHost.empty() || remotePort.empty() || remoteSerial.empty() || localSerial.empty()) {
			printf("not enough arguments for connection\n");
			return;
		}

		openPort(handler, remoteHost, remotePort, remoteSerial, localSerial, remoteConfig);
	}
}

int mainCPP(filesystem::path& exec, vector<string>& args) {

	// Disable output caching
	setbuf(stdout, NULL);

	// Print help when no arguments supplied
	if (args.size() == 0) {
		printf("%s <options ...> <-link <link options ...> ...>\n", exec.filename().string().c_str());
		printf("options:\n");
		printf(" -addr [local IP]\n");
		printf(" -port [local network port]\n");
		printf("link options:\n");
		printf(" -addr [remote IP]\n");
		printf(" -port [remote network port]\n");
		printf(" -rser [remote serial port]\n");
		printf(" -lser [serial port]\n");
		printf(" -baud [serial baud]\n");
		printf(" -stops [serial stop bits]\n");
		printf(" -");
		return 1;
	}

	// Default configuration
	string port = to_string(DEFAULT_SOE_PORT);
	string host = "localhost";

	// Parse arguments for network connection
	auto flag = args.begin();
	for (; flag != args.end(); flag++) {
		if (flag + 1 != args.end()) {
			// Flags with argument
			if (*flag == "-addr") {
				host = *++flag;
			} else if (*flag == "-port") {
				port = *++flag;
			}
		}
		// Flags without arguments
		if (*flag == "-link") {
			break; // End of local network arguments
		}
	}
	if (flag != args.begin())
		args.erase(args.begin(), flag - 1);

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

				// Interpret supplied command flags
				interpretFlags(handler, args);

				// Read console
				printf("type ...\n");
				printf(" -exit - to force terminate this client\n");
				printf(" -link <link options ...> - to establish an serial connection to an server\n");
				printf(" -unlink [remote serial port] - to close an existing connection\n");
				// TODO missing functions
//				printf(" -close - to close all connections\n");
				string cmdline;
				while (true) {

					// Read command and arguments
					getline(std::cin, cmdline);
					vector<string> cmdargs;
					string::size_type p1 = 0;
					for (string::size_type p2 = cmdline.find(' '); p2 != string::npos; p2 = cmdline.find(' ', p1)) {
						cmdargs.emplace_back(cmdline, p1, p2 - p1);
						p1 = p2 + 1;
					}
					cmdargs.emplace_back(cmdline, p1);

					// Interpret command flags
					if (cmdargs[0] == "-exit" || cmdargs[0] == "exit") {
						break;
					} else {
						interpretFlags(handler, cmdargs);
					}

				}
			}

			// Cleanup network and exit
			InetCleanup();
			printf("client shutdown complete\n");
			return 0;

		}

	}

	printf("unable to bind to address: %s %s\n", host.c_str(), port.c_str());

	InetCleanup();
	return -1;

}

int main(int argc, const char** argv) {

	filesystem::path exec(argv[0]);
	vector<string> args;
	for (unsigned int i1 = 1; i1 < argc; i1++)
		args.emplace_back(argv[i1]);

	return mainCPP(exec, args);

}
