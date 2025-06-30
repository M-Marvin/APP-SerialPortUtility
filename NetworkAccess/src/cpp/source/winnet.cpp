
#ifdef PLATFORM_WIN

#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "network.hpp"

bool NetSocket::InetInit() {

	WSADATA wsaData;

	int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (result != 0) {
		printf("WinSock2 startup failed: %d\n", result);
		return false;
	}

	return true;

}

void NetSocket::InetCleanup() {

	WSACleanup();

}

void printError(const char* format) {
	DWORD errorCode = GetLastError();
	if (errorCode == 0) return;
	LPSTR msg;
	if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&msg, 0, NULL) > 0) {
		printf(format, errorCode, msg);
		LocalFree(msg);
	}
}

struct NetSocket::SocketImplData {
	SOCKET handle;
	int addrType;
};

struct NetSocket::INetAddrImplData {
	union {
		SOCKADDR sockaddr;
		SOCKADDR_IN sockaddr4;
		SOCKADDR_IN6 sockaddr6;
	} addr;
};


NetSocket::INetAddress::INetAddress() {
	this->implData = new struct NetSocket::INetAddrImplData;
}

NetSocket::INetAddress::~INetAddress() {
	delete this->implData;
}

NetSocket::INetAddress::INetAddress(const NetSocket::INetAddress& other) {
	this->implData = new struct NetSocket::INetAddrImplData;
	memcpy(this->implData, other.implData, sizeof(NetSocket::INetAddrImplData));
}

NetSocket::INetAddress& NetSocket::INetAddress::operator=(const NetSocket::INetAddress& other) {
	if (this == &other)
		return *this;
	memcpy(this->implData, other.implData, sizeof(NetSocket::INetAddrImplData));
	return *this;
}

int compare(const NetSocket::INetAddress& address1, const NetSocket::INetAddress& address2) {
	int i = (int) address1.implData->addr.sockaddr.sa_family - (int) address2.implData->addr.sockaddr.sa_family;
	if (i != 0) {
		return i;
	} else if (address1.implData->addr.sockaddr.sa_family == AF_INET6) {
		return memcmp(&address1.implData->addr.sockaddr6, &address2.implData->addr.sockaddr6, sizeof(SOCKADDR_IN6));
	} else {
		return memcmp(&address1.implData->addr.sockaddr4, &address2.implData->addr.sockaddr4, sizeof(SOCKADDR_IN));
	}
}

bool NetSocket::INetAddress::operator<(const NetSocket::INetAddress& other) const {
	return compare(*this, other) < 0;
}

bool NetSocket::INetAddress::operator>(const NetSocket::INetAddress& other) const {
	return compare(*this, other) > 0;
}

bool NetSocket::INetAddress::operator==(const NetSocket::INetAddress& other) const {
	return compare(*this, other) == 0;
}

bool NetSocket::INetAddress::fromstr(std::string& addressStr, unsigned int port) {
	if (inet_pton(AF_INET, addressStr.c_str(), &this->implData->addr.sockaddr4.sin_addr) == 1) {
		this->implData->addr.sockaddr4.sin_family = AF_INET;
		this->implData->addr.sockaddr4.sin_port = htons(port);
		return true;
	} else if (inet_pton(AF_INET6, addressStr.c_str(), &this->implData->addr.sockaddr6.sin6_addr) == 1) {
		this->implData->addr.sockaddr6.sin6_family = AF_INET6;
		this->implData->addr.sockaddr6.sin6_port = htons(port);
		return true;
	} else {
		printf("INetAddress:fromstr:inet_pton() failed for AF_INET and AF_INET6!\n");
		return false;
	}
}

bool NetSocket::INetAddress::tostr(std::string& addressStr, unsigned int* port) const {
	if (this->implData->addr.sockaddr.sa_family == AF_INET) {
		char addrStr[INET_ADDRSTRLEN];
		if (inet_ntop(AF_INET, &this->implData->addr.sockaddr4.sin_addr, addrStr, INET_ADDRSTRLEN) == 0) {
			printError("Error %lu in INetAddress:tostr:inet_ntop(): %s\n");
			return false;
		}
		*port = htons(this->implData->addr.sockaddr4.sin_port);
		addressStr = std::string(addrStr);
		return true;
	} else if (this->implData->addr.sockaddr.sa_family == AF_INET6) {
		char addrStr[INET6_ADDRSTRLEN];
		if (inet_ntop(this->implData->addr.sockaddr6.sin6_family, &this->implData->addr.sockaddr6.sin6_addr, addrStr, INET6_ADDRSTRLEN) == 0) {
			printError("Error %lu in INetAddress:tostr:inet_ntop(): %s\n");
			return false;
		}
		*port = htons(this->implData->addr.sockaddr6.sin6_port);
		addressStr = std::string(addrStr);
		return true;
	} else {
		printf("INetAddress:tostr:str_to_inet() with non AF_INET or AF_INET6 address!\n");
		return false;
	}
}

bool NetSocket::resolveInet(const std::string& hostStr, const std::string& portStr, bool lookForUDP, std::vector<NetSocket::INetAddress>& addresses) {

	struct addrinfo hints {0};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = lookForUDP ? SOCK_DGRAM : SOCK_STREAM;
	hints.ai_protocol = lookForUDP ? IPPROTO_UDP : IPPROTO_TCP;

	struct addrinfo *info = 0, *ptr = 0;
	if (::getaddrinfo(hostStr.c_str(), portStr.c_str(), &hints, &info) != 0) {
		printError("Error %lu in Socket:resolveInet:getaddrinfo(): %s\n");
		return false;
	}

	for (ptr = info; ptr != 0; ptr = ptr->ai_next) {
		addresses.emplace_back();
		if (ptr->ai_family == AF_INET6) {
			addresses.back().implData->addr.sockaddr6 = *((SOCKADDR_IN6*) ptr->ai_addr);
		} else if (ptr->ai_family == AF_INET) {
			addresses.back().implData->addr.sockaddr4 = *((SOCKADDR_IN*) ptr->ai_addr);
		}
	}

	::freeaddrinfo(info);
	return true;
}

NetSocket::Socket::Socket() {
	this->stype = UNBOUND;
	this->implData = new struct SocketImplData;
	this->implData->handle = INVALID_SOCKET;
}

NetSocket::Socket::~Socket() {
	if (this->stype != UNBOUND) {
		close();
	}
	delete this->implData;
}

bool NetSocket::Socket::listen(const NetSocket::INetAddress& address) {
	if (this->stype != UNBOUND) {
		printf("tried to call listen() on already bound socket!\n");
		return false;
	}

	this->implData->addrType = address.implData->addr.sockaddr.sa_family;
	this->implData->handle = ::socket(address.implData->addr.sockaddr.sa_family, SOCK_STREAM, IPPROTO_TCP);
	if (this->implData->handle == INVALID_SOCKET) {
		printError("Error %lu in Socket:listen:socket(): %s\n");
		return false;
	}

	int result = ::bind(this->implData->handle, &address.implData->addr.sockaddr, address.implData->addr.sockaddr.sa_family == AF_INET ? sizeof(SOCKADDR_IN) : sizeof(SOCKADDR_IN6));
	if (result == SOCKET_ERROR) {
		printError("Error %lu in Socket:listen:bind(): %s\n");
		::closesocket(this->implData->handle);
		this->implData->handle = INVALID_SOCKET;
		return false;
	}

	result = ::listen(this->implData->handle, SOMAXCONN);
	if (result == SOCKET_ERROR) {
		printError("Error %lu in Socket:listen:listen(): %s\n");
		::closesocket(this->implData->handle);
		this->implData->handle = INVALID_SOCKET;
		return false;
	}

	this->stype = LISTEN_TCP;
	return true;
}

bool NetSocket::Socket::bind(NetSocket::INetAddress& address) {
	if (this->stype != UNBOUND) {
		printf("tried to call listen() on already bound socket!\n");
		return false;
	}

	this->implData->addrType = address.implData->addr.sockaddr.sa_family;
	this->implData->handle = ::socket(address.implData->addr.sockaddr.sa_family, SOCK_DGRAM, IPPROTO_UDP);
	if (this->implData->handle == INVALID_SOCKET) {
		printError("Error %lu in Socket:bind:socket(): %s\n");
		return false;
	}

	if (::bind(this->implData->handle, &address.implData->addr.sockaddr, address.implData->addr.sockaddr.sa_family == AF_INET ? sizeof(SOCKADDR_IN) : sizeof(SOCKADDR_IN6)) == SOCKET_ERROR) {
		printError("Error %lu in Socket:bind:bind(): %s\n");
		::closesocket(this->implData->handle);
		this->implData->handle = INVALID_SOCKET;
		return false;
	}

	this->stype = LISTEN_UDP;
	return true;
}

bool NetSocket::Socket::accept(Socket &socket) {
	if (this->stype != LISTEN_TCP) {
		printf("tried to call accept() on non LISTEN_TCP socket!\n");
		return false;
	}
	if (socket.stype != UNBOUND) {
		printf("tried to call accept() with already bound socket!\n");
		return false;
	}

	SOCKET clientSocket = ::accept(this->implData->handle, NULL, NULL);
	if (clientSocket == INVALID_SOCKET) {
		int error = WSAGetLastError();
		if (error != 10004) // Ignore the error message for "socket was closed" since this means it was intentional
			printError("Error %lu in Socket:accept:accept(): %s\n");
		return false;
	}

	socket.implData->addrType = this->implData->addrType;
	socket.implData->handle = clientSocket;
	socket.stype = STREAM;
	return true;
}

bool NetSocket::Socket::connect(const NetSocket::INetAddress& address) {
	if (this->stype != UNBOUND) {
		printf("tried to call connect() on already bound socket!\n");
		return false;
	}

	this->implData->handle = ::socket(address.implData->addr.sockaddr.sa_family, SOCK_STREAM, IPPROTO_TCP);
	if (this->implData->handle == INVALID_SOCKET) {
		printError("Error %lu in Socket:connect:socket(): %s\n");
		return false;
	}

	int result = ::connect(this->implData->handle, &address.implData->addr.sockaddr, address.implData->addr.sockaddr.sa_family == AF_INET ? sizeof(SOCKADDR_IN) : sizeof(SOCKADDR_IN6));
	if (result == SOCKET_ERROR) {
		printError("Error %lu in Socket:connect:connect(): %s\n");
		::closesocket(this->implData->handle);
		this->implData->handle = INVALID_SOCKET;
		return false;
	}

	this->stype = STREAM;
	return true;
}

void NetSocket::Socket::close() {
	if (this->stype == UNBOUND) return;
	::closesocket(this->implData->handle);
	this->implData->handle = INVALID_SOCKET;
	this->stype = UNBOUND;
}

bool NetSocket::Socket::isOpen() {
	return this->stype != UNBOUND && this->implData->handle != INVALID_SOCKET;
}

bool NetSocket::Socket::send(const char* buffer, unsigned int length) {
	if (this->stype == UNBOUND) {
		printf("tried to call send() on unbound socket!\n");
		return false;
	} else if (this->stype != STREAM) {
		printf("tried to call send() on non STREAM socket!\n");
		return false;
	}

	int result = ::send(this->implData->handle, buffer, length, 0);
	if (result == SOCKET_ERROR) {
		printError("Error %lu in Socket:send:send(): %s\n");
		close();
		return false;
	}

	return true;
}

bool NetSocket::Socket::receive(char* buffer, unsigned int length, unsigned int* received) {
	if (this->stype == UNBOUND) {
		printf("tried to call send() on unbound socket!\n");
		return false;
	} else if (this->stype != STREAM) {
		printf("tried to call send() on non STREAM socket!\n");
		return false;
	}

	int result = ::recv(this->implData->handle, buffer, length, 0);
	if (result < 0) {
		int error = WSAGetLastError();
		if (error != 10004)
			printError("Error %lu in Socket:receive:recv(): %s\n");
		close();
		return false;
	} else {
		*received = result;
	}

	// Receiving zero bytes means the connection was closed
	if (result == 0) close();

	return true;
}

bool NetSocket::Socket::receivefrom(NetSocket::INetAddress& address, char* buffer, unsigned int length, unsigned int* received) {
	if (this->stype == UNBOUND) {
		printf("tried to call receivefrom() on unbound socket!\n");
		return false;
	} else if (this->stype != LISTEN_UDP) {
		printf("tried to call receivefrom() on non LISTEN_UDP socket!\n");
		return false;
	}

	int senderAdressLen = sizeof(SOCKADDR_IN6);
	int result = ::recvfrom(this->implData->handle, buffer, length, 0, &address.implData->addr.sockaddr, &senderAdressLen);
	if (result == SOCKET_ERROR) {
		int error = WSAGetLastError();
		if (error != 10004)
			printError("Error %lu in Socket:receivefrom:recvfrom(): %s\n");
		close();
		return false;
	}

	*received = result;
	return true;

}

bool NetSocket::Socket::sendto(const NetSocket::INetAddress& address, const char* buffer, unsigned int length) {
	if (this->stype == UNBOUND) {
		printf("tried to call sendto() on unbound socket!\n");
		return false;
	} else if (this->stype != LISTEN_UDP) {
		printf("tried to call sendto() on non LISTEN_UDP socket!\n");
		return false;
	}

	if (address.implData->addr.sockaddr.sa_family != this->implData->addrType) {
		printf("tried to call receivefrom() with invalid address type for this socket!\n");
		return false;
	}

	int result = ::sendto(this->implData->handle, buffer, length, 0, &address.implData->addr.sockaddr, address.implData->addr.sockaddr.sa_family == AF_INET ? sizeof(SOCKADDR_IN) : sizeof(SOCKADDR_IN6));
	if (result == SOCKET_ERROR) {
		printError("Error %lu in Socket:sendto:sendto(): %s\n");
		close();
		return false;
	}

	return true;
}

#endif
