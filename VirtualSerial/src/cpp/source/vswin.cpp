/*
 * vswin.cpp
 *
 *  Created on: 12.08.2025
 *      Author: marvi
 */

#include "vstasks.hpp"
#include "setup.h"
#include <string.h>
#include <memory.h>

bool runCom0Com(const std::string& cmd) {
	return MainA(execName.c_str(), cmd.c_str()) == 0;
}

bool runDriverCmd(const std::vector<std::string>& args) {
	printf("run com0com command ...");
	std::string cmd;
	if (args.size() == 0) {
		cmd = "help";
	} else {
		for (auto s : args)
			cmd = cmd + s + " ";
	}
	return runCom0Com(cmd);
}

bool uninstall() {
	printf("invoke com0com uninstall ...\n");
	return runCom0Com("uninstall");
}

bool installPorts(const std::string& portA, const std::string& portB) {

	printf("invoke com0com install port ...\n");
	if (!runCom0Com("install PortName=" + portA + " PortName=" + portB)) {
		printf("install port failed\n");
		return false;
	}

	return true;

}

bool removePorts(const std::string& port) {

	return false;
}


