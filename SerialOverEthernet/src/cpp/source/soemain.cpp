/*
 * soemain.cpp
 *
 * Main Serial over Ethernet/IP process handling.
 * Implements server connection acceptor and client connection requests.
 *
 *  Created on: 22.02.2025
 *      Author: Marvin Koehler (M_Marvin)
 */

#include <iostream>
#include <algorithm>
#include "soemain.hpp"
#include "dbgprintf.h"

static std::mutex m_clientConnections;
static std::condition_variable cv_clientConnections;
static std::vector<SerialOverEthernet::SOELinkHandler*> clientConnections;

void cleanupDeadConnectionHandlers() {
	std::lock_guard<std::mutex> lock(m_clientConnections);
	clientConnections.erase(std::remove_if(clientConnections.begin(), clientConnections.end(), [](SerialOverEthernet::SOELinkHandler* managedHandler){
		if (!managedHandler->isAlive()) {
			delete managedHandler;
			return true;
		}
		return false;
	}), clientConnections.end());
}

SerialOverEthernet::SOELinkHandler* createConnectionHandler(NetSocket::Socket* unmanagedSocket, std::string socketHostName, std::string socketHostPort, bool virtualMode) {
	std::lock_guard<std::mutex> lock(m_clientConnections);
	dbgprintf("[DBG] create handler for: %s/%s\n", socketHostName.c_str(), socketHostPort.c_str());
	SerialOverEthernet::SOELinkHandler* managedHandler;
	if (virtualMode) {
#ifdef PLATFORM_WIN
		managedHandler = new SerialOverEthernet::SOELinkHandlerVCOM(unmanagedSocket, socketHostName, socketHostPort, [](SerialOverEthernet::SOELinkHandler* managedHandler) {
			cv_clientConnections.notify_one(); // try to run the cleanup of closed handlers if not in server mode
		});
#else
		printf("[!] VIRTUAL PORT MODE NOT YET SUPPORTED ON PLATFORMS OTHER THAN WINDOWS\n");
		managedHandler = new SerialOverEthernet::SOELinkHandlerCOM(unmanagedSocket, socketHostName, socketHostPort, [](SerialOverEthernet::SOELinkHandler* managedHandler) {
			cv_clientConnections.notify_one(); // try to run the cleanup of closed handlers if not in server mode
		});
#endif
	} else {
		managedHandler = new SerialOverEthernet::SOELinkHandlerCOM(unmanagedSocket, socketHostName, socketHostPort, [](SerialOverEthernet::SOELinkHandler* managedHandler) {
			cv_clientConnections.notify_one(); // try to run the cleanup of closed handlers if not in server mode
		});
	}
	clientConnections.push_back(managedHandler);
	return managedHandler;
}

bool linkRemotePort(std::string& remoteHost, std::string& remotePort, std::string& remoteSerial, std::string& localSerial, SerialAccess::SerialPortConfiguration& remoteConfig, SerialAccess::SerialPortConfiguration& localConfig, bool virtualMode) {
	std::vector<NetSocket::INetAddress> addresses;
	NetSocket::resolveInet(remoteHost, remotePort, true, addresses);
	NetSocket::Socket* clientSocket = NetSocket::newSocket();

	printf("[i] establishing link: %s <-> %s @ %s/%s\n", localSerial.c_str(), remoteSerial.c_str(), remoteHost.c_str(), remotePort.c_str());

	for (auto address : addresses) {

		std::string serverHostName;
		unsigned int serverHostPort;
		address.tostr(serverHostName, &serverHostPort);
		std::string serverHostPortStr = std::to_string(serverHostPort);

		printf("[i] serial over ethernet/IP, attempt connection on: %s/%d\n", serverHostName.c_str(), serverHostPort);

		// try to connect to server
		if (!clientSocket->connect(address, SOE_TCP_HANDSHAKE_TIMEOUT))
			continue;

		dbgprintf("[DBG] connect succeded at: %s/%s\n", serverHostName.c_str(), serverHostPortStr.c_str());

		// create connection handler, try to apply configurations
		SerialOverEthernet::SOELinkHandler* handler = createConnectionHandler(clientSocket, serverHostName, serverHostPortStr, virtualMode);
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
			printf("[!] failed to configure local port: %s\n", localSerial.c_str());
			handler->shutdown();
			return false;
		}

		printf("[i] link established: %s <-> %s @ %s/%s (%s/%s)\n", localSerial.c_str(), remoteSerial.c_str(), remoteHost.c_str(), remotePort.c_str(), serverHostName.c_str(), serverHostPortStr.c_str());
		return true;

	}
	clientSocket->close();
	printf("[i] unable to established link: %s <-> %s @ %s/%s\n", localSerial.c_str(), remoteSerial.c_str(), remoteHost.c_str(), remotePort.c_str());
	return false;
}

int runMain(std::string& serverHostName, std::string& serverHostPort, std::vector<std::string>& linkArgs) {
	
	// initialize networking
	if (!NetSocket::InetInit()) {
		printf("[!] failed to initialize network!\n");
		return -1;
	}

	// parse additional link flags, triggering client connection handshakes and setup
	interpretFlags(linkArgs);

	// If no host address supplied, only wait for client connections to terminate
	if (serverHostName.empty()) {
		std::unique_lock<std::mutex> lock(m_clientConnections);
		cv_clientConnections.wait(lock, [&lock](){
			// this has to run every now and then to get rid of closed connection handlers
			lock.unlock();
			cleanupDeadConnectionHandlers();
			lock.lock();

			return clientConnections.size() == 0;
		});
	} else {

		// resolve supplied host string
		std::vector<NetSocket::INetAddress> localAddresses;
		NetSocket::resolveInet(serverHostName, serverHostPort, true, localAddresses);

		// attempt to bind to first available local host address
		NetSocket::Socket* serverSocket = NetSocket::newSocket();
		for (NetSocket::INetAddress& address : localAddresses) {

			std::string localAddress;
			unsigned int localPort;
			address.tostr(localAddress, &localPort);
			printf("[i] serial over ethernet/IP, attempt claim address: %s/%d\n", localAddress.c_str(), localPort);

			if (serverSocket->listen(address)) {

				printf("[i] serial over ethernet/IP, open server port on: %s/%d\n", localAddress.c_str(), localPort);

				while (serverSocket->isOpen()) {
					// this has to run every now and then to get rid of closed connection handlers
					cleanupDeadConnectionHandlers();

					NetSocket::Socket* clientSocket = NetSocket::newSocket();
					if (serverSocket->accept(*clientSocket)) {

						std::string clientHostName = "N/A";
						std::string clientHostPort = "N/A";
						NetSocket::INetAddress address;
						if (clientSocket->getINet(address)) {
							unsigned int clientHostPortNr = 0;
							if (address.tostr(clientHostName, &clientHostPortNr)) {
								clientHostPort = std::to_string(clientHostPortNr);
							}
						}

						printf("[i] incomming connection request: %s/%s\n", clientHostName.c_str(), clientHostPort.c_str());

						// create handler for connection and make new socket for next request
						createConnectionHandler(clientSocket, clientHostName, clientHostPort, false);
						continue;

					}
					delete clientSocket;
				}

				goto end_listen;

			}

		}
		printf("[!] unable to bind to address: %s/%s\n", serverHostName.c_str(), serverHostPort.c_str());
	end_listen:
		printf("[i] server socket closed, no more connections accepted\n");
		delete serverSocket;

	}

	// cleanup network and exit
	NetSocket::InetCleanup();
	printf("[i] client shutdown complete\n");
	return 0;

}


