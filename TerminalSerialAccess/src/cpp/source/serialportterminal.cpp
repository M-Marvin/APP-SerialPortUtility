/*
 * serialportterminal.cpp
 *
 * An simple serial port terminal for windows
 *
 *  Created on: 05.04.2024
 *      Author: Marvin Koehler
 */

#include <stdio.h>
#include <iostream>
#include <thread>

#ifdef PLATFORM_WIN
#include <windows.h>
#endif
#include "serialportterminal.h"
#include <serial_port.hpp>

#ifndef BUILD_VERSION
#define BUILD_VERSION N/A
#endif
// neccessary because of an weird toolchain bug not allowing quotes in -D flags
#define STRINGIZE(x) #x
#define ASSTRING(x) STRINGIZE(x)

#define BUFFER_SIZE 128
#define CONSECUTIVE_DELAY 10
#define TRANSMISSION_TIMEOUT 1000
#define RECEPTION_TIMEOUT 1000

static bool shouldTerminate;
static bool lineEditing = false;
static char sendLineEnd = 0;
static SerialAccess::SerialPortConfiguration portConfiguration(SerialAccess::DEFAULT_PORT_CONFIGURATION);
static SerialAccess::SerialPort* port;
#ifdef PLATFORM_WIN
static HANDLE console = 0;
#endif

int main(int argc, const char** argv) {

	// disable output buffering
	setbuf(stdout, NULL);

	// print help if no arguments
	if (argc == 1) {
		std::string exec(argv[0]);
		std::string execName = exec.substr(exec.find_last_of("/\\") + 1);
		printf("%s [port name] <options ...>\n", execName.c_str());
		printf("options:\n");
		printf(" -baud [baud]\n");
		printf(" -bits [data bits]\n");
		printf(" -stops [stop bits] : one|one-half|two\n");
		printf(" -parity [parity] : none|even|odd|mark|space\n");
		printf(" -flowctrl [flow control] : none|xonxoff|rtscts|dsrdtr\n");
		printf(" -lineedit (send new line) : sendlf|sendcr\n");
		printf("serial terminal version: " ASSTRING(BUILD_VERSION) "\n");
		return 1;
	}

	// parse port name
	std::string portName(argv[1]);

	// parse options
	for (unsigned int i = 2; i < argc; i++) {
		std::string flag(argv[i]);
		if (i + 1 < argc) {
			i++;
			std::string arg(argv[i]);
			// flags with argument
			if (flag == "-baud") {
				portConfiguration.baudRate = std::strtoul(argv[i], NULL, 10);
			} else if (flag == "-bits") {
				portConfiguration.dataBits = std::strtoull(argv[i], NULL, 10);
			} else if (flag == "-stops") {
				if (arg == "one") portConfiguration.stopBits = SerialAccess::SPC_STOPB_ONE;
				if (arg == "one-half") portConfiguration.stopBits = SerialAccess::SPC_STOPB_ONE_HALF;
				if (arg == "two") portConfiguration.stopBits = SerialAccess::SPC_STOPB_TWO;
			} else if (flag == "-flowctrl") {
				if (arg == "none") portConfiguration.flowControl = SerialAccess::SPC_FLOW_NONE;
				if (arg == "xonxoff") portConfiguration.flowControl = SerialAccess::SPC_FLOW_XON_XOFF;
				if (arg == "rtscts") portConfiguration.flowControl = SerialAccess::SPC_FLOW_RTS_CTS;
				if (arg == "dsrdtr") portConfiguration.flowControl = SerialAccess::SPC_FLOW_DSR_DTR;
			} else if (flag == "-parity") {
				if (arg == "none") portConfiguration.parity = SerialAccess::SPC_PARITY_NONE;
				if (arg == "even") portConfiguration.parity = SerialAccess::SPC_PARITY_EVEN;
				if (arg == "odd") portConfiguration.parity = SerialAccess::SPC_PARITY_ODD;
				if (arg == "mark") portConfiguration.parity = SerialAccess::SPC_PARITY_MARK;
				if (arg == "space") portConfiguration.parity = SerialAccess::SPC_PARITY_SPACE;
			} else if (flag == "-lineedit") {
				lineEditing = true;
				if (arg == "sendlf") sendLineEnd = '\n';
				if (arg == "sendcr") sendLineEnd = '\r';
			} else {
				i--; // no match with argument
			}
		}
		// flags without arguments
		if (flag == "-lineedit") {
			lineEditing = true;
		}
	}

	// attempt enable raw input mode
	if (!lineEditing) {
		if (!setupConsole(false)) {
			printf("[!] unable to configure terminal, fallback to line editing mode\n");
			lineEditing = true;
		}
	}

	// attempt or fallback to line editing mode
	if (lineEditing) {
		if (!setupConsole(true)) {
			printf("[!] unable to configure terminal\n");
		}
	}

	// crate port
	port = SerialAccess::newSerialPortS(portName);

	// open port
	if (!port->openPort()) {
		printf("[!] failed to open port: %s\n", portName.c_str());
		return -1;
	}

	// configure port
	if (!port->setConfig(portConfiguration)) {
		printf("[!] failed to configure port: %s\n", portName.c_str());
		printf("[i] this usualy indiciates not supported hardware configuration or an general invalid configuration\n");
		port->closePort();
		return -1;
	}

	// configure serial timeouts
	if (!port->setTimeouts(CONSECUTIVE_DELAY, TRANSMISSION_TIMEOUT)) {
		printf("[!] failed to set port timeouts: %s\n", portName.c_str());
		port->closePort();
		return -1;
	}

	// start reception thread
	shouldTerminate = false;
	std::thread receptionThread(receptionLoop);

	// start transmission loop
	char inputChar;
	while (!shouldTerminate) {
		if (!lineEditing) {
#ifdef PLATFORM_WIN
			unsigned long int read;
			if (ReadConsole(console, &inputChar, 1, &read, NULL)) {
				port->writeBytes(&inputChar, 1);
			} else {
				printf("[!] failed to get raw console access, fallback to line mode\n");
				lineEditing = true;
			}
#else
			break;
#endif
		} else {
			std::string line;
			getline(std::cin, line);
			port->writeBytes(line.c_str(), line.length());
			if (sendLineEnd) {
				port->writeBytes(&sendLineEnd, 1);
			}
		}
	}

	// terminate transmission thread
	shouldTerminate = true;
	receptionThread.join();

	// close port
	port->closePort();

	return 0;
}

#ifdef PLATFORM_WIN

void printError() {
	DWORD errorCode = GetLastError();
	if (errorCode == 0) return;
	LPSTR msg;
	if (FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, errorCode, 0, (LPSTR) &msg, 0, NULL)) {
		printf("error code %lu: %s\n", errorCode, msg);
		LocalFree(msg);
	}
}

bool setupConsole(bool lineInput) {
	console = GetStdHandle(STD_INPUT_HANDLE);
	if (!SetConsoleMode(console, lineInput ? (ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT) : (ENABLE_PROCESSED_INPUT))) {
		printError();
		return false;
	}
	return true;
}

#else

bool setupConsole(bool lineInput) {
	return lineInput;
}

#endif

void receptionLoop() {
	char receptionBuffer[BUFFER_SIZE];
	unsigned long receptionLen = 0;

	while (!shouldTerminate) {
		receptionLen = port->readBytesConsecutive(receptionBuffer, BUFFER_SIZE, CONSECUTIVE_DELAY, RECEPTION_TIMEOUT);
		if (receptionLen > 0)
			printf("%.*s", receptionLen, receptionBuffer);
	}
}
