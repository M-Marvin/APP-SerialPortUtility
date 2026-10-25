/*
 * test.cpp
 *
 *  Created on: 08.09.2026
 *      Author: marvi
 */
#define _XOPEN_SOURCE 600 // required for posix_openpt()

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include <shared_mutex>
#include <condition_variable>

#include <virtual_serial_port.hpp>

std::mutex m_stop;
std::condition_variable cv_stop;

bool stop = false;

void signal_handler(int signal) {
	if (signal == SIGINT || signal == SIGTERM) {
		cv_stop.notify_all();
	}
}

int main(int argn, const char** argv) {

//	if (signal(SIGINT, signal_handler) == SIG_ERR || signal(SIGTERM, signal_handler) == SIG_ERR) {
//		printf("signal handler could not be registered\n");
//		return -1;
//	}

	printf("Hello World!\n");



	const char* portFile = "/dev/ttyV0"; // "\\\\.\\COM4";
	SerialAccess::VirtualSerialPort* port = SerialAccess::newVirtualSerialPort(portFile); // SerialAccess::newVirtualSerialPort(portFile);

	if (!port->openPort()) {
		printf("unable to open port\n");
		return -4;
	}

	port->setTimeouts(100, 0, 100);

	printf("read ...\n");

	char buffer[1024] {0};
	int readBytes = port->readBytes(buffer, 1024, true);

	printf("readBytes: %d\n", readBytes);

	port->setTimeouts(-1, 1000, 100);

	printf("read ...\n");

	readBytes = port->readBytes(buffer, 1024, true);

	printf("readBytes: %d\n", readBytes);

	printf("wait for config change ...\n");

	bool configChange = true;
	bool comStateChange = false;
	bool dataReceived = false;
	bool dataTransmitted = false;
	port->waitForEvents(configChange, comStateChange, dataReceived, dataTransmitted, true);

	printf("config changed\n");

	port->closePort();

//	std::unique_lock<std::mutex> lock(m_stop);
//	cv_stop.wait(lock);

	return 0;

}
