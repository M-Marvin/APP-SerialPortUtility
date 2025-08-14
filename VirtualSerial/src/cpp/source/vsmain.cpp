/*
 * vsmain.cpp
 *
 *  Created on: 11.08.2025
 *      Author: marvi
 */

#include <iostream>
#include "vstasks.hpp"

int mainCPP(std::string exec, std::vector<std::string>& args) {

	// disable output caching
	setbuf(stdout, NULL);

	execName = exec.substr(exec.find_last_of("/\\") + 1);

	if (args.size() == 0) {
		printf("%s <command>\n", execName.c_str());
		printf("commands:\n");
		printf(" uninstall - removes all drivers or other configurations from the system\n");
		printf(" install [port name A] [port name B] - installs a new virtual serial pair\n");
		printf(" remove [port name] - removes the virtual port and it's counterpart\n");
		printf("windows only:\n");
		printf(" com0com <com0com args ... ('help' by default)> - runs an com0com port, the virtual serial driver used on windows");
		return 1;
	}

	if (args[0] == "com0com") {
		args.erase(args.begin());
		return runDriverCmd(args) ? 0 : -1;
	} else if (args[0] == "uninstall") {
		return uninstall();
	} else if (args[0] == "install") {
		if (args.size() < 3) {
			printf("not enough arguments\n");
			return 1;
		}
		return installPorts(args[1], args[2]) ? 0 : -1;
	} else if (args[0] == "remove") {
		if (args.size() < 2) {
			printf("not enough arguments\n");
			return 1;
		}
		return removePorts(args[1]) ? 0 : -1;
	}

	printf("unknown command\n");
	return 1;
}

int main(int argc, const char** argv) {

	std::string exec(argv[0]);
	std::vector<std::string> args;
	for (unsigned int i1 = 1; i1 < argc; i1++)
		args.emplace_back(argv[i1]);

	return mainCPP(exec, args);

}
