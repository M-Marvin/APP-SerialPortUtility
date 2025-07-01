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
#include "soeimpl.hpp"
#include "dbgprintf.h"

bool openPort(SerialOverEthernet::SOESocketHandler& handler, const std::string& host, const std::string& port, const std::string& remotePort, const std::string& localPort, const SerialAccess::SerialPortConfiguration& config) {
	std::vector<NetSocket::INetAddress> addresses;
	NetSocket::resolveInet(host, port, true, addresses);
	for (auto address : addresses) {
		if (handler.openRemotePort(address, remotePort, config, localPort)) {
			printf("[i] link established: %s %s <-> %s\n", host.c_str(), remotePort.c_str(), localPort.c_str());
			return true;
		}
	}
	return false;
}

void interpretFlags(SerialOverEthernet::SOESocketHandler& handler, const std::vector<std::string>& args) {

	// Parse arguments for connections
	std::string remoteHost;
	std::string remotePort = std::to_string(DEFAULT_SOE_PORT);
	std::string remoteSerial;
	std::string localSerial;
	SerialAccess::SerialPortConfiguration remoteConfig = SerialAccess::DEFAULT_PORT_CONFIGURATION;
	bool link = false;

	for (auto flag = args.begin(); flag != args.end(); flag++) {

		// Complete last link call before processing other options
		if (*flag == "-link" || *flag == "-unlink") {
			if (link) {
				if (remoteHost.empty() || remotePort.empty() || remoteSerial.empty() || localSerial.empty()) {
					printf("[!] not enough arguments for connection\n");
					continue;
				}
				link = false;

				openPort(handler, remoteHost, remotePort, remoteSerial, localSerial, remoteConfig);
			}
		}

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
				if (*flag == "one") remoteConfig.stopBits = SerialAccess::SPC_STOPB_ONE;
				if (*flag == "one-half") remoteConfig.stopBits = SerialAccess::SPC_STOPB_ONE_HALF;
				if (*flag == "two") remoteConfig.stopBits = SerialAccess::SPC_STOPB_TWO;
			} else if (*flag == "-parity") {
				flag++;
				if (*flag == "none") remoteConfig.parity = SerialAccess::SPC_PARITY_NONE;
				if (*flag == "even") remoteConfig.parity = SerialAccess::SPC_PARITY_EVEN;
				if (*flag == "odd") remoteConfig.parity = SerialAccess::SPC_PARITY_ODD;
				if (*flag == "mark") remoteConfig.parity = SerialAccess::SPC_PARITY_MARK;
				if (*flag == "space") remoteConfig.parity = SerialAccess::SPC_PARITY_SPACE;
			} else if (*flag == "-flowctrl") {
				flag++;
				if (*flag == "none") remoteConfig.flowControl = SerialAccess::SPC_FLOW_NONE;
				if (*flag == "xonxoff") remoteConfig.flowControl = SerialAccess::SPC_FLOW_XON_XOFF;
				if (*flag == "rtscts") remoteConfig.flowControl = SerialAccess::SPC_FLOW_RTS_CTS;
				if (*flag == "dsrdtr") remoteConfig.flowControl = SerialAccess::SPC_FLOW_DSR_DTR;
			} else if (*flag == "-unlink") {
				flag++;
				if (!handler.closeLocalPort(*flag)) {
					printf("[!] unable to close port: %s\n", flag->c_str());
				} else {
					printf("[i] link closed: %s\n", flag->c_str());
				}
			}
		}
		// Flags without arguments

		if (*flag == "-link") {
			link = true;
		} else if (*flag == "-close") {
			if (!handler.closeAllPorts()) {
				printf("[!] some ports failed to close and might still be open\n");
			} else {
				printf("[i] all links closed\n");
			}
		} else if (*flag == "-list") {
			handler.listAllPorts();
		}
	}

	if (link) {
		if (remoteHost.empty() || remotePort.empty() || remoteSerial.empty() || localSerial.empty()) {
			printf("[!] not enough arguments for connection\n");
			return;
		}

		openPort(handler, remoteHost, remotePort, remoteSerial, localSerial, remoteConfig);
	}
}

int mainCPP(std::string& exec, std::vector<std::string>& args) {

	// Disable output caching
	setbuf(stdout, NULL);
	dbgprintf("[DBG] test dbg output\n");

	// Print help when no arguments supplied
	if (args.size() == 0) {
		std::string execName = exec.substr(exec.find_last_of("/\\") + 1);
		printf("%s <options ...> <-link <link options ...> ...>\n", execName.c_str());
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
		return 1;
	}

	// Default configuration
	std::string port = std::to_string(DEFAULT_SOE_PORT);
	std::string host = "localhost";

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
		if (*flag == "-link" || *flag == "-unlink" || *flag == "-close" || *flag == "-list" || *flag == "-exit") {
			break; // End of local network arguments
		}
	}
	if (flag != args.begin())
		args.erase(args.begin(), flag - 1);

	// Initialize networking
	if (!NetSocket::InetInit()) {
		printf("[!] failed to initialize network!\n");
		return -1;
	}

	// Resolve supplied host string
	std::vector<NetSocket::INetAddress> localAddresses;
	NetSocket::resolveInet(host, port, true, localAddresses);

	// Attempt to bind to first available local host address
	NetSocket::Socket* socket = NetSocket::newSocket();
	for (NetSocket::INetAddress& address : localAddresses) {

		if (socket->bind(address)) {

			std::string localAddress;
			unsigned int localPort;
			address.tostr(localAddress, &localPort);
			printf("[i] serial over ethernet/IP, open client port on: %s %d\n", localAddress.c_str(), localPort);

			{
				// Start socket handler
				SerialOverEthernet::SOESocketHandler handler(socket);

				// Interpret supplied command flags
				interpretFlags(handler, args);

				// Read console
				printf("type ...\n");
				printf(" -exit - to force terminate this client\n");
				printf(" -link <link options ...> - to establish an serial connection to an server\n");
				printf(" -unlink [local serial port] - to close an existing connection\n");
				printf(" -close - to close all connections\n");
				printf(" -list - to view all currently open port links\n");
				std::string cmdline;
				while (socket->isOpen()) {

					// Read command and arguments
					std::getline(std::cin, cmdline);
					std::vector<std::string> cmdargs;
					std::string::size_type p1 = 0;
					for (std::string::size_type p2 = cmdline.find(' '); p2 != std::string::npos; p2 = cmdline.find(' ', p1)) {
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
			NetSocket::InetCleanup();
			printf("[i] client shutdown complete\n");
			return 0;

		}

	}

	printf("[!] unable to bind to address: %s %s\n", host.c_str(), port.c_str());

	NetSocket::InetCleanup();
	return -1;

}

int main(int argc, const char** argv) {

	std::string exec(argv[0]);
	std::vector<std::string> args;
	for (unsigned int i1 = 1; i1 < argc; i1++)
		args.emplace_back(argv[i1]);

	return mainCPP(exec, args);

}
