/*
 * serialportterminal.cpp
 *
 *  Created on: 05.04.2024
 *      Author: Marvin Koehler
 */

#include <serial_port.h>
#include <stdio.h>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <chrono>

#include <windows.h>
#include "serialportterminal.h"

#define BUFFER_SIZE 128
#define CONSECUTIVE_DELAY 500

using namespace std;

static bool shouldTerminate;
static SerialPort* port;

char transmitionBuffer[BUFFER_SIZE];
unsigned long transmitionLen = 0;
condition_variable transmitionCV;
mutex transmitionMutex;
chrono::milliseconds lastInput;

char receptionBuffer[BUFFER_SIZE];
unsigned long receptionLen = 0;

int main(int argc, const char** argv) {

	if (argc < 3) {
		printf("spa [port name] [baud]\n");
		return 1;
	}

	const char* portName = argv[1];
	int baud = atoi(argv[2]);

	if (baud <= 0) {
		printf("invalid baud %d\n", baud);
		return 2;
	}

	printf("connecting to port '%s'\n", portName);

	port = new SerialPort(portName);

	if (!port->openPort()) {
		printf("failed to open port!\n");
		return -1;
	}

	port->setBaud(baud);
	port->setTimeouts(100, 100);

	printf("== command not send until enter is typed ==\n");

	/* loop start */

	shouldTerminate = false;
	thread receptionThread(receptionLoop);
	thread transmitionThread(transmitionLoop);

	DWORD  mode;
	HANDLE hstdin = GetStdHandle( STD_INPUT_HANDLE );
	GetConsoleMode( hstdin, &mode );
	SetConsoleMode( hstdin, ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT );  // see note below

	char inputChar;
	while (!shouldTerminate) {
		unsigned long int read;
		ReadConsole(hstdin, &inputChar, 1, &read, NULL);
		{
			std::lock_guard lk(transmitionMutex);
			if (transmitionLen < BUFFER_SIZE) transmitionBuffer[transmitionLen++] = inputChar;
			lastInput = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch());
		}
		if (transmitionLen == BUFFER_SIZE) transmitionCV.notify_one();
	}

	shouldTerminate = true;
	receptionThread.join();
	transmitionThread.join();

	/* loop end */

	port->closePort();

	return 0;
}

void transmitionLoop() {
	while (!shouldTerminate) {
		std::unique_lock lk(transmitionMutex);
		transmitionCV.wait_for(lk, chrono::milliseconds(CONSECUTIVE_DELAY));

		chrono::milliseconds tnow = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch());
		chrono::milliseconds tsinceInput = tnow - lastInput;
		if (transmitionLen > 0 && (tsinceInput > chrono::milliseconds(CONSECUTIVE_DELAY) || transmitionLen == BUFFER_SIZE)) {
			unsigned long transmitted = port->writeBytes(transmitionBuffer, transmitionLen);
			transmitionLen -= transmitted;
		}
	}
}

void receptionLoop() {
	while (!shouldTerminate) {
		receptionLen = port->readBytes(receptionBuffer, BUFFER_SIZE);
		if (receptionLen > 0) {
			for (unsigned long i = 0; i < receptionLen; i++) printf("%c", receptionBuffer[i]);
			receptionLen = 0;
		}
	}
}
