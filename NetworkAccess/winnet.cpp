
#ifdef PLATFORM_WINDOWS

#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "network.hpp"

bool InetInit() {

	WSADATA wsaData;

	int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (result != 0) {
		printf("WinSock2 startup failed: %d\n", result);
		return false;
	}

	return true;

}

void InetCleanup() {

	WSACleanup();

}

void printerror(const char* format, int error) {
	char *s = NULL;
	FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&s, 0, NULL);
	printf(format, s);
	LocalFree(s);
}

struct SocketImplData {
	SOCKET handle;
	int addrType;
};

struct INetAddrImplData {
	union {
		SOCKADDR sockaddr;
		SOCKADDR_IN sockaddr4;
		SOCKADDR_IN6 sockaddr6;
	} addr;
};


INetAddress::INetAddress() {
	this->implData = new struct INetAddrImplData;
}

INetAddress::~INetAddress() {
	delete this->implData;
}

INetAddress::INetAddress(const INetAddress& other) {
	this->implData = new struct INetAddrImplData;
	memcpy(this->implData, other.implData, sizeof(INetAddrImplData));
}

INetAddress& INetAddress::operator=(const INetAddress& other) {
	if (this == &other)
		return *this;
	memcpy(this->implData, other.implData, sizeof(INetAddrImplData));
	return *this;
}

bool INetAddress::fromstr(string& addressStr, unsigned int port) {
	if (inet_pton(AF_INET, addressStr.c_str(), &this->implData->addr.sockaddr4.sin_addr) == 1) {
		this->implData->addr.sockaddr4.sin_family = AF_INET;
		this->implData->addr.sockaddr4.sin_port = htons(port);
		return true;
	} else if (inet_pton(AF_INET6, addressStr.c_str(), &this->implData->addr.sockaddr6.sin6_addr) == 1) {
		this->implData->addr.sockaddr6.sin6_family = AF_INET6;
		this->implData->addr.sockaddr6.sin6_port = htons(port);
		return true;
	} else {
		printf("inet_pton() failed for AF_INET and AF_INET6!\n");
		return false;
	}
}

bool INetAddress::tostr(string& addressStr, unsigned int* port) const {
	if (this->implData->addr.sockaddr.sa_family == AF_INET) {
		char addrStr[INET_ADDRSTRLEN];
		if (inet_ntop(AF_INET, &this->implData->addr.sockaddr4.sin_addr, addrStr, INET_ADDRSTRLEN) == 0) {
			printerror("inet_ntop() failed: %s\n", WSAGetLastError());
			return false;
		}
		*port = htons(this->implData->addr.sockaddr4.sin_port);
		addressStr = string(addrStr);
		return true;
	} else if (this->implData->addr.sockaddr.sa_family == AF_INET6) {
		char addrStr[INET6_ADDRSTRLEN];
		if (inet_ntop(this->implData->addr.sockaddr6.sin6_family, &this->implData->addr.sockaddr6.sin6_addr, addrStr, INET6_ADDRSTRLEN) == 0) {
			printerror("inet_ntop() failed: %s\n", WSAGetLastError());
			return false;
		}
		*port = htons(this->implData->addr.sockaddr6.sin6_port);
		addressStr = string(addrStr);
		return true;
	} else {
		printf("str_to_inet() with non AF_INET or AF_INET6 address!\n");
		return false;
	}
}

bool resolve_inet(const string& hostStr, const string& portStr, bool lookForUDP, vector<INetAddress>& addresses) {

	struct addrinfo hints {0};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = lookForUDP ? SOCK_DGRAM : SOCK_STREAM;
	hints.ai_protocol = lookForUDP ? IPPROTO_UDP : IPPROTO_TCP;

	struct addrinfo *info = 0, *ptr = 0;
	int result = ::getaddrinfo(hostStr.c_str(), portStr.c_str(), &hints, &info);
	if (result != 0) {
		printerror("getaddrinfo() failed: %s", result);
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

Socket::Socket() {
	this->stype = UNBOUND;
	this->implData = new struct SocketImplData;
	this->implData->handle = INVALID_SOCKET;
}

Socket::~Socket() {
	if (this->stype != UNBOUND) {
		close();
	}
	delete this->implData;
}

bool Socket::listen(const INetAddress& address) {
	if (this->stype != UNBOUND) {
		printf("tried to call listen() on already bound socket!\n");
		return false;
	}

	this->implData->addrType = address.implData->addr.sockaddr.sa_family;
	this->implData->handle = ::socket(address.implData->addr.sockaddr.sa_family, SOCK_STREAM, IPPROTO_TCP);
	if (this->implData->handle == INVALID_SOCKET) {
		printerror("socket() failed: %s\n", WSAGetLastError());
		return false;
	}

	int result = ::bind(this->implData->handle, &address.implData->addr.sockaddr, address.implData->addr.sockaddr.sa_family == AF_INET ? sizeof(SOCKADDR_IN) : sizeof(SOCKADDR_IN6));
	if (result == SOCKET_ERROR) {
		printerror("bind() failed: %s\n", WSAGetLastError());
		::closesocket(this->implData->handle);
		this->implData->handle = INVALID_SOCKET;
		return false;
	}

	result = ::listen(this->implData->handle, SOMAXCONN);
	if (result == SOCKET_ERROR) {
		printerror("listen() failed: %s\n", WSAGetLastError());
		::closesocket(this->implData->handle);
		this->implData->handle = INVALID_SOCKET;
		return false;
	}

	this->stype = LISTEN_TCP;
	return true;
}

bool Socket::bind(INetAddress& address) {
	if (this->stype != UNBOUND) {
		printf("tried to call listen() on already bound socket!\n");
		return false;
	}

	this->implData->addrType = address.implData->addr.sockaddr.sa_family;
	this->implData->handle = ::socket(address.implData->addr.sockaddr.sa_family, SOCK_DGRAM, IPPROTO_UDP);
	if (this->implData->handle == INVALID_SOCKET) {
		printerror("socket() failed: %s\n", WSAGetLastError());
		return false;
	}

	int result = ::bind(this->implData->handle, &address.implData->addr.sockaddr, address.implData->addr.sockaddr.sa_family == AF_INET ? sizeof(SOCKADDR_IN) : sizeof(SOCKADDR_IN6));
	if (result == SOCKET_ERROR) {
		printerror("bind() failed: %s\n", WSAGetLastError());
		::closesocket(this->implData->handle);
		this->implData->handle = INVALID_SOCKET;
		return false;
	}

	this->stype = LISTEN_UDP;
	return true;
}

bool Socket::accept(Socket &socket) {
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
			printerror("accept() failed: %s\n", error);
		return false;
	}

	socket.implData->addrType = this->implData->addrType;
	socket.implData->handle = clientSocket;
	socket.stype = STREAM;
	return true;
}

bool Socket::connect(const INetAddress& address) {
	if (this->stype != UNBOUND) {
		printf("tried to call connect() on already bound socket!\n");
		return false;
	}

	this->implData->handle = ::socket(address.implData->addr.sockaddr.sa_family, SOCK_STREAM, IPPROTO_TCP);
	if (this->implData->handle == INVALID_SOCKET) {
		printerror("socket() failed: %s\n", WSAGetLastError());
		return false;
	}

	int result = ::connect(this->implData->handle, &address.implData->addr.sockaddr, address.implData->addr.sockaddr.sa_family == AF_INET ? sizeof(SOCKADDR_IN) : sizeof(SOCKADDR_IN6));
	if (result == SOCKET_ERROR) {
		printerror("connect() failed: %s\n", WSAGetLastError());
		::closesocket(this->implData->handle);
		this->implData->handle = INVALID_SOCKET;
		return false;
	}

	this->stype = STREAM;
	return true;
}

void Socket::close() {
	if (this->stype == UNBOUND) return;
	::closesocket(this->implData->handle);
	this->implData->handle = INVALID_SOCKET;
	this->stype = UNBOUND;
}

bool Socket::isOpen() {
	return this->stype != UNBOUND && this->implData->handle != INVALID_SOCKET;
}

bool Socket::send(const char* buffer, unsigned int length) {
	if (this->stype == UNBOUND) {
		printf("tried to call send() on unbound socket!\n");
		return false;
	} else if (this->stype != STREAM) {
		printf("tried to call send() on non STREAM socket!\n");
		return false;
	}

	int result = ::send(this->implData->handle, buffer, length, 0);
	if (result == SOCKET_ERROR) {
		printerror("send() failed: %s\n", WSAGetLastError());
		close();
		return false;
	}

	return true;
}

bool Socket::receive(char* buffer, unsigned int length, unsigned int* received) {
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
		if (error != 10004) printerror("recv() failed: %s\n", error);
		close();
		return false;
	} else {
		*received = result;
	}

	// Receiving zero bytes means the connection was closed
	if (result == 0) close();

	return true;
}

bool Socket::receivefrom(INetAddress& address, char* buffer, unsigned int length, unsigned int* received) {
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
		if (error != 10004) printerror("recvfrom() failed: %s\n", error);
		close();
		return false;
	}

	*received = result;
	return true;

}

bool Socket::sendto(const INetAddress& address, const char* buffer, unsigned int length) {
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
		printerror("sendto() failed: %s\n", WSAGetLastError());
		close();
		return false;
	}

	return true;
}

#endif
