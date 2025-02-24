/*
 * virtualport.h
 *
 *  Created on: 11.02.2025
 *      Author: marvi
 */

#ifndef VIRTUALPORT_H_
#define VIRTUALPORT_H_

#define _AMD64_

#include <windows.h>
#include <wdf.h>

NTSTATUS DriverEntry(PDRIVER_OBJECT  driverObject, PUNICODE_STRING registryPath);

void EvtWdfDriverUnload(WDFDRIVER driver);

#endif /* VIRTUALPORT_H_ */
