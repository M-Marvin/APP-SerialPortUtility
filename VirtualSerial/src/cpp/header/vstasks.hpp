/*
 * vstasks.hpp
 *
 *  Created on: 11.08.2025
 *      Author: marvi
 */

#ifndef SRC_CPP_HEADER_VSTASKS_HPP_
#define SRC_CPP_HEADER_VSTASKS_HPP_

#include <vector>
#include <string>

static std::string execName;

bool uninstall();

bool installPorts(const std::string& portA, const std::string& portB);
bool removePorts(const std::string& port);

bool runDriverCmd(const std::vector<std::string>& args);

#endif /* SRC_CPP_HEADER_VSTASKS_HPP_ */
