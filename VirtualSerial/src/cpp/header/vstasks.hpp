/*
 * vstasks.hpp
 *
 *  Created on: 11.08.2025
 *      Author: marvi
 */

#ifndef SRC_CPP_HEADER_VSTASKS_HPP_
#define SRC_CPP_HEADER_VSTASKS_HPP_

#include <string>

bool install();
bool uninstall();

bool createPorts(const std::string& portA, const std::string& portB);
bool removePorts(const std::string& port);

#endif /* SRC_CPP_HEADER_VSTASKS_HPP_ */
