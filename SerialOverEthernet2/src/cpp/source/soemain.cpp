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
#include <vector>
#include <algorithm>
#include "soeimpl.hpp"
#include "dbgprintf.h"

#ifndef BUILD_VERSION
#define BUILD_VERSION N/A
#endif
// neccessary because of an weird toolchain bug not allowing quotes in -D flags
#define STRINGIZE(x) #x
#define ASSTRING(x) STRINGIZE(x)

static std::mutex m_clientConnections;
static std::condition_variable cv_clientConnections;
static std::vector<SerialOverEthernet::SOESocketHandler*> clientConnections;

void cleanupDeadConnectionHandlers() {
	std::unique_lock<std::mutex> lock(m_clientConnections);
	clientConnections.erase(std::remove_if(clientConnections.begin(), clientConnections.end(), [](SerialOverEthernet::SOESocketHandler* managedHandler){
		if (!managedHandler->isAlive()) {
			delete managedHandler;
			return true;
		}
		return false;
	}), clientConnections.end());
	lock.release();
	cv_clientConnections.notify_one();
}

SerialOverEthernet::SOESocketHandler* createConnectionHandler(NetSocket::Socket* unmanagedSocket) {
	std::unique_lock<std::mutex> lock(m_clientConnections);
	SerialOverEthernet::SOESocketHandler* managedHandler = new SerialOverEthernet::SOESocketHandler(unmanagedSocket, [](SerialOverEthernet::SOESocketHandler* managedHandler) {
		cv_clientConnections.notify_one(); // try to run the cleanup of closed handlers if not in server mode
	});
	clientConnections.push_back(managedHandler);
	return managedHandler;
}

bool linkRemotePort(std::string& remoteHost, std::string& remotePort, std::string& remoteSerial, std::string& localSerial, SerialAccess::SerialPortConfiguration& remoteConfig, SerialAccess::SerialPortConfiguration& localConfig) {
	std::vector<NetSocket::INetAddress> addresses;
	NetSocket::resolveInet(remoteHost, remotePort, true, addresses);
	NetSocket::Socket* clientSocket = NetSocket::newSocket();

	printf("[i] establishing link: %s <-> %s @ %s %s\n", localSerial.c_str(), remoteSerial.c_str(), remoteHost.c_str(), remotePort.c_str());

	for (auto address : addresses) {

		// try to connect to server
		if (!clientSocket->connect(address))
			continue;

		// create connection handler, try to apply configurations
		SerialOverEthernet::SOESocketHandler* handler = createConnectionHandler(clientSocket);
		if (!handler->openRemotePort(remoteSerial)) {
			printf("[!] failed to open remote port: %s\n", remoteSerial.c_str());
			handler->shutdown();
			return false;
		}
		if (!handler->setRemoteConfig(remoteConfig)) {
			printf("[!] failed to configure remote port: %s\n", remoteSerial.c_str());
			handler->shutdown();
			return false;
		}
		if (!handler->openLocalPort(localSerial)) {
			printf("[!] failed to open local port: %s\n", localSerial.c_str());
			handler->shutdown();
			return false;
		}
		if (!handler->setLocalConfig(localConfig)) {
			printf("[!] failed to configure local port: %s\n", remoteSerial.c_str());
			handler->shutdown();
			return false;
		}

		printf("[i] link established: %s %s <-> %s\n", remoteHost.c_str(), remoteSerial.c_str(), localSerial.c_str());
		return true;

	}
	printf("[i] unable to established link: %s %s <-> %s\n", remoteHost.c_str(), remoteSerial.c_str(), localSerial.c_str());
	return false;
}

void interpretFlags(const std::vector<std::string>& args) {

	// parse arguments for connections
	std::string remoteHost;
	std::string remotePort = std::to_string(DEFAULT_SOE_PORT);
	std::string remoteSerial;
	std::string localSerial;
	SerialAccess::SerialPortConfiguration remoteConfig = SerialAccess::DEFAULT_PORT_CONFIGURATION;
	bool link = false;

	for (auto flag = args.begin(); flag != args.end(); flag++) {

		// complete last link call before processing other options
		if (*flag == "-link" || *flag == "-unlink") {
			if (link) {
				if (remoteHost.empty() || remotePort.empty() || remoteSerial.empty() || localSerial.empty()) {
					printf("[!] not enough arguments for connection\n");
					continue;
				}
				link = false;

				linkRemotePort(remoteHost, remotePort, remoteSerial, localSerial, remoteConfig, remoteConfig); // TODO
			}
		}

		if (flag + 1 != args.end()) {
			// flags with argument
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
//				flag++; TODO
//				if (!handler.closeLocalPort(*flag)) {
//					printf("[!] unable to close port: %s\n", flag->c_str());
//				} else {
//					printf("[i] link closed: %s\n", flag->c_str());
//				}
			}
		}
		// flags without arguments

		if (*flag == "-link") {
			link = true;
		}// else if (*flag == "-close") { TODO
//			if (!handler.closeAllPorts()) {
//				printf("[!] some ports failed to close and might still be open\n");
//			} else {
//				printf("[i] all links closed\n");
//			}
//		} else if (*flag == "-list") {
//			handler.listAllPorts();
//		}
	}

	if (link) {
		if (remoteHost.empty() || remotePort.empty() || remoteSerial.empty() || localSerial.empty()) {
			printf("[!] not enough arguments for connection\n");
			return;
		}

		linkRemotePort(remoteHost, remotePort, remoteSerial, localSerial, remoteConfig, remoteConfig); // TODO
	}
}

int mainCPP(std::string& exec, std::vector<std::string>& args) {

	// disable output caching
	setbuf(stdout, NULL);
	dbgprintf("[DBG] test dbg output enabled\n");

	// print help when no arguments supplied
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
		printf("serial over ethernet version: " ASSTRING(BUILD_VERSION) "\n");
		return 1;
	}

	// default configuration
	std::string port = std::to_string(DEFAULT_SOE_PORT);
	std::string host = "";

	// parse arguments for network connection
	auto flag = args.begin();
	for (; flag != args.end(); flag++) {
		if (flag + 1 != args.end()) {
			// flags with argument
			if (*flag == "-addr") {
				host = *++flag;
			} else if (*flag == "-port") {
				port = *++flag;
			}
		}
		// flags without arguments
		if (*flag == "-link" || *flag == "-unlink" || *flag == "-close" || *flag == "-list" || *flag == "-exit") {
			break; // end of local network arguments
		}
	}
	if (flag != args.begin())
		args.erase(args.begin(), flag - 1);

	// initialize networking
	if (!NetSocket::InetInit()) {
		printf("[!] failed to initialize network!\n");
		return -1;
	}

	// parse additional flags, triggering client connection handshakes and setup
	interpretFlags(args);

	// If no host address supplied, only wait for client connections to terminate
	if (host.empty()) {
		std::unique_lock<std::mutex> lock(m_clientConnections);
		cv_clientConnections.wait(lock, [](){
			// this has to run every now and then to get rid of closed connection handlers
			cleanupDeadConnectionHandlers();

			return clientConnections.size() == 0;
		});
	} else {

		// resolve supplied host string
		std::vector<NetSocket::INetAddress> localAddresses;
		NetSocket::resolveInet(host, port, true, localAddresses);

		// attempt to bind to first available local host address
		NetSocket::Socket* serverSocket = NetSocket::newSocket();
		for (NetSocket::INetAddress& address : localAddresses) {

			std::string localAddress;
			unsigned int localPort;
			address.tostr(localAddress, &localPort);
			printf("[i] serial over ethernet/IP, attempt claim address: %s %d\n", localAddress.c_str(), localPort);

			if (serverSocket->listen(address)) {

				printf("[i] serial over ethernet/IP, open server port on: %s %d\n", localAddress.c_str(), localPort);

				while (serverSocket->isOpen()) {
					// this has to run every now and then to get rid of closed connection handlers
					cleanupDeadConnectionHandlers();

					NetSocket::Socket* clientSocket = NetSocket::newSocket();
					if (serverSocket->accept(*clientSocket)) {

						printf("[i] incomming connection request ...\n");

						// create handler for connection and make new socket for next request
						createConnectionHandler(clientSocket);
						continue;

					}
					delete clientSocket;
				}

				goto end_listen;

			}

		}
		printf("[!] unable to bind to address: %s %s\n", host.c_str(), port.c_str());
	end_listen:
		printf("[i] server socket closed, no more connection accepted\n");
		delete serverSocket;

	}

	// cleanup network and exit
	NetSocket::InetCleanup();
	printf("[i] client shutdown complete\n");
	return 0;

}

int main(int argc, const char** argv) {

	std::string exec(argv[0]);
	std::vector<std::string> args;
	for (unsigned int i1 = 1; i1 < argc; i1++)
		args.emplace_back(argv[i1]);

	return mainCPP(exec, args);

}
