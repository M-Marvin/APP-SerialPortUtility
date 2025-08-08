/*
 * soemain.h
 *
 * Defines the static functions for the main soe process entry.
 *
 *  Created on: 04.02.2025
 *      Author: Marvin Koehler (M_Marvin)
 */

#ifndef SOE_MAIN_HPP_
#define SOE_MAIN_HPP_

#include <vector>
#include <string>
#include <serial_port.hpp>
#include <netsocket.hpp>
#include <soeconnection.hpp>

/**
 * Starts the main process, initializes server and client connections.
 * @param serverHostName The local address string for the host to bind its listen socket to
 * @param serverHostPort The local port string for the host to bind its listen socket to.
 * @param linkArgs The additional command line arguments for connections to establish on startup.
 * @return exit code of the application, usually zero for normal termination
 */
int runMain(std::string& serverHostName, std::string& serverHostPort, std::vector<std::string>& linkArgs);

/**
 * Interprets start argument flags for connections to create.
 * @param args The command line arguments
 */
void interpretFlags(const std::vector<std::string>& args);

/**
 * Creates a new connection handler for the supplied client socket.
 * The newly created manager handles deletion of the dynamically allocated socket.
 * @param unmanagedSocket The dynamically created client socket, must be connected already
 * @param socketHostName The remote host name, used for log entries related to this connection
 * @param socketHostPort The remote host port, used for log entries related to this connection
 * @return An pointer to the newly created connection handler, or an nullptr if the creation failed
 */
SerialOverEthernet::SOESocketHandler* createConnectionHandler(NetSocket::Socket* unmanagedSocket, std::string socketHostName, std::string socketHostPort);
/**
 * Check all currently available connection handler for closed connections, and properly shutdown and delte them.
 */
void cleanupDeadConnectionHandlers();
/**
 * Attempts to establish an connection to the specified host and configures the remote ports with the supplied configurations.
 * @param remoteHost The remote server host address to connect to
 * @param remotePort The remote server host port to connect to
 * @param remoteSerial The remote server serial port path
 * @param localSerial The local serial port path
 * @param remoteConfig The remote server serial configuration
 * @param localConfig The local serial configuration
 * @return true if the connection was established successfully, false otherwise
 */
bool linkRemotePort(std::string& remoteHost, std::string& remotePort, std::string& remoteSerial, std::string& localSerial, SerialAccess::SerialPortConfiguration& remoteConfig, SerialAccess::SerialPortConfiguration& localConfig);

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

#endif
