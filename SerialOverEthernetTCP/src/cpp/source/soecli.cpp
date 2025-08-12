/*
 * soecli.cpp
 *
 * Implements the command line intefeace.
 * Handles command argument parsing and help display.
 *
 *  Created on: 08.08.2025
 *      Author: Marvin Koehler (M_Marvin)
 */

#ifndef BUILD_VERSION
#define BUILD_VERSION N/A
#endif
// neccessary because of an weird toolchain bug not allowing quotes in -D flags
#define STRINGIZE(x) #x
#define ASSTRING(x) STRINGIZE(x)

#include "soemain.hpp"
#include "dbgprintf.h"

void interpretFlags(const std::vector<std::string>& args) {

	// parse arguments for connections
	std::string remoteHost;
	std::string remotePort = std::to_string(SOE_TCP_DEFAULT_SOE_PORT);
	std::string remoteSerial;
	std::string localSerial;
	SerialAccess::SerialPortConfiguration remoteConfig = SerialAccess::DEFAULT_PORT_CONFIGURATION;
	SerialAccess::SerialPortConfiguration localConfig = SerialAccess::DEFAULT_PORT_CONFIGURATION;
	bool link = false;

	for (auto flag = args.begin(); flag != args.end(); flag++) {

		// complete last link call before processing other options
		if (*flag == "-link" || *flag == "-unlink") {
			if (link) {
				if (remoteHost.empty() || remotePort.empty() || remoteSerial.empty() || localSerial.empty()) {
					printf("[!] not enough arguments for connection\n");
					continue;
				}
				link = false;

				linkRemotePort(remoteHost, remotePort, remoteSerial, localSerial, localConfig, remoteConfig);
			}
		}

		if (flag + 1 != args.end()) {
			// flags with argument
			if (*flag == "-addr") {
				remoteHost = *++flag;
			} else if (*flag == "-port") {
				remotePort = *++flag;
			} else if (*flag == "-rser") {
				remoteSerial = *++flag;
			} else if (*flag == "-lser") {
				localSerial = *++flag;
			} else {
				bool applyLocal = flag->rfind("-r", 0) != 0;
				bool applyRemote = flag->rfind("-l", 0) != 0;
				SerialAccess::SerialPortConfiguration* config = applyRemote ? &remoteConfig : &localConfig;

				if (*flag == "-lbaud" || *flag == "-rbaud" || *flag == "-baud") {
					config->baudRate = stoul(*++flag);
					if (applyRemote && applyLocal) localConfig.baudRate = remoteConfig.baudRate;
				} else if (*flag == "-lbits" || *flag == "-rbits" || *flag == "-bits") {
					config->dataBits = stoul(*++flag);
					if (applyRemote && applyLocal) localConfig.dataBits = remoteConfig.dataBits;
				} else if (*flag == "-lstops" || *flag == "-rstops" || *flag == "-stops") {
					flag++;
					if (*flag == "one") config->stopBits = SerialAccess::SPC_STOPB_ONE;
					if (*flag == "one-half") config->stopBits = SerialAccess::SPC_STOPB_ONE_HALF;
					if (*flag == "two") config->stopBits = SerialAccess::SPC_STOPB_TWO;
					if (applyRemote && applyLocal) localConfig.stopBits = remoteConfig.stopBits;
				} else if (*flag == "-lparity" || *flag == "-rparity" || *flag == "-parity") {
					flag++;
					if (*flag == "none") config->parity = SerialAccess::SPC_PARITY_NONE;
					if (*flag == "even") config->parity = SerialAccess::SPC_PARITY_EVEN;
					if (*flag == "odd") config->parity = SerialAccess::SPC_PARITY_ODD;
					if (*flag == "mark") config->parity = SerialAccess::SPC_PARITY_MARK;
					if (*flag == "space") config->parity = SerialAccess::SPC_PARITY_SPACE;
					if (applyRemote && applyLocal) localConfig.parity = remoteConfig.parity;
				} else if (*flag == "-lflowctrl" || *flag == "-rflowctrl" || *flag == "-flowctrl") {
					flag++;
					if (*flag == "none") config->flowControl = SerialAccess::SPC_FLOW_NONE;
					// XON/XOFF is handled by simply passing them trough to the other socket
					//if (*flag == "xonxoff") config->flowControl = SerialAccess::SPC_FLOW_XON_XOFF;
					if (*flag == "rtscts") config->flowControl = SerialAccess::SPC_FLOW_RTS_CTS;
					if (*flag == "dsrdtr") config->flowControl = SerialAccess::SPC_FLOW_DSR_DTR;
					if (applyRemote && applyLocal) localConfig.flowControl = remoteConfig.flowControl;
				}

			}
		}
		// flags without arguments

		if (*flag == "-link") {
			link = true;
		}
	}

	if (link) {
		if (remoteHost.empty() || remotePort.empty() || remoteSerial.empty() || localSerial.empty()) {
			printf("[!] not enough arguments for connection\n");
			return;
		}

		linkRemotePort(remoteHost, remotePort, remoteSerial, localSerial, remoteConfig, localConfig);
	}
}

int mainCPP(std::string& exec, std::vector<std::string>& args) {

	// disable output caching
	setbuf(stdout, NULL);
	dbgprintf("[DBG] test dbg output enabled\n");

	// print help when no arguments supplied
	if (args.size() == 0) {
		std::string execName = exec.substr(exec.find_last_of("/\\") + 1);
		printf("%s <server only options ...> <-link <link options ...> ...>\n", execName.c_str());
		printf("options:\n");
		printf(" -addr [local IP]\n");
		printf(" -port [local network port]\n");
		printf("link options:\n");
		printf(" -addr [remote IP]\n");
		printf(" -port [remote network port]\n");
		printf(" -rser [remote serial port]\n");
		printf(" -lser [serial port]\n");
		printf(" -(l|r|)baud [serial baud]\n");
		printf(" -(l|r|)bits [data bits]\n");
		printf(" -(l|r|)flowctrl [flow control] : none|rtscts|dsrdtr\n");
		printf(" -(l|r|)stops [stop bits] : one|one-half|two\n");
		printf(" -(l|r|)parity [parity] : none|even|odd|mark|space\n");
		printf(" (l - local only | r - remote only | both)\n");
		printf("serial over ethernet version: " ASSTRING(BUILD_VERSION) "\n");
		return 1;
	}

	// default configuration
	std::string serverHostPort = std::to_string(SOE_TCP_DEFAULT_SOE_PORT);
	std::string serverHostName = ""; // empty means create no server

	// parse arguments for network connection
	auto flag = args.begin();
	for (; flag != args.end(); flag++) {
		if (flag + 1 != args.end()) {
			// flags with argument
			if (*flag == "-addr") {
				serverHostName = *++flag;
			} else if (*flag == "-port") {
				serverHostPort = *++flag;
			}
		}
		// flags without arguments
		if (*flag == "-link") {
			break; // end of server arguments
		}
	}
	if (flag != args.begin())
		args.erase(args.begin(), flag - 1);

	return runMain(serverHostName, serverHostPort, args);
}

int main(int argc, const char** argv) {

	std::string exec(argv[0]);
	std::vector<std::string> args;
	for (unsigned int i1 = 1; i1 < argc; i1++)
		args.emplace_back(argv[i1]);

	return mainCPP(exec, args);

}
