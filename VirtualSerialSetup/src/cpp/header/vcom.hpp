/*
 * serialportterminal.h
 *
 *  Created on: 06.04.2024
 *      Author: Marvin Koehler (M_Marvin)
 */

#ifndef VCOM_HPP_
#define VCOM_HPP_

#include <string>
#include <vector>

/**
 * Main entry point of the process, with C++ compatible data types.
 * @param exec The executable name
 * @param args The command line arguments
 * @return the exit code of the process, usually zero for normal termination
 */
int mainCPP(std::string& exec, std::vector<std::string>& args);
/**
 * Main entry point of the proocess
 * @param argc The number of command line arguments
 * @param argv The command line arguments
 * @return the exit code of the process, usually zero for normal termination
 */
int main(int argc, const char** argv);

bool installPort(std::string portName);
bool removePort(std::string portName);
bool removeAllPorts();
bool updateDriver();
bool installDriver();
bool uninstallDriver();

#endif /* VCOM_HPP_ */
