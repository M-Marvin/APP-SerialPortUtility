/*
 * drvsetup.cpp
 *
 *  Created on: 01.09.2025
 *      Author: marvi
 */

#ifndef BUILD_VERSION
#define BUILD_VERSION N/A
#endif
// neccessary because of an weird toolchain bug not allowing quotes in -D flags
#define STRINGIZE(x) #x
#define ASSTRING(x) STRINGIZE(x)

#define INIT_DRV_GUIDS
#include "VCOM/public.h"
#include "VCOM/serial.h"
#include <windows.h>
#include <setupapi.h>
#include <newdev.h>
#include <stdio.h>

void printError(const char* format) {
	DWORD errorCode = GetLastError();
	if (errorCode == 0) return;
	LPSTR msg;
	if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&msg, 0, NULL) > 0) {
		printf(format, errorCode, msg); fflush(stdout);
		LocalFree(msg);
	} else {
		printf(format, errorCode, "<no error message defined>\n"); fflush(stdout);
	}
}

bool removeAllPorts()
{

}

bool removePort(std::string& portName)
{
	printf("DRIVER DEVICE REMOVE STARTED\n");

// Computer\HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\ROOT\PORTS\0001

	SetupDiCallClassInstaller(DIF_REMOVE, DeviceInfoSet, DeviceInfoData);

	printf("DRIVER DEVICE REMOVE COMPLETED\n");
	return true;
}

bool installPort(std::string& portName)
{
	printf("DRIVER DEVICE INSTALL STARTED\n");

	// find driver INF
	char infFullPath[MAX_PATH] = {0};
	if (GetFullPathNameA("VCOM.inf", MAX_PATH, infFullPath, NULL) >= MAX_PATH) {
		printError("error 0x%x in VirtualSerialPort:createPort:GetFullPathNameA: %s");
		return false;
	}

	printf("DRIVER LOAD INF: %s\n", infFullPath);

	// read class from driver INF
	GUID vcomClassGUID;
	char vcomClassName[MAX_CLASS_NAME];
	if (!SetupDiGetINFClassA(infFullPath, &vcomClassGUID, vcomClassName, MAX_CLASS_NAME, NULL)) {
		printError("error 0x%x in VirtualSerialPort:createPort:SetupDiGetINFClassA: %s");
		return false;
	}

	// create device info container
	HDEVINFO deviceInfoSet = SetupDiCreateDeviceInfoList(&vcomClassGUID, NULL);
	if (deviceInfoSet == INVALID_HANDLE_VALUE) {
		printError("error 0x%x in VirtualSerialPort:createPort:SetupDiCreateDeviceInfoList: %s");
		SetupDiDestroyDeviceInfoList(deviceInfoSet);
		return false;
	}

	// create device info in container
	SP_DEVINFO_DATA deviceInfoData = {0};
	deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
	if (!SetupDiCreateDeviceInfoA(deviceInfoSet, vcomClassName, &vcomClassGUID, NULL, NULL, DICD_GENERATE_ID, &deviceInfoData)) {
		printError("error 0x%x in VirtualSerialPort:createPort:SetupDiCreateDeviceInfoA: %s");
		SetupDiDestroyDeviceInfoList(deviceInfoSet);
		return false;
	}

	// configure hardware ID
	if (!SetupDiSetDeviceRegistryPropertyA(deviceInfoSet, &deviceInfoData, SPDRP_HARDWAREID, (const unsigned char*) HARDWARE_ID_VCOM, strlen(HARDWARE_ID_VCOM) + 1)) {
		printError("error 0x%x in VirtualSerialPort:createPort:SetupDiSetDeviceRegistryPropertyA: %s");
		SetupDiDestroyDeviceInfoList(deviceInfoSet);
		return false;
	}

	// install device
	if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE, deviceInfoSet, &deviceInfoData)) {
		printError("error 0x%x in VirtualSerialPort:createPort:SetupDiCallClassInstaller: %s");
		SetupDiDestroyDeviceInfoList(deviceInfoSet);
		return false;
	}

	SetupDiDestroyDeviceInfoList(deviceInfoSet);

	printf("DRIVER DEVICE INSTALL COMPLETED\n");
	return true;
}

bool updateDriver()
{

	// find driver INF
	char infFullPath[MAX_PATH] = {0};
	if (GetFullPathNameA("VCOM.inf", MAX_PATH, infFullPath, NULL) >= MAX_PATH) {
		printError("error 0x%x in VirtualSerialPort:createPort:GetFullPathNameA: %s");
		return false;
	}

	printf("DRIVER LOAD INF: %s\n", infFullPath);

	// update driver to let it initialize the new devices
	if (!UpdateDriverForPlugAndPlayDevicesA(NULL, HARDWARE_ID_VCOM, infFullPath, 0, NULL)) {
		printError("error 0x%x in VirtualSerialPort:createPort:UpdateDriverForPlugAndPlayDevicesA: %s");
		return false;
	}

}

bool installDriver()
{
	return false;
}
bool uninstallDriver()
{
	return false;
}
