/*
 * network.h
 *
 *  Created on: 03.02.2025
 *      Author: marvi
 */

#ifndef NETWORK_H_
#define NETWORK_H_

struct SocketImplData;

enum SocketType {
	UNBOUND = 0,
	LISTEN = 1,
	STREAM = 2
};

class Socket {

public:
	Socket();
	~Socket();

	bool listen(unsigned int port);
	bool accept(Socket &clientSocket);
	bool connect(const char* host, unsigned int port);
	void close();
	bool isOpen();

	SocketType type() {
		return this->stype;
	}

	bool send(const char* buffer, unsigned int length);
	bool receive(char* buffer, unsigned int length, unsigned int* received);

private:
	SocketType stype;
	SocketImplData* implData;

};

bool InetInit();
void InetCleanup();

#endif /* NETWORK_H_ */
