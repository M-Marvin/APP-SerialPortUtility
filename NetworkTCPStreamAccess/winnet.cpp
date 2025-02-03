#ifdef PLATFORM_WINDOWS

#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "network.h"

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
};

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

bool Socket::listen(unsigned int port) {
	if (this->stype != UNBOUND) {
		printf("tried to call listen() on already bound socket!\n");
		return false;
	}

	struct addrinfo *adrinf = NULL, *ptr = NULL, hints;
	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	char portStr[8] = {0};
	itoa(port, portStr, 10);

	int result = ::getaddrinfo(NULL, portStr, &hints, &adrinf);
	if (result != 0) {
		printerror("getadrinfo failed: %s\n", result);
		::freeaddrinfo(adrinf);
		return false;
	}

	do {
		if (ptr == NULL) {
			ptr = adrinf;
		} else {
			ptr = ptr->ai_next;
			if (ptr == NULL) break;
		}

		this->implData->handle = ::socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
		if (this->implData->handle == INVALID_SOCKET) {
			printerror("socket() failed: %s\n", WSAGetLastError());
			continue;
		}

		result = ::bind(this->implData->handle, ptr->ai_addr, ptr->ai_addrlen);
		if (result == SOCKET_ERROR) {
			printerror("bind() failed: %s\n", WSAGetLastError());
			::closesocket(this->implData->handle);
			this->implData->handle = INVALID_SOCKET;
			continue;
		}

		result = ::listen(this->implData->handle, SOMAXCONN);
		if (result == SOCKET_ERROR) {
			printerror("listen() failed: %s\n", WSAGetLastError());
			::closesocket(this->implData->handle);
			this->implData->handle = INVALID_SOCKET;
			continue;
		}

		::freeaddrinfo(adrinf);

		this->stype = LISTEN;
		return true;

	} while (true);

	printf("failed to create listen socket!\n");
	::freeaddrinfo(adrinf);
	return false;
}

bool Socket::accept(Socket &socket) {
	if (this->stype != LISTEN) {
		printf("tried to call accept() on non LISTEN socket!\n");
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

	socket.implData->handle = clientSocket;
	socket.stype = STREAM;
	return true;
}

bool Socket::connect(const char* host, unsigned int port) {
	if (this->stype != UNBOUND) {
		printf("tried to call connect() on already bound socket!\n");
		return false;
	}

	struct addrinfo *adrinf = NULL, *ptr = NULL, hints;
	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	char portStr[8] = {0};
	itoa(port, portStr, 10);

	int result = ::getaddrinfo(host, portStr, &hints, &adrinf);
	if (result != 0) {
		printerror("getadrinfo failed: %s\n", result);
		::freeaddrinfo(adrinf);
		return false;
	}

	do {
		if (ptr == NULL) {
			ptr = adrinf;
		} else {
			ptr = ptr->ai_next;
			if (ptr == NULL) break;
		}

		this->implData->handle = ::socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
		if (this->implData->handle == INVALID_SOCKET) {
			printerror("socket() failed: %s\n", WSAGetLastError());
			continue;
		}

		result = ::bind(this->implData->handle, ptr->ai_addr, ptr->ai_addrlen);
		if (result == SOCKET_ERROR) {
			printerror("bind() failed: %s\n", WSAGetLastError());
			::closesocket(this->implData->handle);
			this->implData->handle = INVALID_SOCKET;
			continue;
		}

		result = ::connect(this->implData->handle, ptr->ai_addr, ptr->ai_addrlen);
		if (result == SOCKET_ERROR) {
			printerror("connect() failed: %s\n", WSAGetLastError());
			::closesocket(this->implData->handle);
			this->implData->handle = INVALID_SOCKET;
			continue;
		}

		::freeaddrinfo(adrinf);

		this->stype = STREAM;
		return true;

	} while (true);

	printf("failed to connect socket!\n");
	::freeaddrinfo(adrinf);
	return false;
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
	if (this->stype == LISTEN) {
		printf("tried to call send() on LISTEN socket!\n");
		return false;
	} else if (this->stype == UNBOUND) {
		printf("tried to call send() on unbound socket!\n");
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
	if (this->stype == LISTEN) {
		printf("tried to call send() on LISTEN socket!\n");
		return false;
	} else if (this->stype == UNBOUND) {
		printf("tried to call send() on unbound socket!\n");
		return false;
	}

	int result = ::recv(this->implData->handle, buffer, length, 0);
	if (result < 0) {
		printerror("recv() failed: %s\n", WSAGetLastError());
		close();
		return false;
	} else {
		*received = result;
	}

	// Receiving zero bytes means the connection was closed
	if (result == 0) close();

	return true;
}

#endif
