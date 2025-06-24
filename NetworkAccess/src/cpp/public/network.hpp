/*
 * network.h
 *
 *  Created on: 03.02.2025
 *      Author: marvi
 */

#ifndef NETWORK_HPP_
#define NETWORK_HPP_

#include <string>
#include <vector>

using namespace std;

/** Platform Specific Data **/
struct SocketImplData;
struct INetAddrImplData;

class INetAddress {

public:
	INetAddress();
	INetAddress(const INetAddress& other);
	~INetAddress();

	bool fromstr(string& addressStr, unsigned int port);
	bool tostr(string& addressStr, unsigned int* port) const;

	INetAddress& operator=(const INetAddress& other);

	INetAddrImplData* implData;

};

bool resolve_inet(const string& hostStr, const string& portStr, bool lookForUDP, vector<INetAddress>& addresses);

enum SocketType {
	UNBOUND = 0,
	LISTEN_TCP = 1,
	LISTEN_UDP = 2,
	STREAM = 3
};

class Socket {

public:
	Socket();
	~Socket();

	/**
	 * Creates a new TCP port that can accept incomming connections usign the accept() function
	 * @param localAddress The local address to bind the socket to
	 * @return true if the port was successfully bound, false otherwise
	 */
	bool listen(const INetAddress& localAddress);

	/**
	 * Attempts to accept an incomming connection and initializes the supplied (unbound) socket for it as TCP stream socket.
	 * This function blocks until an connection is received.
	 * @param clientSocket The unbound socket to use for the incomming connection
	 * @return true if an connection was accepted and the socket was initialized successfully, false otherwise
	 */
	bool accept(Socket &clientSocket);

	/**
	 * Attempts to establish a connection to an TCP listen socket at the specified address.
	 * @param remoteAddress The address to connect to
	 * @return true if an connection was successfully established, false otherwise
	 */
	bool connect(const INetAddress& remoteAddress);

	/**
	 * Sends data trough the TCP connection.
	 * @param buffer The buffer holding the data
	 * @param length The length of the data
	 * @return true if the data was sent successfully, false otherwise
	 */
	bool send(const char* buffer, unsigned int length);

	/**
	 * Receives data trough the TCP connection.
	 * This function might block indefinitely until data is received.
	 * This function might return with zero bytes read, which is not an error.
	 * @param buffer The buffer to write the payload to
	 * @param length The capacity of the buffer
	 * @param received The actual number of bytes received
	 * @return true if the function did return normally (no error occurred), false otherwise
	 */
	bool receive(char* buffer, unsigned int length, unsigned int* received);

	/**
	 * Creates and new socket configured for UDP transmissions
	 * @return true if the port was successfully bound, false otherwise
	 */
	bool bind(INetAddress& localAddress);

	/**
	 * Receives data trough UDP transmissions.
	 * This function might block indefinitely until data is received.
	 * This function might return with zero bytes read, which is not an error.
	 * @param remoteAddress The sender address of the received package
	 * @param buffer The buffer to write the data to
	 * @param length The capacity of the buffer
	 * @param received The actual number of bytes received
	 * @return true if the function did return normally (no error occurred), false otherwise
	 */
	bool receivefrom(INetAddress& remoteAddress, char* buffer, unsigned int length, unsigned int* received);

	/**
	 * Sends data trough the UDP transmissions
	 * @param remoteAddress The target address to which the data should be send.
	 * @param buffer The buffer holding the data
	 * @param length The length of the data
	 * @return true if the data was sent successfully, false otherwise
	 */
	bool sendto(const INetAddress& remoteAddress, const char* buffer, unsigned int length);

	/**
	 * Closes the port.
	 */
	void close();

	/**
	 * Checks if the port is still open and operaitional.
	 * @return true if the port is still open, false otherwise
	 */
	bool isOpen();

	SocketType type() {
		return this->stype;
	}

private:
	SocketType stype;
	SocketImplData* implData;

};

bool InetInit();
void InetCleanup();

#endif /* NETWORK_HPP_ */
