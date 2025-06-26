#ifdef PLATFORM_LIN

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include "network.hpp"

bool NetSocket::InetInit() {
	return true;
}

void NetSocket::InetCleanup() {}

struct SocketImplData {

};

struct NetSocket::INetAddrImplData {
	union {
		sockaddr sockaddr;
		sockaddr_in sockaddr4;
		sockaddr_in6 sockaddr6;
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
		printf("inet_pton() failed for AF_INET and AF_INET6!\n");
		return false;
	}
}

bool NetSocket::INetAddress::tostr(std::string& addressStr, unsigned int* port) const {
	if (this->implData->addr.sockaddr.sa_family == AF_INET) {
		char addrStr[INET_ADDRSTRLEN];
		if (inet_ntop(AF_INET, &this->implData->addr.sockaddr4.sin_addr, addrStr, INET_ADDRSTRLEN) == 0) {
			printError("inet_ntop() failed: %s\n", WSAGetLastError());
			return false;
		}
		*port = htons(this->implData->addr.sockaddr4.sin_port);
		addressStr = std::string(addrStr);
		return true;
	} else if (this->implData->addr.sockaddr.sa_family == AF_INET6) {
		char addrStr[INET6_ADDRSTRLEN];
		if (inet_ntop(this->implData->addr.sockaddr6.sin6_family, &this->implData->addr.sockaddr6.sin6_addr, addrStr, INET6_ADDRSTRLEN) == 0) {
			printError("inet_ntop() failed: %s\n", WSAGetLastError());
			return false;
		}
		*port = htons(this->implData->addr.sockaddr6.sin6_port);
		addressStr = std::string(addrStr);
		return true;
	} else {
		printf("str_to_inet() with non AF_INET or AF_INET6 address!\n");
		return false;
	}
}

bool NetSocket::resolveInet(const std::string& hostStr, const std::string& portStr, bool lookForUDP, std::vector<NetSocket::INetAddress>& addresses) {

	struct addrinfo hints {0};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = lookForUDP ? SOCK_DGRAM : SOCK_STREAM;
	hints.ai_protocol = lookForUDP ? IPPROTO_UDP : IPPROTO_TCP;

	struct addrinfo *info = 0, *ptr = 0;
	int result = ::getaddrinfo(hostStr.c_str(), portStr.c_str(), &hints, &info);
	if (result != 0) {
		printError("getaddrinfo() failed: %s", result);
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

//int main(int argc, char *argv[])
//{
//     int sockfd, newsockfd, portno;
//     socklen_t clilen;
//     char buffer[256];
//     struct sockaddr_in serv_addr, cli_addr;
//     int n;
//     if (argc < 2) {
//         fprintf(stderr,"ERROR, no port provided\n");
//         exit(1);
//     }
//     sockfd = socket(AF_INET, SOCK_STREAM, 0);
//     if (sockfd < 0)
//        error("ERROR opening socket");
//     bzero((char *) &serv_addr, sizeof(serv_addr));
//     portno = atoi(argv[1]);
//     serv_addr.sin_family = AF_INET;
//     serv_addr.sin_addr.s_addr = INADDR_ANY;
//     serv_addr.sin_port = htons(portno);
//     if (bind(sockfd, (struct sockaddr *) &serv_addr,
//              sizeof(serv_addr)) < 0)
//              error("ERROR on binding");
//     listen(sockfd,5);
//     clilen = sizeof(cli_addr);
//     newsockfd = accept(sockfd,
//                 (struct sockaddr *) &cli_addr,
//                 &clilen);
//     if (newsockfd < 0)
//          error("ERROR on accept");
//     bzero(buffer,256);
//     n = read(newsockfd,buffer,255);
//     if (n < 0) error("ERROR reading from socket");
//     printf("Here is the message: %s\n",buffer);
//     n = write(newsockfd,"I got your message",18);
//     if (n < 0) error("ERROR writing to socket");
//     close(newsockfd);
//     close(sockfd);
//     return 0;
//}

bool NetSocket::Socket::listen(unsigned int port) {
	if (this->stype != UNBOUND) {
		printf("tried to call listen() on already bound socket!\n");
		return false;
	}

	// https://www.linuxhowtos.org/C_C++/socket.htm

	socket(AF_INET, SOCK_STREAM, 0);



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

bool NetSocket::Socket::accept(Socket &socket) {
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

bool NetSocket::Socket::connect(const char* host, unsigned int port) {
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

bool NetSocket::Socket::receive(char* buffer, unsigned int length, unsigned int* received) {
	if (this->stype == LISTEN) {
		printf("tried to call send() on LISTEN socket!\n");
		return false;
	} else if (this->stype == UNBOUND) {
		printf("tried to call send() on unbound socket!\n");
		return false;
	}

	int result = ::recv(this->implData->handle, buffer, length, 0);
	if (result < 0) {
		printError("recv() failed: %s\n", WSAGetLastError());
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
