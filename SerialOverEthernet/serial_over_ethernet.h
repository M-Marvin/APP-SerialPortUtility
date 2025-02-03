/*
 * serial_over_ethernet.h
 *
 *  Created on: 03.02.2025
 *      Author: marvi
 */

#ifndef SERIAL_OVER_ETHERNET_H_
#define SERIAL_OVER_ETHERNET_H_

#include <network.h>

void shutdown();
void startNewHandler(Socket* socket);
void clientHandle(Socket &socket);

#endif /* SERIAL_OVER_ETHERNET_H_ */
