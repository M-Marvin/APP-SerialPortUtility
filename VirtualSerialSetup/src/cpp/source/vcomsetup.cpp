/*
 * vcomsetup.cpp
 *
 *  Created on: 01.09.2025
 *      Author: marvi
 */

#include "vcom.hpp"

int mainCPP(std::string& exec, std::vector<std::string>& args) {

	if (args.size() == 0) {
		std::string execName = exec.substr(exec.find_last_of("/\\") + 1);
		printf("%s <options ...>\n", execName.c_str());
		printf("options:\n");
		printf(" -install [port name]\n");
		printf(" -remove [port name]\n");
		printf(" -removeall\n");
		printf(" -update\n");
		printf(" -drvinstall\n");
		printf(" -drvuninstall\n");
		printf("virtual serial setup version: " ASSTRING(BUILD_VERSION) "\n");
		return 1;
	}


	for (auto flag = args.begin(); flag != args.end(); flag++) {
		if (flag + 1 != args.end()) {
			// flags with argument
			if (*flag == "-install") {
				if (!installPort(*++flag)) return -1;
			} else if (*flag == "-remove") {
				if (!removePort(*++flag)) return -1;
			}
		}
		// flags without arguments
		if (*flag == "-removeall") {
			if (!removeAllPorts()) return -1;
		} else if (*flag == "-drvinstall") {
			if (!installDriver()) return -1;
		} else if (*flag == "-drvuninstall") {
			if (!installDriver()) return -1;
		} else if (*flag == "-update") {
			if (!updateDriver()) return -1;
		}
	}

	return 0;

}

int main(int argc, const char** argv) {

	std::string exec(argv[0]);
	std::vector<std::string> args;
	for (unsigned int i1 = 1; i1 < argc; i1++)
		args.emplace_back(argv[i1]);

	return mainCPP(exec, args);

}

