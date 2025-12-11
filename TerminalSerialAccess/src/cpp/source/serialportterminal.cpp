/*
 * serialportterminal.cpp
 *
 * An simple serial port terminal for windows and linux
 *
 *  Created on: 05.04.2024
 *      Author: Marvin Koehler (M_Marvin)
 */

#include <stdio.h>
#include <iostream>
#include <thread>

#ifdef PLATFORM_WIN
#include <windows.h>
#else
#include <termios.h>
#endif
#include "serialportterminal.h"
#include <serial_port.hpp>

#ifndef BUILD_VERSION
#define BUILD_VERSION N/A
#endif
// neccessary because of an weird toolchain bug not allowing quotes in -D flags
#define STRINGIZE(x) #x
#define ASSTRING(x) STRINGIZE(x)

static bool shouldTerminate;				// if stdin was closed and the receptor thread should close
static bool lineEditing = false;			// if line editing mode is enabled
static char sendLineEnd = 0;				// if a ln or cr should be send after each line entered
static bool sendLFonCR = false;				// if an CR received should be printed as LF
static unsigned long pipeCloseDelay = 0;	// the delay for closing the receptor thread after closing stdin
static SerialAccess::SerialPortConfiguration portConfiguration(SerialAccess::DEFAULT_PORT_CONFIGURATION);
static SerialAccess::SerialPort* port;

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
		printf(" -crtolf : prints all carriage returns received as line feeds\n");
		printf(" -dclose [pipe close delay] : [ms]\n");
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
			} else if (flag == "-dclose") {
				pipeCloseDelay = std::strtoul(argv[i], NULL, 10);
			} else {
				i--; // no match with argument
			}
		}
		// flags without arguments
		if (flag == "-lineedit") {
			lineEditing = true;
		} else if (flag == "-crtolf") {
			sendLFonCR = true;
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
		setupConsole(true);
		return -1;
	}

	// configure serial timeouts
	if (!port->setTimeouts(-1, 0, -1)) {
		printf("[!] failed to set port timeouts: %s\n", portName.c_str());
		port->closePort();
		setupConsole(true);
		return -1;
	}

	// configure port
	if (!port->setConfig(portConfiguration)) {
		printf("[!] failed to configure port: %s\n", portName.c_str());
		printf("[i] this usualy indiciates not supported hardware configuration or an general invalid configuration\n");
		port->closePort();
		setupConsole(true);
		return -1;
	}

	// start reception thread
	shouldTerminate = false;
	std::thread receptionThread(receptionLoop);

	// start transmission loop
	char inputChar;
	while (!shouldTerminate && !std::cin.eof()) {
		if (!lineEditing) {
			std::cin.read(&inputChar, 1);
			port->writeBytes(&inputChar, 1);
		} else {
			std::string line;
			getline(std::cin, line);
			port->writeBytes(line.c_str(), line.length());
			if (sendLineEnd) {
				port->writeBytes(&sendLineEnd, 1);
			}
		}
	}

	// wait for the configured delay
	std::this_thread::sleep_for(std::chrono::milliseconds(pipeCloseDelay));

	// terminate transmission thread
	shouldTerminate = true;
	port->closePort();
	receptionThread.join();

	// close port
	port->closePort();

	setupConsole(true);
	return 0;
}

void receptionLoop() {
	char receptionBuffer[1];
	unsigned long receptionLen = 0;

	while (!shouldTerminate) {
		receptionLen = port->readBytes(receptionBuffer, 1);
		if (receptionLen > 0) {
			if (sendLFonCR && receptionBuffer[0] == '\r')
				receptionBuffer[0] = '\n';
			printf("%.*s", (int) receptionLen, receptionBuffer);
		}
	}
}

#ifdef PLATFORM_WIN

static HANDLE console = 0;

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
		// INVALID_HANDLE most likely means we receive piped input from an another process, so no need to setup an inexistent console
		if (GetLastError() == 6) return true;

		printError();
		return false;
	}
	return true;
}

#else

bool setupConsole(bool lineInput) {
	struct termios term;
	if (tcgetattr(fileno(stdin), &term) == -1)
		return false;

	if (lineInput)
		term.c_lflag |= (ECHO | ICANON);
	else
		term.c_lflag &= ~(ECHO | ICANON);

	if (tcsetattr(fileno(stdin), 0, &term) == -1)
		return false;

	return true;
}

#endif
