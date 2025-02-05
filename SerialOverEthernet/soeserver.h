/*
 * serial_over_ethernet.h
 *
 *  Created on: 03.02.2025
 *      Author: marvi
 */

#ifndef SOESERVER_H_
#define SOESERVER_H_

#include <network.h>

void shutdown();
void cleanupClosedClients();
void handleClientReception();

#endif /* SOESERVER_H_ */
